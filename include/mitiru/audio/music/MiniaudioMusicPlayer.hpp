#pragma once

/// @file MiniaudioMusicPlayer.hpp
/// @brief MusicDirector と MusicRenderer を miniaudio の engine に 1 本の音としてつなぐ
/// @details 音源ファイルはエンコードされたままメモリへ読み込む。区間を鳴らすたびに、命令を出すスレッドで ma_decoder を開いて読み手として命令に詰める。音声スレッドではファイルを開かず、メモリも確保しない。
///          鳴り終わった instance の読み手は step() で回収する。director は「render が読んだ位置 + lookahead」まで進める。デバイス使用時は音の時計、
///          デバイスを開かない書き出し時は固定ステップに従う。書き出し時の lookahead を 1 ステップ分にすると、n ステップ目の要求は何度実行しても同じ小節で鳴る。

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/audio/music/MusicDirector.hpp>
#include <mitiru/audio/music/MusicRenderer.hpp>
#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru::audio::music
{

/// @brief エンコードされたファイル (wav / ogg / mp3 / flac) をメモリから読む IStemReader
class DecoderStemReader final : public IStemReader
{
public:
	DecoderStemReader() = default;
	~DecoderStemReader() override { close(); }
	DecoderStemReader(const DecoderStemReader&) = delete;
	DecoderStemReader& operator=(const DecoderStemReader&) = delete;

	bool open(const std::vector<std::uint8_t>& bytes, ma_uint32 channels, ma_uint32 sampleRate)
	{
		close();
		const ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, channels, sampleRate);
		m_open = ma_decoder_init_memory(bytes.data(), bytes.size(), &cfg, &m_decoder) == MA_SUCCESS;
		return m_open;
	}

	void close()
	{
		if (m_open) { ma_decoder_uninit(&m_decoder); }
		m_open = false;
	}

	std::uint64_t read(float* out, std::uint64_t frames) override
	{
		ma_uint64 got = 0;
		ma_decoder_read_pcm_frames(&m_decoder, out, frames, &got);
		return got;
	}

private:
	ma_decoder m_decoder{};
	bool       m_open = false;
};

class MiniaudioMusicPlayer
{
public:
	/// @brief 拡張子を除く assets/audio からの相対ファイル名を受け取り、バイト列を返す。ファイルがなければ空を返す
	using LoadFile = std::function<std::vector<std::uint8_t>(std::string_view file)>;

	MiniaudioMusicPlayer() = default;
	~MiniaudioMusicPlayer() { unload(); }
	MiniaudioMusicPlayer(const MiniaudioMusicPlayer&) = delete;
	MiniaudioMusicPlayer& operator=(const MiniaudioMusicPlayer&) = delete;

	/// @brief 曲の構成と音源を読み込み、無音のまま再生を始める。失敗時は理由、成功時は空を返す
	/// @param lookaheadFrames director を render より先に進める量。書き出し時は 1 ステップのフレーム数
	std::string load(ma_engine& engine, std::shared_ptr<const MusicManifest> manifest, const LoadFile& loadFile,
	                 std::int64_t lookaheadFrames)
	{
		unload();
		const ma_uint32 channels = ma_engine_get_channels(&engine);
		const ma_uint32 rate = ma_engine_get_sample_rate(&engine);
		if (std::string err = loadFiles(*manifest, loadFile); !err.empty()) { return err; }
		m_channels = channels;
		m_rate = rate;
		m_lookahead = lookaheadFrames;
		m_director = std::make_unique<MusicDirector>(std::move(manifest), rate);
		m_renderer = std::make_unique<MusicRenderer>(channels);
		m_pool.resize(MusicRenderer::kMaxInstances * kMaxStemsPerInstance);
		for (auto& r : m_pool) { r = std::make_unique<DecoderStemReader>(); }
		m_free.clear();
		for (std::size_t i = m_pool.size(); i-- > 0;) { m_free.push_back(static_cast<std::uint16_t>(i)); }
		m_live.reserve(MusicRenderer::kMaxInstances * 2);
		return startSound(engine);
	}

	void unload()
	{
		if (m_soundReady)
		{
			ma_sound_stop(&m_sound);
			ma_sound_uninit(&m_sound);  // ここから先、音声スレッドは renderer を読まない
			ma_data_source_uninit(&m_source.base);
			m_soundReady = false;
		}
		m_live.clear();
		m_pool.clear();
		m_renderer.reset();
		m_director.reset();
		m_files.clear();
		m_fileNames.clear();
	}

	[[nodiscard]] bool loaded() const noexcept { return m_soundReady; }
	/// @brief 曲の出力。BGM のバス (low-pass) へつなぎ替えるために使う
	[[nodiscard]] ma_sound* sound() noexcept { return m_soundReady ? &m_sound : nullptr; }
	[[nodiscard]] MusicDirector& director() noexcept { return *m_director; }
	[[nodiscard]] const MusicRenderer& renderer() const noexcept { return *m_renderer; }

	/// @brief director が出した命令を 1 件ずつ受け取る。書き出しツールの記録に使い、空の関数を渡すと外れる
	void setCommandLog(std::function<void(const MusicCommand&)> log) { m_log = std::move(log); }

	/// @brief 1 ステップ分の後始末と命令出し。director の request など、すべての要求を済ませた後に呼ぶ
	void step()
	{
		if (!m_soundReady) { return; }
		reclaim();
		const std::int64_t target = m_renderer->position() + m_lookahead;
		if (target <= m_director->now()) { return; }
		m_commands.clear();
		m_director->advance(static_cast<std::uint64_t>(target - m_director->now()), m_commands);
		if (m_commands.overflowed) { warnOnce("audio.music.overflow", "1 回に出す音楽の命令が多すぎて、一部を捨てました。"); }
		for (const MusicCommand& c : m_commands) { send(c); }
	}

private:
	struct Source
	{
		ma_data_source_base base{};
		MusicRenderer*      renderer = nullptr;
		ma_uint32           channels = 2;
		ma_uint32           rate = 48000;
	};
	struct Live
	{
		std::uint32_t id = 0;
		std::uint8_t  count = 0;
		std::array<std::uint16_t, kMaxStemsPerInstance> readers{};
	};

	static void warnOnce(const char* key, const char* what)
	{
		mitiru::debug::warnOnce(key, std::string(what) + "music.json で区間とスティンガーが重なって鳴る数を減らしてください。");
	}

	std::string loadFiles(const MusicManifest& m, const LoadFile& loadFile)
	{
		m_segmentFiles.assign(m.segments.size(), {});
		m_stingerFiles.assign(m.stingers.size(), 0);
		for (std::size_t s = 0; s < m.segments.size(); ++s)
		{
			if (m.segments[s].stems.size() > kMaxStemsPerInstance)
			{
				return "music.json: segments." + m.segments[s].id + " の stems は " + std::to_string(kMaxStemsPerInstance) + " 本まで";
			}
			for (const auto& stem : m.segments[s].stems)
			{
				const int idx = fileIndex(stem.file, loadFile);
				if (idx < 0) { return "music.json: 音源 \"" + stem.file + "\" が assets/audio に無い"; }
				m_segmentFiles[s].push_back(idx);
			}
		}
		for (std::size_t s = 0; s < m.stingers.size(); ++s)
		{
			const int idx = fileIndex(m.stingers[s].file, loadFile);
			if (idx < 0) { return "music.json: 音源 \"" + m.stingers[s].file + "\" が assets/audio に無い"; }
			m_stingerFiles[s] = idx;
		}
		return {};
	}

	int fileIndex(const std::string& file, const LoadFile& loadFile)
	{
		for (std::size_t i = 0; i < m_fileNames.size(); ++i) { if (m_fileNames[i] == file) { return static_cast<int>(i); } }
		std::vector<std::uint8_t> bytes = loadFile(file);
		if (bytes.empty()) { return -1; }
		m_fileNames.push_back(file);
		m_files.push_back(std::move(bytes));
		return static_cast<int>(m_files.size() - 1);
	}

	std::string startSound(ma_engine& engine)
	{
		static const ma_data_source_vtable vtable{&onRead, &onSeek, &onGetDataFormat, nullptr, nullptr, nullptr, 0};
		ma_data_source_config cfg = ma_data_source_config_init();
		cfg.vtable = &vtable;
		if (ma_data_source_init(&cfg, &m_source.base) != MA_SUCCESS) { return "音楽の data source を作れない"; }
		m_source.renderer = m_renderer.get();
		m_source.channels = m_channels;
		m_source.rate = m_rate;
		const ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
		if (ma_sound_init_from_data_source(&engine, &m_source.base, flags, nullptr, &m_sound) != MA_SUCCESS)
		{
			ma_data_source_uninit(&m_source.base);
			return "音楽の ma_sound を作れない";
		}
		m_soundReady = true;
		ma_sound_start(&m_sound);
		return {};
	}

	static ma_result onRead(ma_data_source* ds, void* out, ma_uint64 frames, ma_uint64* read)
	{
		auto* s = static_cast<Source*>(ds);
		s->renderer->render(static_cast<float*>(out), frames);
		if (read != nullptr) { *read = frames; }
		return MA_SUCCESS;
	}
	static ma_result onSeek(ma_data_source*, ma_uint64) { return MA_NOT_IMPLEMENTED; }
	static ma_result onGetDataFormat(ma_data_source* ds, ma_format* format, ma_uint32* channels, ma_uint32* rate,
	                                 ma_channel* map, size_t cap)
	{
		const auto* s = static_cast<const Source*>(ds);
		if (format != nullptr)   { format[0] = ma_format_f32; }
		if (channels != nullptr) { channels[0] = s->channels; }
		if (rate != nullptr)     { rate[0] = s->rate; }
		if (map != nullptr)      { ma_channel_map_init_standard(ma_standard_channel_map_default, map, cap, s->channels); }
		return MA_SUCCESS;
	}

	void send(MusicCommand c)
	{
		if (m_log) { m_log(c); }
		if (c.kind == MusicCommandKind::StartInstance) { attach(c); }
		if (!m_renderer->push(c))
		{
			warnOnce("audio.music.queue", "音楽の命令がたまりすぎて、一部を捨てました。");
			if (c.kind == MusicCommandKind::StartInstance) { release(c.instance); }
		}
	}

	void attach(MusicCommand& c)
	{
		const MusicManifest& m = m_director->manifest();
		Live live;
		live.id = c.instance;
		auto add = [&](int file, int layer) {
			if (m_free.empty()) { warnOnce("audio.music.readers", "同時に鳴らせる音源の数を超えたので、鳴らせない音源がありました。"); return; }
			const std::uint16_t r = m_free.back();
			if (!m_pool[r]->open(m_files[static_cast<std::size_t>(file)], m_channels, m_rate)) { return; }
			m_free.pop_back();
			c.stems[live.count] = MusicStemVoice{m_pool[r].get(), static_cast<std::int16_t>(layer), 1.0f};
			live.readers[live.count++] = r;
		};
		if (c.segment >= 0)
		{
			const auto& seg = m.segments[static_cast<std::size_t>(c.segment)];
			for (std::size_t i = 0; i < seg.stems.size(); ++i)
			{
				add(m_segmentFiles[static_cast<std::size_t>(c.segment)][i], seg.stems[i].layer);
			}
		}
		else if (c.stinger >= 0)
		{
			add(m_stingerFiles[static_cast<std::size_t>(c.stinger)], -1);
		}
		c.stemCount = live.count;
		m_live.push_back(live);
	}

	void reclaim()
	{
		std::uint32_t ids[32];
		for (std::size_t n = m_renderer->popFinished(ids, 32); n > 0; n = m_renderer->popFinished(ids, 32))
		{
			for (std::size_t i = 0; i < n; ++i) { release(ids[i]); }
		}
	}

	void release(std::uint32_t id)
	{
		for (std::size_t i = 0; i < m_live.size(); ++i)
		{
			if (m_live[i].id != id) { continue; }
			for (std::uint8_t k = 0; k < m_live[i].count; ++k)
			{
				m_pool[m_live[i].readers[k]]->close();
				m_free.push_back(m_live[i].readers[k]);
			}
			m_live[i] = m_live.back();
			m_live.pop_back();
			return;
		}
	}

	ma_uint32                                        m_channels = 2;
	ma_uint32                                        m_rate = 48000;
	std::int64_t                                     m_lookahead = 0;
	std::unique_ptr<MusicDirector>                   m_director;
	std::unique_ptr<MusicRenderer>                   m_renderer;
	MusicCommandBuffer                               m_commands;
	std::function<void(const MusicCommand&)>         m_log;
	std::vector<std::string>                         m_fileNames;
	std::vector<std::vector<std::uint8_t>>           m_files;
	std::vector<std::vector<int>>                    m_segmentFiles;
	std::vector<int>                                 m_stingerFiles;
	std::vector<std::unique_ptr<DecoderStemReader>>  m_pool;
	std::vector<std::uint16_t>                       m_free;
	std::vector<Live>                                m_live;
	Source                                           m_source;
	ma_sound                                         m_sound{};
	bool                                             m_soundReady = false;
};

}  // namespace mitiru::audio::music
