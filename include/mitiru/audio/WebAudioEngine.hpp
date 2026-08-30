#pragma once

/// @file WebAudioEngine.hpp
/// @brief Emscripten/WASM 用の Web Audio API バックエンド。
/// @details __EMSCRIPTEN__ 環境でのみコンパイルされる。
///
///          音の指定はデスクトップの FileAudioEngine と同じ「id」。
///          `<baseDir>/<id>.wav|.ogg|.mp3` を順に探し、最初に見つかった 1 本を鳴らす。
///          バイト列は vfs から読む。web の素材は preload package として wasm の
///          FS に入っていて URL では引けないので、fetch ではなくメモリから
///          decodeAudioData へ渡す。復号した AudioBuffer は id 単位で持ち回る。

#ifdef __EMSCRIPTEN__

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <string>
#include <string_view>
#include <unordered_set>

#include <emscripten.h>

#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/asset/AssetPack.hpp>

// ── JS 側との受け渡し ────────────────────────────────────────────────
/// @cond INTERNAL
EM_JS(void, mitiru_webaudio_init, (), {
	if (Module._mitiru_audio) { return; }
	var Ctx = window.AudioContext || window.webkitAudioContext;
	if (!Ctx) { return; }
	var ctx = new Ctx();
	var master = ctx.createGain();
	master.connect(ctx.destination);
	Module._mitiru_audio = { ctx: ctx, master: master, buffers: {}, playing: {}, pending: {}, music: null };

	// 自動再生の制限は操作で解ける。最初の操作を拾って resume する。
	var wake = function () { if (ctx.state === 'suspended') { ctx.resume(); } };
	['pointerdown', 'mousedown', 'touchend', 'keydown'].forEach(function (t) {
		document.addEventListener(t, wake, true);
	});
});

EM_JS(void, mitiru_webaudio_decode, (const char* idPtr, const std::uint8_t* data, int len), {
	var a = Module._mitiru_audio;
	if (!a) { return; }
	var id = UTF8ToString(idPtr);
	if (a.buffers[id] !== undefined) { return; }
	a.buffers[id] = null;
	var bytes = HEAPU8.slice(data, data + len);
	a.ctx.decodeAudioData(bytes.buffer,
		function (buf) {
			a.buffers[id] = buf;
			// 復号を待っている間に来ていた要求を、ここで 1 回だけ鳴らす。
			var q = a.pending[id];
			if (q) { delete a.pending[id]; mitiru_webaudio_play_js(id, q.v, q.p, q.l, q.m, q.a); }
		},
		function () { delete a.buffers[id]; delete a.pending[id]; });
});

EM_JS(int, mitiru_webaudio_ready, (const char* idPtr), {
	var a = Module._mitiru_audio;
	return (a && a.buffers[UTF8ToString(idPtr)]) ? 1 : 0;
});

EM_JS(void, mitiru_webaudio_play, (const char* idPtr, float vol, float pitch, int loop,
                                   int music, double at), {
	if (!Module._mitiru_audio) { return; }
	mitiru_webaudio_play_js(UTF8ToString(idPtr), vol, pitch, loop, music, at);
});

/// AudioContext が申告する出力遅延 (秒)。耳の位置は masterTimeSec から これを引いた点。
EM_JS(double, mitiru_webaudio_latency, (), {
	var a = Module._mitiru_audio;
	if (!a) { return 0; }
	return (a.ctx.outputLatency || a.ctx.baseLatency || 0);
});

/// 復号済みなら鳴らし、まだなら要求を覚えておく。EM_JS の外にも置くのは、
/// decodeAudioData の完了からも同じ経路で鳴らすため。
EM_JS(void, mitiru_webaudio_define_play, (), {
	if (window.mitiru_webaudio_play_js) { return; }
	window.mitiru_webaudio_play_js = function (id, vol, pitch, loop, music, at) {
	var a = Module._mitiru_audio;
	if (!a) { return; }
	var buf = a.buffers[id];
	if (!buf) {
		// 復号中。最後の要求だけ覚えて、終わり次第 1 回鳴らす。
		if (a.buffers[id] === null) { a.pending[id] = {v: vol, p: pitch, l: loop, m: music, a: at}; }
		return;
	}
	if (a.ctx.state === 'suspended') { a.ctx.resume(); }

	// ループと BGM は「鳴っている最中の呼び直し = 音量とピッチの変更」。
	// 鳴らし直すと歌や伴奏が頭へ戻る。ゲーム側はこの約束で音量調整を書いている。
	if (loop || music) {
		var cur = music ? a.music : a.playing[id];
		if (cur) {
			cur.gain.gain.value = vol;
			cur.src.playbackRate.value = pitch;
			return;
		}
	}

	var src = a.ctx.createBufferSource();
	src.buffer = buf;
	src.loop = !!loop;
	src.playbackRate.value = pitch;
	var g = a.ctx.createGain();
	g.gain.value = vol;
	src.connect(g);
	g.connect(a.master);
	// at は AudioContext の時計上の絶対時刻。0 なら即時。過ぎた時刻はその場で鳴らす
	// (遅れて鳴らすより、拍の手がかりとして正しい)。
	src.start((at && at > a.ctx.currentTime) ? at : 0);

	// 覚えるのはループと BGM だけ。一度きりの効果音まで覚えると、連打したときに
	// 前の音を止めてしまう。
	if (loop || music) {
		var entry = { src: src, gain: g };
		if (music) { a.music = entry; } else { a.playing[id] = entry; }
		src.onended = function () {
			if (music) { if (a.music === entry) { a.music = null; } }
			else if (a.playing[id] === entry) { delete a.playing[id]; }
		};
	}
	};
});

EM_JS(void, mitiru_webaudio_stop, (const char* idPtr), {
	var a = Module._mitiru_audio;
	if (!a) { return; }
	var id = UTF8ToString(idPtr);
	if (a.playing[id]) {
		try { a.playing[id].src.stop(); } catch (e) {}
		delete a.playing[id];
	}
});

EM_JS(void, mitiru_webaudio_stop_music, (), {
	var a = Module._mitiru_audio;
	if (!a || !a.music) { return; }
	try { a.music.src.stop(); } catch (e) {}
	a.music = null;
});

EM_JS(void, mitiru_webaudio_set_volume, (float vol), {
	var a = Module._mitiru_audio;
	if (a) { a.master.gain.value = vol; }
});

EM_JS(double, mitiru_webaudio_time, (), {
	var a = Module._mitiru_audio;
	return a ? a.ctx.currentTime : 0;
});
/// @endcond

namespace mitiru::audio
{

/// @brief Web Audio API で鳴らすオーディオエンジン。
/// @details id の解決規約はデスクトップの FileAudioEngine と同じ。
class WebAudioEngine final : public IAudioEngine
{
public:
	/// @param baseDir 音の置き場。例 "oscars_gardening/assets/audio"。
	explicit WebAudioEngine(std::string baseDir)
		: m_baseDir(std::move(baseDir))
	{
		while (!m_baseDir.empty() && m_baseDir.back() == '/') { m_baseDir.pop_back(); }
		mitiru_webaudio_init();
		mitiru_webaudio_define_play();
		primeAll();
	}

	void playSound(std::string_view id) override { fire(id, 1.0f, 1.0f, false, false); }
	void playSound(std::string_view id, float vol) override { fire(id, vol, 1.0f, false, false); }
	void playSoundEx(std::string_view id, float vol, float pitch, float) override
	{
		fire(id, vol, pitch, false, false);
	}
	void playSoundLoop(std::string_view id, float vol, float pitch, float) override
	{
		fire(id, vol, pitch, true, false);
	}
	void stopSound(std::string_view id) override
	{
		const std::string key{id};
		mitiru_webaudio_stop(key.c_str());
	}
	void stopSoundFade(std::string_view id, float) override { stopSound(id); }

	void playMusic(std::string_view id) override { fire(id, 1.0f, 1.0f, true, true); }
	void playMusic(std::string_view id, float vol, bool loop) override
	{
		fire(id, vol, 1.0f, loop, true);
	}
	void playMusicEx(std::string_view id, float vol, bool loop, float) override
	{
		fire(id, vol, 1.0f, loop, true);
	}
	void stopMusic() override { mitiru_webaudio_stop_music(); }
	void stopMusicFade(float) override { stopMusic(); }

	void setVolume(float volume) override
	{
		const float v = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
		mitiru_webaudio_set_volume(v);
	}

	/// @details Web Audio は C++ から同期に状態を問えないので安全側に倒す。
	[[nodiscard]] bool isPlaying(std::string_view) const override { return false; }

	/// @details AudioContext の時計。ゲームが音を基準に判定するときの基準になる。
	[[nodiscard]] double masterTimeSec() const noexcept override
	{
		return mitiru_webaudio_time();
	}

	/// @details 譜面で鳴る時刻が決まっている音は、フレームではなく音の時計で撃つ。
	///          atSec は masterTimeSec と同じ AudioContext の絶対時刻なので、
	///          そのまま BufferSource.start へ渡せばサンプル精度で発火する。
	///          ここを既定 (時刻を捨てて即時再生) のままにすると、先読みぶん
	///          最大 0.6 秒の前倒しで鳴り、拍が崩れる。
	void playSoundScheduled(std::string_view id, double atSec, float volume,
	                        float pitchScale) override
	{
		fire(id, volume, pitchScale, false, false, atSec);
	}

	[[nodiscard]] double outputLatencySec() const noexcept override
	{
		return mitiru_webaudio_latency();
	}

private:
	/// @details 起動時に音を全部読んで復号を始める。復号は非同期なので、鳴らす
	///          瞬間に初めて頼むと最初の 1 回が落ちる。拍に合わせて鳴らすゲームでは
	///          その 1 回が致命的なので、先に全部渡しておく。
	void primeAll()
	{
		std::error_code ec;
		for (const auto& e : std::filesystem::directory_iterator(m_baseDir, ec))
		{
			if (ec) { break; }
			if (!e.is_regular_file()) { continue; }
			const auto ext = e.path().extension().string();
			if (ext != ".wav" && ext != ".ogg" && ext != ".mp3") { continue; }
			prime(e.path().stem().string());
		}
	}

	/// @details 復号は非同期なので、初めて使う音は頼んだそのフレームでは鳴らない。
	///          鳴らす前に必ず prime を通しておき、2 回目以降はキャッシュから出す。
	void fire(std::string_view id, float vol, float pitch, bool loop, bool music,
	          double atSec = 0.0)
	{
		const std::string key{id};
		if (!prime(key)) { return; }
		mitiru_webaudio_play(key.c_str(), vol, pitch, loop ? 1 : 0, music ? 1 : 0, atSec);
	}

	/// @details 見つからない id を毎フレーム探し直さないよう、結果に関わらず記録する。
	bool prime(const std::string& id)
	{
		if (m_requested.count(id) != 0) { return true; }
		for (const char* ext : {".wav", ".ogg", ".mp3"})
		{
			const auto bytes = mitiru::vfs::readGlobal(m_baseDir + "/" + id + ext);
			if (!bytes || bytes->empty()) { continue; }
			m_requested.insert(id);
			mitiru_webaudio_decode(id.c_str(), bytes->data(), static_cast<int>(bytes->size()));
			return true;
		}
		m_requested.insert(id);
		std::fprintf(stderr, "[mitiru] sound id not found under %s: %s\n",
		             m_baseDir.c_str(), id.c_str());
		return false;
	}

	std::string m_baseDir;
	std::unordered_set<std::string> m_requested;
};

} // namespace mitiru::audio

#endif // __EMSCRIPTEN__
