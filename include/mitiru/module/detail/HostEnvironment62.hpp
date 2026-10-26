#pragma once

/// @file HostEnvironment62.hpp
/// @brief ABI v62（ADR 0099）の host 環境を境界型へ変換する型と関数の宣言。
/// OS を呼ばない処理だけを置き、Engine なしでテストする。OS への問い合わせは platform/win32/ と apps/mitiru_host/ に置く

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/input/InputTimingSettings.hpp>
#include <mitiru/module/HostView.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/module/detail/HostBoundary62.hpp>

namespace mitiru::module::detail
{

/// settings::kChangeEnvironment と同じ値。Engine が settings を include しなくて済むよう、ここにも置く
inline constexpr std::uint32_t kSettingsChangeEnvironment = 1u << 7;

inline constexpr std::size_t kHostNameMaxBytes = static_cast<std::size_t>(kHostNameLen) - 1;
inline constexpr std::size_t kHostKeyLabelMaxBytes = static_cast<std::size_t>(kHostKeyLabelLen) - 1;

/// UTF-8 の文字の途中で切らず、maxBytes 以内に収める
[[nodiscard]] inline std::string truncateUtf8(std::string_view s, std::size_t maxBytes)
{
	if (s.size() <= maxBytes) { return std::string(s); }
	std::size_t n = maxBytes;
	while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) { --n; }
	return std::string(s.substr(0, n));
}

/// ゲームへ見せた名前の一覧。shown は HostView に置く名前で、63 byte 以内かつ重複なし。full は host が持つ元の名前で、空のこともある
struct PublishedNames
{
	std::vector<std::string> shown;
	std::vector<std::string> full;
	std::vector<std::size_t> source;   ///< 元の一覧での添字 (一覧が上限を超えると並びが変わるので、host が元の項目へ戻すのに使う)
	std::uint8_t selected = kHostNoSelection;

	[[nodiscard]] bool operator==(const PublishedNames&) const noexcept = default;
};

/// 元の名前を HostView に置ける形にする。長い名前を切り、重複した名前には " (2)" を付ける。
/// 上限を超えるときは、選んでいる項目を最後の枠に入れる。
/// 日本語の機器名 ("ヘッドホン (…Hands-Free AG Audio)") は 63 byte を超えるため、findPublished で見せた名前から元の名前へ戻す
[[nodiscard]] inline PublishedNames publishNames(const std::vector<std::string>& names, int selected)
{
	PublishedNames out;
	std::vector<std::size_t> order;
	const std::size_t cap = static_cast<std::size_t>(kHostListMax);
	for (std::size_t i = 0; i < names.size() && order.size() < cap; ++i) { order.push_back(i); }
	if (selected >= 0 && static_cast<std::size_t>(selected) >= cap && selected < static_cast<int>(names.size()))
	{
		order.back() = static_cast<std::size_t>(selected);
	}
	for (const std::size_t i : order)
	{
		// 名前が空の機器には見せる名前を付ける。元の名前は空のまま残す。空は OS の既定を表すため、機器としては選べない
		const std::string& original = names[i];
		const std::string base = original.empty() ? "Device " + std::to_string(i + 1) : original;
		std::string shown = truncateUtf8(base, kHostNameMaxBytes);
		for (int n = 2; std::find(out.shown.begin(), out.shown.end(), shown) != out.shown.end(); ++n)
		{
			const std::string suffix = " (" + std::to_string(n) + ")";
			shown = truncateUtf8(base, kHostNameMaxBytes - suffix.size()) + suffix;
		}
		if (static_cast<int>(i) == selected) { out.selected = static_cast<std::uint8_t>(out.shown.size()); }
		out.shown.push_back(std::move(shown));
		out.full.push_back(original);
		out.source.push_back(i);
	}
	return out;
}

[[nodiscard]] inline std::optional<std::size_t> findPublished(const PublishedNames& names, std::string_view requested)
{
	if (requested.empty()) { return std::nullopt; }
	for (std::size_t i = 0; i < names.shown.size(); ++i)
	{
		if (names.shown[i] == requested) { return i; }
	}
	for (std::size_t i = 0; i < names.full.size(); ++i)
	{
		if (names.full[i] == requested) { return i; }
	}
	return std::nullopt;
}

/// この PC の環境情報。host が変化を検出したときに Engine へ渡す。録画には含めず、snapshot と HostView の元にする
struct HostEnvironment
{
	PublishedNames monitors;
	PublishedNames audioDevices;
	std::uint32_t refreshMilliHz = 0;
	std::uint8_t hdrCapable = 0;
	std::uint8_t osAccessibility = 0;
	/// VK ごとの刻印。HostView::keyLabels と同じ順に並ぶ。描画だけに使うため、変わっても知らせない
	std::array<char, static_cast<std::size_t>(kHostKeyCount) * kHostKeyLabelLen> keyLabels{};

	/// kChangeEnvironment で知らせる範囲が同じかを調べる。刻印は含めない
	[[nodiscard]] bool sameFacts(const HostEnvironment& o) const noexcept
	{
		return monitors == o.monitors && audioDevices == o.audioDevices && refreshMilliHz == o.refreshMilliHz &&
		       hdrCapable == o.hdrCapable && osAccessibility == o.osAccessibility;
	}
};

inline void copyNameList(char (&dst)[kHostListMax][kHostNameLen], const PublishedNames& names) noexcept
{
	std::memset(dst, 0, sizeof(dst));
	for (std::size_t i = 0; i < names.shown.size() && i < static_cast<std::size_t>(kHostListMax); ++i)
	{
		std::memcpy(dst[i], names.shown[i].data(), std::min(names.shown[i].size(), kHostNameMaxBytes));
	}
}

/// 一覧、選び、刻印を HostView に書く。揺れの倍率と keyMode は変えない
inline void applyEnvironmentToHostView(HostView& view, const HostEnvironment& env) noexcept
{
	view.monitorCount = static_cast<std::uint8_t>(env.monitors.shown.size());
	view.monitorSelected = env.monitors.selected;
	view.audioDeviceCount = static_cast<std::uint8_t>(env.audioDevices.shown.size());
	view.audioDeviceSelected = env.audioDevices.selected;
	copyNameList(view.monitorNames, env.monitors);
	copyNameList(view.audioDeviceNames, env.audioDevices);
	std::memcpy(view.keyLabels, env.keyLabels.data(), sizeof(view.keyLabels));
}

/// 4 つの入力時間をミリ秒で持つ。順は hold / tapWindow / repeatDelay / repeatInterval で、InputSnapshot のフィールドの順とは異なる
using InputTimingMs = std::array<std::uint16_t, 4>;

/// InputSnapshot の v62 のフィールドをすべて書く。snapshot は永続バッファのため、毎フレーム全フィールドを書く。
/// 入力時間は設定がない間 0 とし、ゲームの既定を使う。オンラインでは HostOnline が snapshot を作り直すため、この PC の値は席に届かない
inline void fillSnapshotEnvironment(InputSnapshot& snap, const InputTimingMs& timing, const HostEnvironment& env, const HostView& view) noexcept
{
	snap.inputHoldMs = timing[0];
	snap.inputTapWindowMs = timing[1];
	snap.inputRepeatDelayMs = timing[2];
	snap.inputRepeatIntervalMs = timing[3];
	snap.osAccessibility = env.osAccessibility;
	snap.hdrCapable = env.hdrCapable;
	snap.refreshMilliHz = env.refreshMilliHz;
	snap.monitorCount = view.monitorCount;
	snap.audioDeviceCount = view.audioDeviceCount;
}

/// host から受け取った環境情報を持ち、HostView の生成と変化の判定を行う。Engine なしでテストできるよう、別の型にする
class HostEnvironmentCache
{
public:
	/// @return 最初の読み込み後に、sameFacts の範囲が変わったら true。
	/// 最初の読み込みは変化として扱わず、最初の update が見る snapshot には正しい値を入れる
	bool update(const HostEnvironment& env)
	{
		const bool changed = m_set && !m_env.sameFacts(env);
		m_env = env;
		m_set = true;
		m_view = HostView{};
		applyEnvironmentToHostView(m_view, m_env);
		return changed;
	}

	[[nodiscard]] const HostEnvironment& environment() const noexcept { return m_env; }
	/// 一覧、選び、刻印だけを持つ HostView。揺れの倍率と keyMode は呼ぶ側が加える
	[[nodiscard]] const HostView& view() const noexcept { return m_view; }

private:
	HostEnvironment m_env;
	HostView m_view{};
	bool m_set = false;
};

/// 刻印を HostView の 1 枠に 7 byte まで入れる。収まらない場合は空にし、HUD で表の名前を使う。途中で切れた文字より表の名前を優先する
inline void setKeyLabel(HostEnvironment& env, int vk, std::string_view label) noexcept
{
	if (vk < 0 || vk >= kHostKeyCount) { return; }
	char* slot = env.keyLabels.data() + static_cast<std::size_t>(vk) * kHostKeyLabelLen;
	std::memset(slot, 0, static_cast<std::size_t>(kHostKeyLabelLen));
	if (label.size() <= kHostKeyLabelMaxBytes) { std::memcpy(slot, label.data(), label.size()); }
}

/// 3 つの OS 設定を kOsA11y* の bit にする。クライアント領域のアニメーションが切れている状態を「動きの軽減」とするため、値を反転する
[[nodiscard]] constexpr std::uint8_t osAccessibilityBits(bool highContrastOn, bool clientAreaAnimationOn, bool transparencyOn) noexcept
{
	std::uint8_t bits = 0;
	if (highContrastOn) { bits = static_cast<std::uint8_t>(bits | kOsA11yHighContrast); }
	if (!clientAreaAnimationOn) { bits = static_cast<std::uint8_t>(bits | kOsA11yReduceMotion); }
	if (!transparencyOn) { bits = static_cast<std::uint8_t>(bits | kOsA11yReduceTransparency); }
	return bits;
}

/// MITIRU_OS_A11Y の値を解析する。語は , または空白で区切り、"none" はすべて無効とする。読めない語があれば nullopt。
/// スクリプトの実行や撮影で、PC の設定に関係なく同じ値を作るために使う
[[nodiscard]] inline std::optional<std::uint8_t> parseOsAccessibilityOverride(std::string_view text)
{
	std::uint8_t bits = 0;
	bool any = false;
	std::size_t i = 0;
	while (i < text.size())
	{
		while (i < text.size() && (text[i] == ',' || text[i] == ' ')) { ++i; }
		std::size_t j = i;
		while (j < text.size() && text[j] != ',' && text[j] != ' ') { ++j; }
		if (j == i) { break; }
		const std::string_view word = text.substr(i, j - i);
		i = j;
		any = true;
		if (word == "high_contrast") { bits = static_cast<std::uint8_t>(bits | kOsA11yHighContrast); }
		else if (word == "reduce_motion") { bits = static_cast<std::uint8_t>(bits | kOsA11yReduceMotion); }
		else if (word == "reduce_transparency") { bits = static_cast<std::uint8_t>(bits | kOsA11yReduceTransparency); }
		else if (word != "none") { return std::nullopt; }
	}
	if (!any) { return std::nullopt; }
	return bits;
}

/// QueryDisplayConfig のリフレッシュレートの分数をミリヘルツにする。59.94 Hz は 59940 となり、整数の Hz では 59000 になる
[[nodiscard]] constexpr std::uint32_t refreshMilliHzFromRational(std::uint32_t numerator, std::uint32_t denominator) noexcept
{
	if (denominator == 0) { return 0; }
	const std::uint64_t mhz = (static_cast<std::uint64_t>(numerator) * 1000u + denominator / 2u) / denominator;
	return mhz > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(mhz);
}

[[nodiscard]] inline InputTimingMs inputTimingMsOf(const input::InputTimingSettings& t) noexcept
{
	return { secondsToMs16(t.holdSec), secondsToMs16(t.tapWindowSec), secondsToMs16(t.repeatDelaySec),
	         secondsToMs16(t.repeatIntervalSec) };
}

}  // namespace mitiru::module::detail
