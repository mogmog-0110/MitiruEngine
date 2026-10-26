#pragma once

// Dawn の instance / adapter / device をまとめて持つ。webgpu のヘッダを使うのは src/dawn/ の中だけにして、
// エンジンの公開ヘッダ (Engine.hpp など) へ Dawn の型を出さない。

#include <dawn/native/DawnNative.h>
#include <webgpu/webgpu_cpp.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/gfx/ShaderCompileCount.hpp>
#include <mitiru/gfx/dawn/DawnApi.hpp>

namespace mitiru::gfx
{

struct DawnContext
{
	DawnApi       api = DawnApi::D3D11;
	bool          compat = false;       ///< WebGPU の互換 (compatibility) レベルで作ったか。D3D11 の FL 11_0 はこの扱い
	std::string   description;          ///< アダプタの説明 (例: "D3D11 driver version ...")
	std::string   deviceName;
	std::uint32_t vendorId = 0;
	bool          lost = false;         ///< デバイスが失われた。以後の描画は止める
	std::atomic<std::uint32_t> uncapturedErrors{0};   ///< 検証の誤りなど、捕まえずに Dawn が知らせた数。warnOnce は同じ文を 1 回しか出さないので、試験はこちらで数える

	std::unique_ptr<dawn::native::Instance> native;
	wgpu::Instance instance;
	wgpu::Adapter  adapter;
	wgpu::Device   device;
	wgpu::Queue    queue;

	/// @brief 作る。失敗したら nullptr と error。
	/// @details アダプタの選び方は環境変数で絞れる。MITIRU_DAWN_VENDOR=nvidia|amd|intel (既定は any)、
	///          MITIRU_DAWN_COMPAT=1 で互換レベルを強制、MITIRU_DAWN_ALLOW_SOFTWARE=1 でソフトウェアのアダプタも許す。
	[[nodiscard]] static std::shared_ptr<DawnContext> create(DawnApi api, std::string& error);

	~DawnContext();

	/// @brief done が立つまで ProcessEvents を回して待つ。timeoutMs を過ぎたら false
	bool pump(const bool& done, int timeoutMs) const;

	/// @brief キューに積んだ仕事が終わるまで待つ
	bool waitIdle(int timeoutMs = 20000) const;

	/// @brief RGBA8 / BGRA8 (4 byte / 画素) のテクスチャを CPU へ読む。RGBA の並びで返す
	[[nodiscard]] std::vector<std::uint8_t> readTexture(const wgpu::Texture& texture, std::uint32_t width, std::uint32_t height) const;

	[[nodiscard]] wgpu::ShaderModule makeShader(const std::string& wgsl, const char* label) const;
};

[[nodiscard]] inline std::string dawnStr(wgpu::StringView v)
{
	if (v.data == nullptr) { return {}; }
	return v.length == WGPU_STRLEN ? std::string(v.data) : std::string(v.data, v.length);
}

/// @brief Dawn の作業スレッドで作る描画パイプライン。D3D11 は FXC で 1 本あたり数十 ms から 1 秒余りかかるので、start で作り始めて起動の他の仕事と並べ、
///        使うとき (get、または SetPipeline への暗黙の変換) に終わっていなければ待つ。作れなかったときは空のパイプラインになり、理由を 1 回だけ知らせる。
///        ctx は start から使い終わるまで生きていること (パスが持つ DawnContext の参照がその約束を満たす)
class AsyncRenderPipeline
{
public:
	/// label は MITIRU_DAWN_PSO_LOG=1 のときに、作るのにかかった時間と待った時間を stderr へ出す名前 (文字列リテラル)
	void start(const DawnContext& ctx, const wgpu::RenderPipelineDescriptor& descriptor, const char* label = "pipeline")
	{
		m_ctx = &ctx;
		m_label = label;
		if (syncRequested())
		{
			// MITIRU_DAWN_SYNC_PSO=1: 非同期をやめて、その場で作る (起動の時間と、最初の使用の費用を切り分けるための印)
			::mitiru::gfx::notePipelineCreated();
			m_pipeline = ctx.device.CreateRenderPipeline(&descriptor);
			return;
		}
		m_state = std::make_shared<State>();
		m_state->startedAt = std::chrono::steady_clock::now();
		::mitiru::gfx::notePipelineCreated();
		ctx.device.CreateRenderPipelineAsync(&descriptor, wgpu::CallbackMode::AllowProcessEvents,
			[state = m_state](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline pipeline, wgpu::StringView message) {
				if (status == wgpu::CreatePipelineAsyncStatus::Success) { state->pipeline = std::move(pipeline); }
				else { state->message = dawnStr(message); }
				state->finishedAt = std::chrono::steady_clock::now();
				state->done = true;
			});
	}

	[[nodiscard]] const wgpu::RenderPipeline& get() const
	{
		if (m_state)
		{
			const auto waitBegan = std::chrono::steady_clock::now();
			(void)m_ctx->pump(m_state->done, 60000);
			logTiming(waitBegan);
			m_pipeline = m_state->pipeline;
			if (!m_pipeline) { reportFailure(m_state->message); }
			m_state.reset();
		}
		return m_pipeline;
	}

	/// 作り始めた、または作り終えているか (作れたかは get の結果で見る)
	explicit operator bool() const noexcept { return m_state != nullptr || static_cast<bool>(m_pipeline); }
	operator const wgpu::RenderPipeline&() const { return get(); }

private:
	struct State
	{
		bool done = false;
		wgpu::RenderPipeline pipeline;
		std::string message;
		std::chrono::steady_clock::time_point startedAt;
		std::chrono::steady_clock::time_point finishedAt;
	};
	static void reportFailure(const std::string& message);
	[[nodiscard]] static bool syncRequested();
	void logTiming(std::chrono::steady_clock::time_point waitBegan) const;

	const DawnContext* m_ctx = nullptr;
	const char* m_label = "pipeline";
	mutable std::shared_ptr<State> m_state;
	mutable wgpu::RenderPipeline m_pipeline;
};

} // namespace mitiru::gfx
