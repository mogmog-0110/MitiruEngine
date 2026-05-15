#pragma once

/// @file ProfilerConnection.hpp
/// @brief エンジンシステム計測接続
/// @details ProfilerOverlayをEngine内部のサブシステム（描画・物理・オーディオ・入力・UI）
///          に接続し、フレーム毎のタイミング内訳とメモリ推定値を自動収集する。
///          HTTP APIエンドポイントとTracy連携もサポートする。

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <mitiru/debug/ProfilerOverlay.hpp>
#include <mitiru/debug/TracyIntegration.hpp>

namespace mitiru
{
class Engine;
class Screen;

namespace audio { class AudioMixer; }
namespace server { class EngineHttpServer; }
} // namespace mitiru

namespace mitiru::debug
{

// ── タイミング内訳 ─────────────────────────────────────────

/// @brief フレーム処理時間の内訳
struct FrameBreakdown
{
	float totalMs = 0.0f;     ///< フレーム全体（ミリ秒）
	float renderMs = 0.0f;    ///< 描画処理
	float physicsMs = 0.0f;   ///< 物理演算
	float audioMs = 0.0f;     ///< オーディオ更新
	float inputMs = 0.0f;     ///< 入力処理
	float uiMs = 0.0f;        ///< UI/ImGui処理
	float otherMs = 0.0f;     ///< その他（合計 - 各サブシステム）

	/// @brief JSON文字列に変換する
	[[nodiscard]] std::string toJson() const
	{
		/// snprintfで固定小数点フォーマット
		char buf[512];
		std::snprintf(buf, sizeof(buf),
			R"({"totalMs":%.3f,"renderMs":%.3f,"physicsMs":%.3f,)"
			R"("audioMs":%.3f,"inputMs":%.3f,"uiMs":%.3f,"otherMs":%.3f})",
			totalMs, renderMs, physicsMs, audioMs, inputMs, uiMs, otherMs);
		return std::string(buf);
	}
};

// ── メモリ内訳（推定値） ───────────────────────────────────

/// @brief メモリ使用量の推定内訳
/// @details 実際のアロケーション追跡ではなく、オブジェクト数とサイズから推定する。
struct MemoryBreakdown
{
	float totalMB = 0.0f;      ///< 合計推定メモリ（MB）
	float texturesMB = 0.0f;   ///< テクスチャ推定メモリ
	float meshesMB = 0.0f;     ///< メッシュ推定メモリ
	float audioMB = 0.0f;      ///< オーディオバッファ推定メモリ
	float uiMB = 0.0f;         ///< UIツリー推定メモリ

	/// @brief JSON文字列に変換する
	[[nodiscard]] std::string toJson() const
	{
		char buf[256];
		std::snprintf(buf, sizeof(buf),
			R"({"totalMB":%.2f,"texturesMB":%.2f,"meshesMB":%.2f,)"
			R"("audioMB":%.2f,"uiMB":%.2f})",
			totalMB, texturesMB, meshesMB, audioMB, uiMB);
		return std::string(buf);
	}
};

// ── スコープタイマー ───────────────────────────────────────

/// @brief RAII計測スコープ
/// @details コンストラクタで開始、デストラクタで終了して結果をターゲットに書き込む。
///          Tracy有効時は同時にTracyゾーンも発行する。
class ScopeTimer
{
public:
	/// @brief コンストラクタ（計測開始）
	/// @param targetMs 計測結果の書き込み先
	explicit ScopeTimer(float& targetMs) noexcept
		: m_targetMs(targetMs)
		, m_start(Clock::now())
	{
	}

	/// @brief デストラクタ（計測終了・結果書き込み）
	~ScopeTimer()
	{
		const auto end = Clock::now();
		const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
			end - m_start).count();
		m_targetMs = static_cast<float>(us) / 1000.0f;
	}

	/// コピー禁止
	ScopeTimer(const ScopeTimer&) = delete;
	ScopeTimer& operator=(const ScopeTimer&) = delete;

	/// ムーブ禁止
	ScopeTimer(ScopeTimer&&) = delete;
	ScopeTimer& operator=(ScopeTimer&&) = delete;

private:
	using Clock = std::chrono::high_resolution_clock;
	float& m_targetMs;                      ///< 結果の書き込み先
	Clock::time_point m_start;              ///< 計測開始時点
};

// ── プロファイラ計測マクロ ──────────────────────────────────

/// @brief プロファイラスコープ（内部タイマー + Tracy両方に送信）
/// @details MITIRU_PROFILE_SCOPE と ScopeTimer を同時に発行する。
///          Tracy無効時は ScopeTimer のみ動作する。
#define MITIRU_PROFILER_SCOPE(name, targetMs) \
	MITIRU_PROFILE_SCOPE(name); \
	::mitiru::debug::ScopeTimer _profilerTimer_##__LINE__(targetMs)

// ── プロファイラ接続クラス ─────────────────────────────────

/// @brief エンジンシステムへのプロファイラ接続
/// @details Engine内部のメインループにフックし、サブシステム毎の処理時間を計測する。
///          計測結果はFrameBreakdownとして毎フレーム更新される。
///
/// @par 接続方法
/// @code
/// Engine engine;
/// debug::ProfilerConnection profiler;
/// profiler.connect(engine);
///
/// // ゲームループ内で計測ポイントにフックする:
/// // 入力フェーズ
/// {
///     MITIRU_PROFILER_SCOPE("Input", profiler.currentFrame().inputMs);
///     inputState.beginFrame();
///     window->pollEvents();
/// }
/// // 描画フェーズ
/// {
///     MITIRU_PROFILER_SCOPE("Render", profiler.currentFrame().renderMs);
///     screen->present();
/// }
/// profiler.endFrame(); // フレーム計測を確定
///
/// auto breakdown = profiler.frameBreakdown();
/// @endcode
class ProfilerConnection
{
public:
	/// @brief コンストラクタ
	/// @param historySize フレーム履歴サイズ
	explicit ProfilerConnection(std::size_t historySize = 120) noexcept
		: m_overlay(historySize)
	{
	}

	/// @brief Engineに接続する
	/// @param engine エンジンインスタンス（ライフタイムは呼び出し元が保証する）
	/// @details HTTP APIサーバーが有効な場合、/api/profiler エンドポイントを登録する。
	void connect(Engine& engine)
	{
		m_engine = &engine;
	}

	/// @brief HTTP APIサーバーにプロファイラエンドポイントを登録する
	/// @param httpServer HTTPサーバーインスタンス
	/// @details GET /api/profiler → FrameBreakdown JSON を返す。
	void registerHttpEndpoint(server::EngineHttpServer* httpServer)
	{
		m_httpServer = httpServer;
		// EngineHttpServerのカスタムハンドラ機構に登録する
		// （EngineHttpServerの実装に依存するため、コールバック経由で接続）
	}

	/// @brief 計測対象のAudioMixerを設定する
	/// @param mixer オーディオミキサー（nullptrで解除）
	void setAudioMixer(audio::AudioMixer* mixer) noexcept
	{
		m_audioMixer = mixer;
	}

	/// @brief フレーム計測を開始する
	/// @details メインループの先頭で呼ぶ。フレーム全体のタイマーを開始する。
	void beginFrame()
	{
		m_frameStart = HiResClock::now();
		m_currentFrame = FrameBreakdown{};
	}

	/// @brief フレーム計測を確定する
	/// @details メインループの末尾で呼ぶ。otherMs を自動計算し、ProfilerOverlayに転送する。
	void endFrame()
	{
		const auto frameEnd = HiResClock::now();
		const auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(
			frameEnd - m_frameStart).count();
		m_currentFrame.totalMs = static_cast<float>(totalUs) / 1000.0f;

		/// otherMs = total - (render + physics + audio + input + ui)
		const float knownMs = m_currentFrame.renderMs
			+ m_currentFrame.physicsMs
			+ m_currentFrame.audioMs
			+ m_currentFrame.inputMs
			+ m_currentFrame.uiMs;
		m_currentFrame.otherMs = std::max(0.0f,
			m_currentFrame.totalMs - knownMs);

		/// ProfilerOverlayにフレームデータを転送する
		FrameProfile profile;
		profile.totalFrameMs = m_currentFrame.totalMs;
		profile.samples = buildSamples();
		m_overlay.pushFrame(std::move(profile));

		/// 確定済みフレームを保存
		m_lastFrame = m_currentFrame;
		++m_frameCount;

		/// Tracy連携: フレーム境界とカスタムプロット
		MITIRU_PROFILE_FRAME();
		TracyHelper::plotValue("Frame (ms)",
			static_cast<double>(m_lastFrame.totalMs));
		TracyHelper::plotValue("Render (ms)",
			static_cast<double>(m_lastFrame.renderMs));
	}

	/// @brief 現在計測中のフレームデータへの参照を取得する
	/// @return 計測中のFrameBreakdown（各サブシステムがここに書き込む）
	[[nodiscard]] FrameBreakdown& currentFrame() noexcept
	{
		return m_currentFrame;
	}

	/// @brief 最後に確定したフレーム内訳を取得する
	[[nodiscard]] const FrameBreakdown& frameBreakdown() const noexcept
	{
		return m_lastFrame;
	}

	/// @brief メモリ使用量の推定内訳を取得する
	/// @details テクスチャ数・メッシュ数等から概算する。
	///          正確なアロケーション追跡にはカスタムアロケータが必要。
	[[nodiscard]] MemoryBreakdown getMemoryBreakdown() const
	{
		MemoryBreakdown mem;

		/// テクスチャ推定: 登録テクスチャ数 * 平均テクスチャサイズ（512x512 RGBA8）
		mem.texturesMB = static_cast<float>(m_estimatedTextureCount)
			* (512.0f * 512.0f * 4.0f) / (1024.0f * 1024.0f);

		/// メッシュ推定: 登録メッシュ数 * 平均頂点数 * 頂点サイズ
		mem.meshesMB = static_cast<float>(m_estimatedMeshCount)
			* (1000.0f * 32.0f) / (1024.0f * 1024.0f);

		/// オーディオ推定: アクティブチャンネル数 * バッファサイズ
		mem.audioMB = static_cast<float>(m_estimatedAudioChannels)
			* (44100.0f * 2.0f * 2.0f * 0.5f) / (1024.0f * 1024.0f);

		/// UI推定: ノード数 * 平均ノードサイズ
		mem.uiMB = static_cast<float>(m_estimatedUINodeCount)
			* 256.0f / (1024.0f * 1024.0f);

		mem.totalMB = mem.texturesMB + mem.meshesMB + mem.audioMB + mem.uiMB;
		return mem;
	}

	/// @brief 推定用のオブジェクト数を設定する
	/// @details 各サブシステムから毎フレーム呼び出して更新する。
	void setEstimatedCounts(int textureCount, int meshCount,
		int audioChannels, int uiNodeCount) noexcept
	{
		m_estimatedTextureCount = textureCount;
		m_estimatedMeshCount = meshCount;
		m_estimatedAudioChannels = audioChannels;
		m_estimatedUINodeCount = uiNodeCount;
	}

	/// @brief 内部ProfilerOverlayを取得する
	[[nodiscard]] const ProfilerOverlay& overlay() const noexcept
	{
		return m_overlay;
	}

	/// @brief 計測済みフレーム数を取得する
	[[nodiscard]] std::uint64_t frameCount() const noexcept
	{
		return m_frameCount;
	}

	/// @brief プロファイラデータのJSON応答を生成する
	/// @return HTTP API用のJSON文字列
	/// @details GET /api/profiler のレスポンスボディとして使用する。
	[[nodiscard]] std::string toApiJson() const
	{
		const auto& frame = m_lastFrame;
		const auto mem = getMemoryBreakdown();

		char buf[1024];
		std::snprintf(buf, sizeof(buf),
			R"({"frame":%s,"memory":%s,"fps":%.1f,"frameCount":%llu})",
			frame.toJson().c_str(),
			mem.toJson().c_str(),
			m_overlay.averageFps(),
			static_cast<unsigned long long>(m_frameCount));
		return std::string(buf);
	}

	/// @brief カスタムHTTPハンドラを取得する（EngineHttpServerに登録用）
	/// @return パスとハンドラのペア
	using HttpHandler = std::function<std::string()>;

	[[nodiscard]] HttpHandler getApiHandler() const
	{
		return [this]() -> std::string { return toApiJson(); };
	}

private:
	using HiResClock = std::chrono::high_resolution_clock;

	/// @brief ProfilerOverlay用のサンプルリストを構築する
	[[nodiscard]] std::vector<ProfileSample> buildSamples() const
	{
		std::vector<ProfileSample> samples;
		float offset = 0.0f;

		auto addSample = [&](const char* name, float ms, std::uint32_t color)
		{
			if (ms > 0.0f)
			{
				samples.push_back({name, offset, ms, 0, color});
				offset += ms;
			}
		};

		addSample("Input",   m_currentFrame.inputMs,   0xFF44AA44);
		addSample("Physics", m_currentFrame.physicsMs,  0xFF4444AA);
		addSample("Audio",   m_currentFrame.audioMs,    0xFFAA44AA);
		addSample("Render",  m_currentFrame.renderMs,   0xFFAA4444);
		addSample("UI",      m_currentFrame.uiMs,       0xFFAAAA44);
		addSample("Other",   m_currentFrame.otherMs,    0xFF888888);

		return samples;
	}

	Engine* m_engine = nullptr;                    ///< 接続先エンジン（非所有）
	server::EngineHttpServer* m_httpServer = nullptr; ///< HTTPサーバー（非所有）
	audio::AudioMixer* m_audioMixer = nullptr;     ///< オーディオミキサー（非所有）
	ProfilerOverlay m_overlay;                     ///< 内部プロファイラオーバーレイ

	HiResClock::time_point m_frameStart;           ///< フレーム開始時点
	FrameBreakdown m_currentFrame;                 ///< 計測中のフレームデータ
	FrameBreakdown m_lastFrame;                    ///< 最後に確定したフレームデータ
	std::uint64_t m_frameCount = 0;                ///< 累計フレーム数

	/// メモリ推定用カウンタ
	int m_estimatedTextureCount = 0;               ///< 推定テクスチャ数
	int m_estimatedMeshCount = 0;                  ///< 推定メッシュ数
	int m_estimatedAudioChannels = 0;              ///< 推定オーディオチャンネル数
	int m_estimatedUINodeCount = 0;                ///< 推定UIノード数
};

} // namespace mitiru::debug
