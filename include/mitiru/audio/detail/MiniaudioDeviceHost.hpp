#pragma once

/// @file MiniaudioDeviceHost.hpp
/// @brief 出力機器の開始、再選択、Null 機器への切り替えを管理する
/// @details ma_engine は機器を持たず、機器の data callback から読み出される。
/// engine の再生中の音とバスは、機器を入れ替えても消えない。
/// 機器の開始と列挙は別スレッドで行い、ゲームのスレッドは maintain() で開始時刻を確認する。
/// 機器は「新しい機器を作る、古い機器を止める、新しい機器を始める」の順で入れ替える。
/// engine のノードグラフは 1 スレッドからしか読めないため、2 つの data callback を同時に動かさない。

#include <mitiru/audio/detail/MiniaudioDeviceLatency.hpp>
#include <mitiru/audio/detail/MiniaudioInclude.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru::audio::detail
{

struct OutputDeviceInfo
{
	std::string name;
	bool        isDefault = false;
};

struct OutputDeviceStatus
{
	bool        onRealDevice = false;   ///< false は Null の機器 (音は出ないが時計は進む)
	bool        onPreferred = true;     ///< 選んだ機器 (未選択なら既定の機器) で鳴っている
	std::string activeName;
	ma_uint32   internalChannels = 0;    ///< OS 側の機器の本数と速さ。ミックスは 2ch・48 kHz のままで、miniaudio と OS が変換する
	ma_uint32   internalSampleRate = 0;
};

class MiniaudioDeviceHost
{
public:
	/// data callback の型。out は ma_format_f32 の interleaved で、frames 個分をすべて書く
	using DataFn = void (*)(void* user, float* out, ma_uint32 frames);

	struct Params
	{
		ma_uint32 sampleRate = 48000;
		ma_uint32 channels = 2;
		ma_uint32 periodMs = 4;
		bool      nullOnly = false;   ///< 実機を開かない (試験と --audio-capture の実時間)
		double    retrySec = 5.0;
	};

	MiniaudioDeviceHost() = default;
	MiniaudioDeviceHost(const MiniaudioDeviceHost&) = delete;
	MiniaudioDeviceHost& operator=(const MiniaudioDeviceHost&) = delete;
	~MiniaudioDeviceHost() { shutdown(); }

	/// @brief 最初の機器を呼んだスレッドで開く。Null 機器も開けなければ false
	bool start(const Params& params, DataFn fn, void* user, std::string preferred)
	{
		m_params = params;
		m_fn = fn;
		m_user = user;
		m_preferred = std::move(preferred);
		m_nextRetry = std::chrono::steady_clock::now();
		reopen();
		return active();
	}

	/// @brief 機器を止めて解放する。data callback が engine を読み続けるため、engine を壊す前に呼ぶ
	void shutdown()
	{
		m_closing.store(true);
		if (m_worker.joinable()) { m_worker.join(); }
		std::unique_ptr<ma_device> dev;
		{
			std::lock_guard lock(m_devMutex);
			dev = std::move(m_dev);
		}
		if (dev) { ma_device_uninit(dev.get()); }
		std::lock_guard lock(m_ctxMutex);
		if (m_ctxReady) { ma_context_uninit(&m_ctx); m_ctxReady = false; }
		if (m_nullCtxReady) { ma_context_uninit(&m_nullCtx); m_nullCtxReady = false; }
	}

	[[nodiscard]] bool active() const
	{
		std::lock_guard lock(m_devMutex);
		return m_dev != nullptr;
	}

	/// @brief ゲームのスレッドから毎フレーム呼ぶ。開き直しが必要なときだけ別スレッドを始める
	void maintain()
	{
		if (m_busy.load() || m_closing.load()) { return; }
		if (m_worker.joinable()) { m_worker.join(); }
		if (!due()) { return; }
		m_busy.store(true);
		m_worker = std::thread([this] { reopen(); m_busy.store(false); });
	}

	/// @brief 使う機器を名前で選ぶ。空なら既定の機器を使う。見つからなければ既定の機器に戻し、見つかるまで探し直す
	void setPreferred(std::string name)
	{
		{
			std::lock_guard lock(m_stateMutex);
			if (name == m_preferred) { return; }
			m_preferred = std::move(name);
		}
		m_reopenRequested.store(true);
	}

	[[nodiscard]] std::string preferred() const
	{
		std::lock_guard lock(m_stateMutex);
		return m_preferred;
	}

	/// @brief 直近に列挙した出力機器。requestRefresh() の後に maintain() が列挙し直す
	[[nodiscard]] std::vector<OutputDeviceInfo> devices() const
	{
		std::lock_guard lock(m_stateMutex);
		return m_devices;
	}

	void requestRefresh() noexcept { m_reopenRequested.store(true); }

	[[nodiscard]] OutputDeviceStatus status() const
	{
		std::lock_guard lock(m_stateMutex);
		return m_status;
	}

	/// @brief 機器の書き込みバッファと OS 側の遅れの合計秒数。機器の変更後は測り直し、測れなければ直近の値を返す
	[[nodiscard]] double latencySec()
	{
		std::unique_lock lock(m_devMutex, std::try_to_lock);
		if (!lock.owns_lock()) { return m_latency.load(); }   // 入れ替え中。止まらずに前の値で答える
		if (!m_dev) { return 0.0; }
		const auto now = std::chrono::steady_clock::now();
		// IAudioClient への問い合わせには約 0.3 ミリ秒かかり、値は機器を替えるまで変わらない
		if (!m_latencyDirty.exchange(false) && m_latencyAt.time_since_epoch().count() != 0
		    && now - m_latencyAt < std::chrono::milliseconds(500))
		{
			return m_latency.load();
		}
		const ma_uint32 rate = m_dev->playback.internalSampleRate;
		if (rate == 0) { return 0.0; }
		const double sec = static_cast<double>(deviceBufferFrames(m_dev.get())) / static_cast<double>(rate)
		                 + backendLatencySec(m_dev.get());
		m_latency.store(sec);
		m_latencyMeasures.fetch_add(1);
		m_latencyAt = now;
		return sec;
	}

	/// @brief stopAll() と resume() から機器を止めて戻す。停止中は機器が失われたとみなさない
	void pause()
	{
		m_userStopped.store(true);
		std::lock_guard lock(m_devMutex);
		if (m_dev) { ma_device_stop(m_dev.get()); }
	}

	void resume()
	{
		m_userStopped.store(false);
		std::lock_guard lock(m_devMutex);
		if (m_dev) { ma_device_start(m_dev.get()); }
	}

	/// 実機を開けない状況を試験する。Null 機器は開ける
	void setOpenFailureForTest(bool fail) noexcept { m_failOpen.store(fail); }

	void reopenNowForTest() { reopen(); }

	/// OS の通知を実際の通知と同じ処理に通す試験用
	void notifyForTest(ma_device_notification_type type)
	{
		std::lock_guard lock(m_devMutex);
		if (!m_dev) { return; }
		ma_device_notification n{};
		n.pDevice = m_dev.get();
		n.type = type;
		onNotify(&n);
	}

	/// 機器を入れ替えた回数。最初に開いた分を含む
	[[nodiscard]] int swapCount() const noexcept { return m_swapCount.load(); }
	[[nodiscard]] int latencyMeasureCount() const noexcept { return m_latencyMeasures.load(); }

private:
	enum class Kind { Named, Default, Null };

	struct Target
	{
		Kind        kind = Kind::Null;
		std::string name;
		ma_device_id id{};
		bool operator==(const Target& o) const { return kind == o.kind && name == o.name; }
	};

	static void onData(ma_device* dev, void* out, const void*, ma_uint32 frames)
	{
		auto* self = static_cast<MiniaudioDeviceHost*>(dev->pUserData);
		self->m_fn(self->m_user, static_cast<float*>(out), frames);
	}

	static void onNotify(const ma_device_notification* n)
	{
		auto* self = static_cast<MiniaudioDeviceHost*>(n->pDevice->pUserData);
		switch (n->type)
		{
		case ma_device_notification_type_stopped:
			// 入れ替え、stopAll、終了で止めた場合は、機器が失われたとみなさない
			if (!self->m_swapping.load() && !self->m_userStopped.load() && !self->m_closing.load())
			{
				self->m_lost.store(true);
			}
			break;
		case ma_device_notification_type_rerouted:
		case ma_device_notification_type_interruption_ended:
			self->m_latencyDirty.store(true);   // 振り替え先の機器でバッファの長さが変わりうる
			break;
		default: break;
		}
	}

	[[nodiscard]] bool due()
	{
		if (m_reopenRequested.exchange(false)) { return true; }
		if (m_lost.load()) { return true; }
		std::lock_guard lock(m_stateMutex);
		const bool unsettled = !m_status.onRealDevice || !m_status.onPreferred;
		return unsettled && std::chrono::steady_clock::now() >= m_nextRetry;
	}

	// 既定の優先順では、実際の backend がすべて失敗すると Null が選ばれ、音が出なくても正常に見える
	[[nodiscard]] static std::vector<ma_backend> realBackends()
	{
		ma_backend all[MA_BACKEND_COUNT];
		std::size_t n = 0;
		std::vector<ma_backend> out;
		if (ma_get_enabled_backends(all, MA_BACKEND_COUNT, &n) != MA_SUCCESS) { return out; }
		for (std::size_t i = 0; i < n; ++i)
		{
			if (all[i] != ma_backend_null) { out.push_back(all[i]); }
		}
		return out;
	}

	bool ensureContexts()
	{
		std::lock_guard lock(m_ctxMutex);
		if (!m_ctxReady)
		{
			ma_context_config cc = ma_context_config_init();
			const std::vector<ma_backend> backends = m_params.nullOnly ? std::vector<ma_backend>{ma_backend_null} : realBackends();
			m_ctxReady = !backends.empty()
			          && ma_context_init(backends.data(), static_cast<ma_uint32>(backends.size()), &cc, &m_ctx) == MA_SUCCESS;
		}
		if (!m_nullCtxReady)
		{
			ma_context_config cc = ma_context_config_init();
			const ma_backend nullBackend = ma_backend_null;
			m_nullCtxReady = ma_context_init(&nullBackend, 1, &cc, &m_nullCtx) == MA_SUCCESS;
		}
		return m_ctxReady || m_nullCtxReady;
	}

	/// 出力機器を列挙し直す。名前で選ぶために id を保持し、ワーカーだけが触る
	void enumerate()
	{
		std::vector<OutputDeviceInfo> list;
		m_ids.clear();
		{
			std::lock_guard lock(m_ctxMutex);
			if (!m_ctxReady) { return; }
			ma_device_info* playback = nullptr;
			ma_uint32 count = 0;
			ma_device_info* capture = nullptr;
			ma_uint32 captureCount = 0;
			if (ma_context_get_devices(&m_ctx, &playback, &count, &capture, &captureCount) != MA_SUCCESS) { return; }
			for (ma_uint32 i = 0; i < count; ++i)
			{
				list.push_back(OutputDeviceInfo{playback[i].name, playback[i].isDefault != 0});
				m_ids.push_back(playback[i].id);
			}
		}
		std::lock_guard state(m_stateMutex);
		m_devices = std::move(list);
	}

	[[nodiscard]] bool findNamed(const std::string& name, ma_device_id& id) const
	{
		for (std::size_t i = 0; i < m_ids.size(); ++i)
		{
			if (m_devices[i].name == name) { id = m_ids[i]; return true; }
		}
		return false;
	}

	[[nodiscard]] std::string defaultName() const
	{
		for (const auto& d : m_devices)
		{
			if (d.isDefault) { return d.name; }
		}
		return {};
	}

	std::unique_ptr<ma_device> open(const Target& t)
	{
		if (t.kind != Kind::Null && m_failOpen.load()) { return nullptr; }
		ma_device_config c = ma_device_config_init(ma_device_type_playback);
		c.playback.pDeviceID = (t.kind == Kind::Named) ? &t.id : nullptr;
		c.playback.format = ma_format_f32;
		c.playback.channels = m_params.channels;
		c.sampleRate = m_params.sampleRate;
		c.periodSizeInMilliseconds = m_params.periodMs;
		c.dataCallback = &MiniaudioDeviceHost::onData;
		c.notificationCallback = &MiniaudioDeviceHost::onNotify;
		c.pUserData = this;
		c.noPreSilencedOutputBuffer = MA_TRUE;   // data callback が全フレームを書く
		c.noClip = MA_TRUE;                      // engine の側で扱う
		auto dev = std::make_unique<ma_device>();
		std::lock_guard lock(m_ctxMutex);
		ma_context* ctx = (t.kind == Kind::Null) ? (m_nullCtxReady ? &m_nullCtx : nullptr) : (m_ctxReady ? &m_ctx : nullptr);
		if (ctx == nullptr || ma_device_init(ctx, &c, dev.get()) != MA_SUCCESS) { return nullptr; }
		return dev;
	}

	/// 古い機器を止めてから新しい機器を始める。新しい機器を始められなければ古い機器に戻す
	bool swapIn(std::unique_ptr<ma_device> fresh)
	{
		m_swapping.store(true);
		std::unique_ptr<ma_device> old;
		{
			std::lock_guard lock(m_devMutex);
			if (m_dev) { ma_device_stop(m_dev.get()); }
		}
		const bool paused = m_userStopped.load();
		if (!paused && ma_device_start(fresh.get()) != MA_SUCCESS)
		{
			{
				std::lock_guard lock(m_devMutex);
				if (m_dev) { ma_device_start(m_dev.get()); }
			}
			m_swapping.store(false);
			ma_device_uninit(fresh.get());
			return false;
		}
		{
			std::lock_guard lock(m_devMutex);
			old = std::move(m_dev);
			m_dev = std::move(fresh);
			m_latencyDirty.store(true);
		}
		m_swapping.store(false);
		if (old) { ma_device_uninit(old.get()); }
		return true;
	}

	[[nodiscard]] std::vector<Target> candidates(const std::string& preferred, bool& preferredFound)
	{
		std::vector<Target> list;
		preferredFound = preferred.empty();
		if (!preferred.empty())
		{
			Target t{Kind::Named, preferred, {}};
			if (findNamed(preferred, t.id)) { list.push_back(t); preferredFound = true; }
		}
		list.push_back(Target{Kind::Default, {}, {}});
		list.push_back(Target{Kind::Null, {}, {}});
		return list;
	}

	void reopen()
	{
		if (m_closing.load()) { return; }
		const bool lost = m_lost.exchange(false);
		if (!ensureContexts()) { scheduleRetry(); return; }
		enumerate();
		const std::string preferred = this->preferred();
		bool preferredFound = false;
		const std::vector<Target> list = candidates(preferred, preferredFound);
		if (!preferredFound)
		{
			mitiru::debug::warnOnce("audio.device.missing:" + preferred,
				"選んだ出力機器 " + preferred + " が見つからないので、既定の機器で鳴らします");
		}
		// 機器が失われておらず、現在の機器が最優先の候補なら、音が一瞬途切れる入れ替えを行わない
		if (!lost && isCurrent(list.front())) { scheduleRetry(); return; }
		for (const Target& t : list)
		{
			auto dev = open(t);
			if (!dev || !swapIn(std::move(dev))) { continue; }
			publish(t, preferred, t.kind == Kind::Default ? defaultName() : t.name);
			scheduleRetry();
			return;
		}
		scheduleRetry();
	}

	[[nodiscard]] bool isCurrent(const Target& t) const
	{
		std::lock_guard lock(m_stateMutex);
		return m_status.onRealDevice && m_current == t;
	}

	void publish(const Target& t, const std::string& preferred, std::string activeName)
	{
		ma_uint32 channels = 0;
		ma_uint32 rate = 0;
		{
			std::lock_guard lock(m_devMutex);
			if (m_dev) { channels = m_dev->playback.internalChannels; rate = m_dev->playback.internalSampleRate; }
		}
		std::lock_guard lock(m_stateMutex);
		m_status.internalChannels = channels;
		m_status.internalSampleRate = rate;
		m_current = t;
		m_status.onRealDevice = (t.kind != Kind::Null);
		m_status.onPreferred = (t.kind == Kind::Named) || (t.kind == Kind::Default && preferred.empty());
		m_status.activeName = std::move(activeName);
		m_swapCount.fetch_add(1);   // 状態を載せた後に数える (数えた時点で status() が新しい)
	}

	void scheduleRetry()
	{
		std::lock_guard lock(m_stateMutex);
		m_nextRetry = std::chrono::steady_clock::now()
		            + std::chrono::milliseconds(static_cast<long long>(m_params.retrySec * 1000.0));
	}

	Params m_params;
	DataFn m_fn = nullptr;
	void*  m_user = nullptr;

	ma_context m_ctx{};
	ma_context m_nullCtx{};
	bool       m_ctxReady = false;
	bool       m_nullCtxReady = false;
	std::mutex m_ctxMutex;

	mutable std::mutex         m_devMutex;
	std::unique_ptr<ma_device> m_dev;
	std::atomic<double>        m_latency{0.0};   // 入れ替え中は錠を取らずに読むので atomic
	std::chrono::steady_clock::time_point m_latencyAt{};

	mutable std::mutex m_stateMutex;
	std::string        m_preferred;
	std::vector<OutputDeviceInfo> m_devices;
	std::vector<ma_device_id>     m_ids;   ///< m_devices と同じ並び。ワーカーだけが触る
	OutputDeviceStatus m_status;
	Target             m_current;
	std::chrono::steady_clock::time_point m_nextRetry{};

	std::atomic<bool> m_busy{false};
	std::atomic<bool> m_closing{false};
	std::atomic<bool> m_swapping{false};
	std::atomic<bool> m_userStopped{false};
	std::atomic<bool> m_lost{false};
	std::atomic<bool> m_reopenRequested{false};
	std::atomic<bool> m_latencyDirty{true};
	std::atomic<bool> m_failOpen{false};
	std::atomic<int>  m_swapCount{0};
	std::atomic<int>  m_latencyMeasures{0};
	std::thread       m_worker;
};

}  // namespace mitiru::audio::detail
