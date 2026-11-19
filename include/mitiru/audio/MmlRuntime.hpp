#pragma once

/// @file MmlRuntime.hpp
/// @brief 実行中のゲームが頼んだ MML (`hud.mml("T120 O5 CDEFG")`) を、OPNA で PCM にして鳴らす (ABI v64、ADR 0164)。
/// @details 音はゲームの状態に入らない (docs/AUDIO.md)。PCM を作る処理はゲームの thread の外 (ワーカー 1 本) で行い、作り終えた次の poll で鳴らす。
///          同じ文字列の WAV は覚えておくので、2 度目からは作り直さない。最初の 1 回だけ、作る時間 (長さしだい) だけ遅れて鳴る。
///          文字列の読み方はベイク (audio/mml/MmlBake.hpp) と共通の audio/mml/MmlSong.hpp の Inline で、1 行 (または `;` の区切り) が 1 トラック。
///          トラックの中の `@FM0`〜`@FM11` / `@SSG` / `@RHYTHM` が音源で、書かなければ FM の 0 番。

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <mitiru/audio/mml/MmlSong.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/util/Hash.hpp>
#include <mitiru_mml/OpnaSequencer.hpp>
#include <mitiru_mml/WavWriter.hpp>

namespace mitiru::audio
{

namespace mml_runtime
{

inline constexpr double      kMaxSeconds     = 30.0;  // これより長い曲は鳴らさない (実行中に作るので。長い曲は build 時に WAV へ焼く)
inline constexpr std::size_t kMaxCacheItems  = 32;
inline constexpr std::size_t kMaxCacheBytes  = 24u * 1024u * 1024u;

}  // namespace mml_runtime

/// @brief MML の文字列を WAV (44.1 kHz、モノラル、16 bit) にする。誤りや空の音は空の配列を返し、error に理由を入れる
[[nodiscard]] inline std::vector<std::uint8_t> renderMmlWav(std::string_view text, std::string& error)
{
	const mml::ParsedSong parsed = mml::parseMml(text, mml::MmlDialect::Inline);
	if (!parsed.ok()) { error = parsed.error; return {}; }
	mitiru_mml::OpnaSequencer seq;
	mml::addTracks(seq, parsed);
	try
	{
		const mitiru_mml::PcmBuffer pcm = seq.render();
		if (pcm.empty()) { error = "音になりませんでした (音符が無いか、MML が読めません)"; return {}; }
		const double seconds = static_cast<double>(pcm.size()) / static_cast<double>(seq.sampleRate());
		if (seconds > mml_runtime::kMaxSeconds)
		{
			error = "30 秒を超える曲は実行中には作りません (build 時に assets/audio/<名前>.mml から WAV へ焼いてください)";
			return {};
		}
		return mitiru_mml::WavWriter::toWav(pcm, seq.sampleRate());
	}
	catch (const std::exception& e)
	{
		error = std::string("MML を読めません: ") + e.what();
		return {};
	}
}

/// @brief 鳴らす先 (host が MiniaudioEngine などに繋ぐ)。key は再生を見分ける番号
struct MmlSink
{
	std::function<void(const std::vector<std::uint8_t>& wav, float volume, bool loop, std::uint64_t key)> play;
	std::function<void(std::uint64_t key)> stop;            ///< key 0 = この MmlRuntime が鳴らした音を全部
	std::function<bool(std::uint64_t key)> isPlaying;
};

/// @brief MML の依頼を受け、PCM を別 thread で作り、鳴らす先へ渡す。request と poll は同じ thread (ゲームの thread) から呼ぶ
class MmlRuntime
{
public:
	/// @param synchronous true なら poll の中で作る (試験用。thread を持たない)
	explicit MmlRuntime(bool synchronous = false) : m_synchronous(synchronous)
	{
		if (!synchronous) { m_worker = std::thread([this] { workerLoop(); }); }
	}
	~MmlRuntime()
	{
		{
			std::lock_guard lock(m_mutex);
			m_quit = true;
		}
		m_wake.notify_all();
		if (m_worker.joinable()) { m_worker.join(); }
	}
	MmlRuntime(const MmlRuntime&) = delete;
	MmlRuntime& operator=(const MmlRuntime&) = delete;

	/// @brief 1 件の依頼を受ける (鳴らすのは次の poll)
	void request(const module::MmlIntent& intent)
	{
		if (intent.stop == 0 && intent.text[0] == '\0') { return; }
		Request r;
		r.text.assign(intent.text, strnlen(intent.text, sizeof(intent.text)));
		r.volume = intent.volume;
		r.handle = intent.handle;
		r.loop = intent.loop != 0;
		r.stop = intent.stop != 0;
		m_incoming.push_back(std::move(r));
	}

	/// @brief 毎フレーム 1 回。作り終えた PCM を受け取り、受けた依頼を鳴らす (または止める)
	void poll(const MmlSink& sink)
	{
		collectFinished();
		std::deque<Request> incoming;
		incoming.swap(m_incoming);
		for (Request& r : incoming) { handle(std::move(r), sink); }
		if (m_synchronous)
		{
			// 依頼の取り消し (stop) を先に済ませてから作る。作った物はこの poll の中で鳴る
			renderPendingInline();
			collectFinished();
		}
		playReady(sink);
	}

	[[nodiscard]] std::size_t cachedCount() const noexcept { return m_cache.size(); }
	[[nodiscard]] std::size_t renderingCount() const noexcept { return m_rendering.size(); }

	/// @brief 再生を見分ける key。handle があればそれ、無ければ文字列から決める (同じ文字列は同じ key)
	[[nodiscard]] static std::uint64_t keyOf(std::uint32_t handle, std::uint64_t textHash) noexcept
	{
		return handle != 0 ? (1ull << 63) | handle : (textHash & ~(1ull << 63)) | 1ull;
	}

private:
	struct Request
	{
		std::string   text;
		float         volume = 1.0f;
		std::uint32_t handle = 0;
		bool          loop = false;
		bool          stop = false;
	};
	struct Job { std::uint64_t hash; std::string text; };
	struct Done { std::uint64_t hash; std::shared_ptr<const std::vector<std::uint8_t>> wav; std::string error; std::string text; };

	static std::uint64_t hashOf(const std::string& s) noexcept { return util::Hash::xxhash(s.data(), s.size()); }

	void handle(Request r, const MmlSink& sink)
	{
		if (r.stop)
		{
			const std::uint64_t key = r.handle != 0 ? keyOf(r.handle, 0) : 0;
			if (sink.stop) { sink.stop(key); }
			// まだ作っている最中の分は、作り終えても鳴らさない
			m_waiting.erase(std::remove_if(m_waiting.begin(), m_waiting.end(),
				[&](const Request& w) { return r.handle == 0 || w.handle == r.handle; }), m_waiting.end());
			return;
		}
		const std::uint64_t hash = hashOf(r.text);
		const std::uint64_t key = keyOf(r.handle, hash);
		// 鳴っている間の同じ依頼 (loop) は鳴らし直さない。毎フレーム呼ばれても 1 本のまま
		if (r.loop && sink.isPlaying && sink.isPlaying(key)) { return; }
		if (r.loop && std::any_of(m_waiting.begin(), m_waiting.end(), [&](const Request& w) { return w.loop && keyOf(w.handle, hashOf(w.text)) == key; })) { return; }
		if (m_cache.find(hash) == m_cache.end() && m_rendering.find(hash) == m_rendering.end()) { startRender(hash, r.text); }
		m_waiting.push_back(std::move(r));
	}

	void startRender(std::uint64_t hash, const std::string& text)
	{
		m_rendering.emplace(hash, text);
		if (m_synchronous) { m_inlineJobs.push_back(Job{hash, text}); return; }
		{
			std::lock_guard lock(m_mutex);
			m_jobs.push_back(Job{hash, text});
		}
		m_wake.notify_one();
	}

	void renderPendingInline()
	{
		for (Job& j : m_inlineJobs) { m_finished.push_back(render(j)); }
		m_inlineJobs.clear();
	}

	static Done render(const Job& j)
	{
		Done d{j.hash, nullptr, {}, j.text};
		std::string error;
		std::vector<std::uint8_t> wav = renderMmlWav(j.text, error);
		if (!wav.empty()) { d.wav = std::make_shared<const std::vector<std::uint8_t>>(std::move(wav)); }
		d.error = std::move(error);
		return d;
	}

	void workerLoop()
	{
		for (;;)
		{
			Job job;
			{
				std::unique_lock lock(m_mutex);
				m_wake.wait(lock, [this] { return m_quit || !m_jobs.empty(); });
				if (m_quit) { return; }
				job = std::move(m_jobs.front());
				m_jobs.pop_front();
			}
			Done d = render(job);
			std::lock_guard lock(m_mutex);
			m_results.push_back(std::move(d));
		}
	}

	void collectFinished()
	{
		std::vector<Done> done;
		{
			std::lock_guard lock(m_mutex);
			done.swap(m_results);
		}
		for (Done& d : m_finished) { done.push_back(std::move(d)); }
		m_finished.clear();
		for (Done& d : done)
		{
			m_rendering.erase(d.hash);
			if (!d.error.empty())
			{
				debug::warnOnce("hud.mml." + std::to_string(d.hash), "hud.mml(\"" + d.text.substr(0, 40) + "...\") を鳴らせません: " + d.error);
			}
			remember(d.hash, std::move(d.wav));
		}
	}

	// 失敗 (wav が null) も覚える。毎フレーム作り直さないため
	void remember(std::uint64_t hash, std::shared_ptr<const std::vector<std::uint8_t>> wav)
	{
		m_cacheBytes += wav ? wav->size() : 0;
		m_cache[hash] = std::move(wav);
		m_order.push_back(hash);
		while (m_order.size() > kKeepItems() || m_cacheBytes > mml_runtime::kMaxCacheBytes)
		{
			const std::uint64_t old = m_order.front();
			m_order.pop_front();
			if (old == hash) { break; }   // 入れたばかりの物は残す
			if (const auto it = m_cache.find(old); it != m_cache.end())
			{
				m_cacheBytes -= it->second ? it->second->size() : 0;
				m_cache.erase(it);
			}
		}
	}
	static constexpr std::size_t kKeepItems() noexcept { return mml_runtime::kMaxCacheItems; }

	void playReady(const MmlSink& sink)
	{
		for (auto it = m_waiting.begin(); it != m_waiting.end();)
		{
			const std::uint64_t hash = hashOf(it->text);
			const auto found = m_cache.find(hash);
			if (found == m_cache.end()) { ++it; continue; }   // まだ作っている
			if (found->second && sink.play) { sink.play(*found->second, it->volume, it->loop, keyOf(it->handle, hash)); }
			it = m_waiting.erase(it);
		}
	}

	const bool m_synchronous;
	std::deque<Request> m_incoming;   // ゲームの thread だけが触る
	std::vector<Request> m_waiting;   // PCM を待っている依頼
	std::unordered_map<std::uint64_t, std::string> m_rendering;
	std::unordered_map<std::uint64_t, std::shared_ptr<const std::vector<std::uint8_t>>> m_cache;
	std::deque<std::uint64_t> m_order;
	std::size_t m_cacheBytes = 0;
	std::vector<Job>  m_inlineJobs;
	std::vector<Done> m_finished;

	std::mutex m_mutex;                 // 以下はワーカーと共有
	std::condition_variable m_wake;
	std::deque<Job>   m_jobs;
	std::vector<Done> m_results;
	bool m_quit = false;
	std::thread m_worker;
};

}  // namespace mitiru::audio
