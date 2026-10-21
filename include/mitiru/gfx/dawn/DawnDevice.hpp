#pragma once

/// @file DawnDevice.hpp
/// @brief Dawn（ネイティブ WebGPU）の IDevice 宣言。MITIRU_WITH_DAWN=ON のビルドだけで使う
/// @details 最終色を 1 枚の RGBA8 オフスクリーンテクスチャにまとめ、readPixels() で読み戻す。3D は Renderer3D_Dawn、UI は RmlUiHost が同じテクスチャに重ねる。
///          窓つきのときは、フレームの最後にそのテクスチャをスワップチェーンへ写して提示する。Dawn の型は src/dawn/ の外に出さない。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/dawn/DawnApi.hpp>

namespace mitiru::gfx
{

struct DawnContext;

class DawnDevice final : public IDevice
{
public:
	/// @brief 窓なしの Dawn デバイスを作る
	/// @param api D3D11 / D3D12 / Vulkan のどれを使うか
	/// @throw std::runtime_error アダプタかデバイスを作れなかったとき。理由を含む
	DawnDevice(int width, int height, DawnApi api);

	/// @brief 窓 (HWND) に提示する Dawn デバイスを作る
	/// @param nativeWindow IWindow::nativeHandle() の値 (Win32 の HWND)
	/// @throw std::runtime_error アダプタ、デバイス、スワップチェーンのどれかを作れなかったとき
	DawnDevice(std::uintptr_t nativeWindow, int width, int height, DawnApi api, bool vsync);
	~DawnDevice() override;

	DawnDevice(const DawnDevice&) = delete;
	DawnDevice& operator=(const DawnDevice&) = delete;

	[[nodiscard]] std::vector<std::uint8_t> readPixels(int width, int height) const override;
	[[nodiscard]] Backend backend() const noexcept override { return Backend::Dawn; }
	void beginFrame() override;
	void endFrame() override;
	[[nodiscard]] std::unique_ptr<IBuffer> createBuffer(BufferType, std::uint32_t, bool, const void*) override { return nullptr; }
	[[nodiscard]] std::unique_ptr<ICommandList> createCommandList() override { return nullptr; }
	void waitForGpu() override;
	void onResize(int w, int h) override;

	/// @brief 垂直同期を切り替える。窓つきのときだけ効く
	void setVSync(bool enabled);

	[[nodiscard]] int width() const noexcept { return m_width; }
	[[nodiscard]] int height() const noexcept { return m_height; }
	[[nodiscard]] DawnApi api() const noexcept { return m_api; }

	/// @brief 使用中のアダプタの説明。診断に使う
	[[nodiscard]] const std::string& adapterDescription() const noexcept { return m_description; }
	[[nodiscard]] bool compatibilityLevel() const noexcept;

	/// @brief GPU のデバイスが失われたか。作り直す経路は無いので、Engine が理由を出して終える
	[[nodiscard]] bool isDeviceLost() const noexcept;

	/// @brief レンダラーと UI が共有する DawnContext
	[[nodiscard]] const std::shared_ptr<DawnContext>& context() const noexcept { return m_context; }

	/// @brief 最終色のテクスチャを持つ。型を隠すため、src/dawn/ の中だけで使う
	struct Target;
	[[nodiscard]] Target& target() const noexcept { return *m_target; }

private:
	void createTarget();
	void configureSurface();
	void present();

	std::shared_ptr<DawnContext> m_context;
	std::unique_ptr<Target> m_target;
	std::string m_description;
	int m_width = 0;
	int m_height = 0;
	DawnApi m_api = DawnApi::D3D11;
	bool m_windowed = false;
	bool m_vsync = true;
	bool m_surfaceDirty = false;   ///< 最小化や GPU の切り替えで surface が古くなったので、次の提示の前に設定し直す
	int m_loseAtFrame = 0;   ///< 試験用: この番号のフレームの終わりにデバイスを失わせる (0 は使わない)
	int m_frames = 0;
};

} // namespace mitiru::gfx
