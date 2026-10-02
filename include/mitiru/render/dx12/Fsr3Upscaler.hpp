#pragma once

/// @file Fsr3Upscaler.hpp
/// @brief AMD FidelityFX FSR 3.1 の upscaler (frame generation は使わない) の窓口
/// @details 実装は src/fsr3/Fsr3Upscaler.cpp で、MITIRU_WITH_FSR3=ON の時だけ作られる。FFX のヘッダはその翻訳単位
///          だけが見る。OFF の時は create が必ず失敗する空の型になり、Renderer3D_DX12 は TAAU で戻す (ADR 0058)。

#ifdef _WIN32

#include <cstdint>
#include <memory>
#include <string>

#include <d3d12.h>

namespace mitiru::render::dx12
{

/// @brief FSR に渡す資源と、その時点の状態。FSR は自分の都合で遷移させ、終わると同じ状態へ戻す
struct Fsr3Texture
{
	ID3D12Resource*       resource = nullptr;
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
};

struct Fsr3Dispatch
{
	ID3D12GraphicsCommandList* commandList = nullptr;
	Fsr3Texture color;            ///< 内部解像度の色 (tonemap 済みの LDR)
	Fsr3Texture depth;            ///< 内部解像度の 1 標本の深度 (R32_FLOAT、近 0・遠 1)
	Fsr3Texture motionVectors;    ///< 内部解像度の動き (R16G16_FLOAT、UV 単位の 前 − 今)
	Fsr3Texture reactive;         ///< 内部解像度の R8。resource が null なら使わない
	Fsr3Texture output;           ///< 出力解像度。UAV を持つこと
	float jitterX = 0.0f;         ///< 射影のずらし (内部解像度の画素、FSR の jitterOffset と同じ符号)
	float jitterY = 0.0f;
	float motionScaleX = 0.0f;    ///< 動きベクトルを画素にする倍率
	float motionScaleY = 0.0f;
	std::uint32_t renderWidth = 0;
	std::uint32_t renderHeight = 0;
	float sharpness = 0.0f;       ///< RCAS の強さ 0..1。0 なら掛けない
	float frameTimeMs = 16.6f;
	float cameraNear = 0.1f;
	float cameraFar = 100.0f;
	float fovY = 1.0f;            ///< 縦の画角 (ラジアン)
	bool  reset = false;          ///< 場面が切り替わったフレームで履歴を捨てる
};

#ifdef MITIRU_HAS_FSR3

class Fsr3Upscaler
{
public:
	Fsr3Upscaler();
	~Fsr3Upscaler();
	Fsr3Upscaler(const Fsr3Upscaler&) = delete;
	Fsr3Upscaler& operator=(const Fsr3Upscaler&) = delete;

	[[nodiscard]] static constexpr bool compiled() noexcept { return true; }

	/// @brief FSR の文脈と共有資源を作る。大きさが変わったら destroy してから作り直す (GPU は呼び出し側が待つ)
	bool create(ID3D12Device* device, std::uint32_t renderWidth, std::uint32_t renderHeight, std::uint32_t displayWidth,
	            std::uint32_t displayHeight);
	void destroy() noexcept;
	[[nodiscard]] bool ready() const noexcept;
	/// @brief commandList へ FSR の compute を積む。終わると descriptor heap と root signature は FSR のものになっている
	bool dispatch(const Fsr3Dispatch& d);
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
	std::string m_error;
};

#else

class Fsr3Upscaler
{
public:
	[[nodiscard]] static constexpr bool compiled() noexcept { return false; }

	bool create(ID3D12Device*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)
	{
		m_error = "FSR 3.1 はこのビルドに入っていない (MITIRU_WITH_FSR3=OFF)";
		return false;
	}
	void destroy() noexcept {}
	[[nodiscard]] bool ready() const noexcept { return false; }
	bool dispatch(const Fsr3Dispatch&) { return false; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

private:
	std::string m_error;
};

#endif

} // namespace mitiru::render::dx12

#endif // _WIN32
