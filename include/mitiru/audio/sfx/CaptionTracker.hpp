#pragma once

/// @file CaptionTracker.hpp
/// @brief 鳴った効果音の字幕を、画面に出す数行に束ねて寿命で消す。
/// @details 同じ音の連続は 1 行にまとめて回数を数える。寿命は音のステップ (60 Hz) の積み上げで数え、壁時計を読まないので、
///          録画の再生や --headless-3d の撮影で同じ絵になる。画面が字幕で埋まらないよう行数に上限を置く。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/audio/SpatialAudio.hpp>
#include <mitiru/audio/sfx/SfxCaption.hpp>

namespace mitiru::audio::sfx
{

enum class CaptionSide : std::uint8_t { None, Front, Behind, Left, Right };

[[nodiscard]] constexpr std::string_view captionSideName(CaptionSide s) noexcept
{
	switch (s)
	{
	case CaptionSide::Front:  return "front";
	case CaptionSide::Behind: return "behind";
	case CaptionSide::Left:   return "left";
	case CaptionSide::Right:  return "right";
	case CaptionSide::None:   break;
	}
	return "";
}

/// @brief 聞き手から見た音の方向。真上・真下や重なるほど近い音は方向なし。
[[nodiscard]] inline CaptionSide captionSideOf(const SpatialAudio& listener, const AudioVec3& source) noexcept
{
	constexpr float kMinDistance = 0.5f;
	constexpr float kMinHorizontal = 0.3f;
	if (listener.listener().position.distanceTo(source) < kMinDistance) { return CaptionSide::None; }
	const AudioVec3 d = listener.toListenerSpace(source);   // +x 右、-z 前
	const float ahead = -d.z;
	if (std::hypot(d.x, ahead) < kMinHorizontal) { return CaptionSide::None; }
	const float deg = std::atan2(d.x, ahead) * (180.0f / 3.14159265f);
	if (std::fabs(deg) <= 45.0f) { return CaptionSide::Front; }
	if (std::fabs(deg) >= 135.0f) { return CaptionSide::Behind; }
	return deg > 0.0f ? CaptionSide::Right : CaptionSide::Left;
}

struct CaptionLine
{
	std::string textKey;
	std::string speakerKey;
	CaptionKind kind = CaptionKind::Ambient;
	CaptionSide side = CaptionSide::None;
	int count = 1;
	float remainSec = 0.0f;
	std::uint32_t handle = 0;
};

class CaptionTracker
{
public:
	static constexpr float kLifeSec = 3.0f;
	static constexpr std::size_t kMaxLines = 4;

	/// @brief 設定を替える。許さなくなった種類の行はすぐ消す
	void setPrefs(const CaptionPrefs& prefs)
	{
		if (prefs == m_prefs) { return; }
		m_prefs = prefs;
		const auto gone = std::remove_if(m_lines.begin(), m_lines.end(),
			[&](const CaptionLine& l) { return !m_prefs.allows(l.kind); });
		m_lines.erase(gone, m_lines.end());
		m_dirty = true;
	}
	[[nodiscard]] const CaptionPrefs& prefs() const noexcept { return m_prefs; }

	/// @brief 音が鳴った。handle が 0 でなく、すでに出ている行と同じなら、鳴らし直しでなく調整とみなして数えない
	void onPlay(const SfxCaption& c, std::uint32_t handle, bool spatial, const SpatialAudio& listener,
	            const AudioVec3& source)
	{
		if (!c.present() || !m_prefs.allows(c.kind)) { return; }
		const CaptionSide side = (c.directional && m_prefs.directional && spatial)
			? captionSideOf(listener, source) : CaptionSide::None;
		// 文の鍵が同じでも、話者や種類が違う音は別の行にする (種類ごとの切り替えで、違う音の行が消えないように)
		auto it = std::find_if(m_lines.begin(), m_lines.end(), [&](const CaptionLine& l) {
			return l.textKey == c.textKey && l.speakerKey == c.speakerKey && l.kind == c.kind;
		});
		if (it != m_lines.end())
		{
			if (handle != 0 && it->handle == handle) { return; }
			++it->count;
			it->remainSec = kLifeSec;
			it->side = side;
			it->handle = handle;
			m_latest = *it;
			// 鳴らし直した行は一番新しい所へ寄せる。上限で消えるのは、いちばん長く鳴っていない行
			std::rotate(it, it + 1, m_lines.end());
		}
		else
		{
			m_lines.push_back({ c.textKey, c.speakerKey, c.kind, side, 1, kLifeSec, handle });
			if (m_lines.size() > kMaxLines) { m_lines.erase(m_lines.begin()); }
			m_latest = m_lines.back();
		}
		m_latestSerial = ++m_serial;
		m_dirty = true;
	}

	void step(float dt)
	{
		if (m_lines.empty()) { return; }
		for (CaptionLine& l : m_lines) { l.remainSec -= dt; }
		const auto gone = std::remove_if(m_lines.begin(), m_lines.end(),
			[](const CaptionLine& l) { return l.remainSec <= 0.0f; });
		if (gone != m_lines.end())
		{
			m_lines.erase(gone, m_lines.end());
			m_dirty = true;
		}
	}

	/// @brief 前に呼んでから行が変わったか。呼ぶと戻る
	[[nodiscard]] bool takeDirty() noexcept
	{
		const bool d = m_dirty;
		m_dirty = false;
		return d;
	}

	[[nodiscard]] const std::vector<CaptionLine>& lines() const noexcept { return m_lines; }
	/// 一番新しく鳴った行 (読み上げに渡す 1 行)。serial が変わるたびに新しい音
	[[nodiscard]] const CaptionLine& latest() const noexcept { return m_latest; }
	[[nodiscard]] std::uint32_t latestSerial() const noexcept { return m_latestSerial; }
	/// 前に呼んでから新しい音が鳴ったか。設定を替えただけでは真にならない (読み上げが同じ字幕を読み直さないため)
	[[nodiscard]] bool takeLatestNew() noexcept
	{
		const bool fresh = m_latestSerial != m_latestTaken;
		m_latestTaken = m_latestSerial;
		return fresh;
	}

private:
	CaptionPrefs m_prefs;
	std::vector<CaptionLine> m_lines;
	CaptionLine m_latest;
	std::uint32_t m_serial = 0;
	std::uint32_t m_latestSerial = 0;
	std::uint32_t m_latestTaken = 0;
	bool m_dirty = false;
};

/// @brief 画面に出す 1 行の文。方向、話者、文、回数の順。translate は鍵を今の言語の文にする関数
template <class Translate>
[[nodiscard]] std::string captionText(const CaptionLine& l, const CaptionPrefs& prefs, Translate&& translate)
{
	std::string out;
	if (l.side != CaptionSide::None && prefs.directional)
	{
		out += "(" + translate("mitiru.caption.dir." + std::string(captionSideName(l.side))) + ") ";
	}
	if (prefs.speakerNames && !l.speakerKey.empty()) { out += translate(l.speakerKey) + ": "; }
	out += translate(l.textKey);
	if (l.count > 1) { out += " \xC3\x97" + std::to_string(l.count); }
	return out;
}

/// @brief view.caption_lines に渡す JSON: [{"text": 表示する文, "kind": "alert" など}]。古い行が先
template <class Translate>
[[nodiscard]] std::string captionLinesJson(
	const std::vector<CaptionLine>& lines, const CaptionPrefs& prefs, Translate&& translate)
{
	nlohmann::json arr = nlohmann::json::array();
	for (const CaptionLine& l : lines)
	{
		arr.push_back({ { "text", captionText(l, prefs, translate) }, { "kind", std::string(captionKindName(l.kind)) } });
	}
	return arr.dump();
}

}  // namespace mitiru::audio::sfx
