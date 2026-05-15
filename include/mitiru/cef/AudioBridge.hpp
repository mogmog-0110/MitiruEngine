#pragma once

/// @file AudioBridge.hpp
/// @brief CEF ↔ C++ 間の AudioMixer 制御 bridge (G-16)
///
/// **Why.** 各 CEF ページが `<audio>` タグで BGM を持つパターンだと scene
/// transition (`loadUrl`) のたびに `<audio>` が破棄され、同一 BGM キーでも
/// リスタートが走る。engine 側の `AudioMixer` は scene を跨いで生存するので、
/// これを JS から叩けるようにすれば「同キーなら継続、異キーなら cross-fade」
/// が自然に書ける。
///
/// **Registered handlers (JS → C++):**
/// - `audio.playBgm`          payload: "BGM_KEY"            → 同キーなら no-op、違えば play
/// - `audio.stopBgm`          payload: ""                  → BGM 停止
/// - `audio.crossFadeBgm`     payload: "BGM_KEY|duration_ms"→ 異キーなら cross-fade、同キーなら no-op
/// - `audio.playSe`           payload: "SE_KEY"            → SE 再生
/// - `audio.setCategoryVolume` payload: "bgm|0.8" or "se|1.0" or "voice|0.6"
/// - `audio.setMasterVolume`  payload: "0.8"
/// - `audio.currentBgm`       payload: ""                  → response: 現在 BGM key
///
/// **Payload 形式.** 任意セパレータが必要な場面は `|` を使う (JSON パーサを
/// 引き込みたくないため)。
///
/// **Usage:**
/// ```cpp
///   mitiru::audio::AudioMixer mixer;
///   auto* ctx = engine.cefContext();
///   mitiru::cef::bindAudioBridge(*ctx, mixer);
///
///   // JS side:
///   //   window.cefQuery({ request: "audio.playBgm|BGM_ACTION" });
///   //   window.cefQuery({ request: "audio.crossFadeBgm|BGM_CLEAR|800" });
/// ```
///
/// **AudioMixer との関係.** BGM は `SoundCategory::Bgm` で排他管理される
/// (同時 1 チャンネル)。このブリッジは **現在の BGM key を記録**し、同キー
/// 再要求に対しては `AudioMixer::play()` を呼ばず no-op にする (`play` は
/// 排他再起動するため)。

#include <charconv>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include <mitiru/audio/AudioMixer.hpp>

// Callback-injected bridge (StateStore と同じ設計パターン)。
// MitiruCefContext.hpp を include しないため CEF 未定義ビルドでもコンパイル可能、
// 単体テストもモック callback で走らせられる。
// 実際の CEF wire 呼び出し例は下部 Doxygen 参照。

namespace mitiru::cef
{

/// @brief AudioMixer と CEF bridge を結ぶ state-holder
/// @details `bindAudioBridge()` が内部で 1 つ生成し、`CefContext` のライフ
///          タイムに寄生して handlers を登録する。同名のグローバルは持たない
///          (engine が複数 CEF overlay を持つケースを想定)。
class AudioBridgeState
{
public:
	AudioBridgeState(audio::AudioMixer& mixer) noexcept
		: m_mixer(&mixer)
	{
	}

	/// @brief BGM を再生する (同キーなら no-op)
	void playBgm(std::string_view key)
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (m_currentBgmKey == key) return;
		m_mixer->play(key, audio::SoundCategory::Bgm, /*loop=*/true);
		m_currentBgmKey = std::string(key);
	}

	/// @brief BGM を停止する
	void stopBgm()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_mixer->stopByCategory(audio::SoundCategory::Bgm);
		m_currentBgmKey.clear();
	}

	/// @brief 異キーなら cross-fade、同キーなら no-op
	void crossFadeBgm(std::string_view key, float durationSec)
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (m_currentBgmKey == key) return;
		m_mixer->crossfadeBgm(key, durationSec);
		m_currentBgmKey = std::string(key);
	}

	/// @brief SE を再生する (同時多発可)
	void playSe(std::string_view key)
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_mixer->play(key, audio::SoundCategory::Se);
	}

	/// @brief カテゴリ別ボリューム
	void setCategoryVolume(audio::SoundCategory cat, float volume)
	{
		m_mixer->setCategoryVolume(cat, volume);
	}

	/// @brief マスターボリューム
	void setMasterVolume(float volume)
	{
		m_mixer->setMasterVolume(volume);
	}

	/// @brief 現在の BGM key (空 = 再生中なし)
	[[nodiscard]] std::string currentBgmKey() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_currentBgmKey;
	}

	/// @brief AudioMixer への直アクセス (診断用)
	[[nodiscard]] audio::AudioMixer& mixer() noexcept { return *m_mixer; }

private:
	audio::AudioMixer* m_mixer = nullptr;     ///< 非所有
	mutable std::mutex m_mutex;
	std::string m_currentBgmKey;              ///< 現 BGM key (空 = 無音)
};

// ── Payload parser helpers ──────────────────────────────────────────

/// @brief "key|value" を分割する
inline std::pair<std::string_view, std::string_view> splitPipe(std::string_view s)
{
	const auto pos = s.find('|');
	if (pos == std::string_view::npos) return {s, std::string_view{}};
	return {s.substr(0, pos), s.substr(pos + 1)};
}

/// @brief 文字列を float に変換 (from_chars / 失敗時 default)
inline float parseFloatOr(std::string_view s, float defaultValue)
{
	float out = defaultValue;
	auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
	(void)ptr;
	return (ec == std::errc{}) ? out : defaultValue;
}

/// @brief カテゴリ名文字列を `SoundCategory` に変換
inline audio::SoundCategory parseCategory(std::string_view s)
{
	if (s == "bgm" || s == "Bgm" || s == "BGM") return audio::SoundCategory::Bgm;
	if (s == "voice" || s == "Voice") return audio::SoundCategory::Voice;
	return audio::SoundCategory::Se;
}

// ── Bridge binding ──────────────────────────────────────────────────

/// @brief handler 登録関数のシグネチャ
/// @details `MitiruCefContext::registerHandler()` と互換。StateStore と同じ
///          pattern。テスト時はモック関数を渡す。
using RegisterHandlerFn = std::function<void(
	std::string /*name*/,
	std::function<std::string(std::string_view /*payload*/)> /*fn*/)>;

/// @brief AudioMixer を CEF handler として expose する
/// @details 登録される handler 名は上記 Doxygen 参照。state は登録される
///          handler closure が shared_ptr で保持するため呼び出し側で管理不要。
///
/// **実 CEF 呼び出し例:**
/// ```cpp
///   mitiru::audio::AudioMixer mixer;
///   auto* ctx = engine.cefContext();
///   mitiru::cef::bindAudioBridge(
///       [ctx](auto name, auto fn) {
///           ctx->registerHandler(std::move(name), std::move(fn));
///       },
///       mixer);
/// ```
inline std::shared_ptr<AudioBridgeState> bindAudioBridge(
	RegisterHandlerFn registerHandler, audio::AudioMixer& mixer)
{
	auto state = std::make_shared<AudioBridgeState>(mixer);

	registerHandler("audio.playBgm",
		[state](std::string_view payload) -> std::string {
			state->playBgm(payload);
			return "{}";
		});

	registerHandler("audio.stopBgm",
		[state](std::string_view /*payload*/) -> std::string {
			state->stopBgm();
			return "{}";
		});

	registerHandler("audio.crossFadeBgm",
		[state](std::string_view payload) -> std::string {
			// "BGM_KEY|duration_ms"
			const auto [key, durStr] = splitPipe(payload);
			const float ms = parseFloatOr(durStr, 500.0f);
			state->crossFadeBgm(key, ms / 1000.0f);
			return "{}";
		});

	registerHandler("audio.playSe",
		[state](std::string_view payload) -> std::string {
			state->playSe(payload);
			return "{}";
		});

	registerHandler("audio.setCategoryVolume",
		[state](std::string_view payload) -> std::string {
			// "bgm|0.8"
			const auto [catStr, volStr] = splitPipe(payload);
			const float vol = parseFloatOr(volStr, 1.0f);
			state->setCategoryVolume(parseCategory(catStr), vol);
			return "{}";
		});

	registerHandler("audio.setMasterVolume",
		[state](std::string_view payload) -> std::string {
			const float vol = parseFloatOr(payload, 1.0f);
			state->setMasterVolume(vol);
			return "{}";
		});

	registerHandler("audio.currentBgm",
		[state](std::string_view /*payload*/) -> std::string {
			const auto key = state->currentBgmKey();
			// JSON 戻り値。key には " が含まれない想定 (BGM キーは英数字)
			return std::string("{\"key\":\"") + key + "\"}";
		});

	return state;
}

} // namespace mitiru::cef
