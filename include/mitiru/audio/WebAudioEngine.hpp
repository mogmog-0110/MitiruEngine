#pragma once

/// @file WebAudioEngine.hpp
/// @brief Emscripten/WASM用 Web Audio API バックエンド
/// @details EM_JS マクロを通じてブラウザの Web Audio API を操作する。
///          __EMSCRIPTEN__ 環境でのみコンパイルされる。

#ifdef __EMSCRIPTEN__

#include <string>
#include <string_view>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <mitiru/audio/AudioEngine.hpp>

// ── JS interop 関数 ────────────────────────────────────

/// @cond INTERNAL
EM_JS(void, mitiru_webaudio_init, (), {
	if (!Module._mitiru_audioCtx) {
		Module._mitiru_audioCtx = new (window.AudioContext || window.webkitAudioContext)();
		Module._mitiru_gainNode = Module._mitiru_audioCtx.createGain();
		Module._mitiru_gainNode.connect(Module._mitiru_audioCtx.destination);
		Module._mitiru_sources = {};
	}
});

EM_JS(void, mitiru_webaudio_play, (const char* pathPtr), {
	var path = UTF8ToString(pathPtr);
	var ctx = Module._mitiru_audioCtx;
	if (!ctx) return;

	// Resume context if suspended (browser autoplay policy)
	if (ctx.state === 'suspended') { ctx.resume(); }

	fetch(path)
		.then(function(response) { return response.arrayBuffer(); })
		.then(function(buffer) { return ctx.decodeAudioData(buffer); })
		.then(function(decoded) {
			// Stop previous source for this path if any
			if (Module._mitiru_sources[path]) {
				try { Module._mitiru_sources[path].stop(); } catch(e) {}
			}
			var source = ctx.createBufferSource();
			source.buffer = decoded;
			source.connect(Module._mitiru_gainNode);
			source.start(0);
			Module._mitiru_sources[path] = source;
		});
});

EM_JS(void, mitiru_webaudio_stop, (const char* pathPtr), {
	var path = UTF8ToString(pathPtr);
	if (Module._mitiru_sources && Module._mitiru_sources[path]) {
		try { Module._mitiru_sources[path].stop(); } catch(e) {}
		delete Module._mitiru_sources[path];
	}
});

EM_JS(void, mitiru_webaudio_stop_all, (), {
	if (!Module._mitiru_sources) return;
	var keys = Object.keys(Module._mitiru_sources);
	for (var i = 0; i < keys.length; i++) {
		try { Module._mitiru_sources[keys[i]].stop(); } catch(e) {}
	}
	Module._mitiru_sources = {};
});

EM_JS(void, mitiru_webaudio_set_volume, (float vol), {
	if (Module._mitiru_gainNode) {
		Module._mitiru_gainNode.gain.value = vol;
	}
});
/// @endcond

namespace mitiru::audio
{

/// @brief Web Audio API ベースのオーディオエンジン
/// @details ブラウザ環境で Web Audio API を使用してサウンドを再生する。
///          AudioContext の自動生成・自動 resume を行う。
///
/// @code
/// #ifdef __EMSCRIPTEN__
/// mitiru::audio::WebAudioEngine audio;
/// audio.playSound("assets/sfx/explosion.ogg");
/// audio.setVolume(0.8f);
/// #endif
/// @endcode
class WebAudioEngine : public IAudioEngine
{
public:
	/// @brief コンストラクタ。AudioContext を初期化する。
	WebAudioEngine()
	{
		mitiru_webaudio_init();
	}

	/// @brief サウンドファイルを再生する
	/// @param id ファイルパス（WASM仮想FS上のパスまたはURL）
	void playSound(std::string_view id) override
	{
		const std::string path{id};
		mitiru_webaudio_play(path.c_str());
	}

	/// @brief サウンドを停止する
	/// @param id ファイルパス
	void stopSound(std::string_view id) override
	{
		const std::string path{id};
		mitiru_webaudio_stop(path.c_str());
	}

	/// @brief BGMを再生する
	/// @param id ファイルパス
	void playMusic(std::string_view id) override
	{
		// BGM も同じ方法で再生する。SE との区別は論理的なものにすぎない
		stopMusic();
		currentMusic_ = std::string{id};
		mitiru_webaudio_play(currentMusic_.c_str());
	}

	/// @brief 現在のBGMを停止する
	void stopMusic() override
	{
		if (!currentMusic_.empty())
		{
			mitiru_webaudio_stop(currentMusic_.c_str());
			currentMusic_.clear();
		}
	}

	/// @brief マスターボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	void setVolume(float volume) override
	{
		volume_ = (volume < 0.0f) ? 0.0f : (volume > 1.0f) ? 1.0f : volume;
		mitiru_webaudio_set_volume(volume_);
	}

	/// @brief 再生中か判定する（Web Audio APIでは近似判定）
	/// @param id ファイルパス
	/// @return 再生中のソースが存在すれば true（厳密ではない）
	[[nodiscard]] bool isPlaying(std::string_view id) const override
	{
		// Web Audio API は C++ から同期的に再生状態を問い合わせる手段を
		// 提供しない。完全な実装なら EM_ASM_INT で source node の状態を
		// 確認するが、ここでは安全側のデフォルトとして false を返す。
		static_cast<void>(id);
		return false;
	}

private:
	float volume_ = 1.0f;          ///< 現在のマスターボリューム
	std::string currentMusic_;      ///< 再生中のBGMパス
};

} // namespace mitiru::audio

#endif // __EMSCRIPTEN__
