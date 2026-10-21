#pragma once

// Dawn の instance / adapter / device をまとめて持つ。webgpu のヘッダを使うのは src/dawn/ の中だけにして、
// エンジンの公開ヘッダ (Engine.hpp など) へ Dawn の型を出さない。

#include <dawn/native/DawnNative.h>
#include <webgpu/webgpu_cpp.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

} // namespace mitiru::gfx
