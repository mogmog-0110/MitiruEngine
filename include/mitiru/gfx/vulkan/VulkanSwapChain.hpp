#pragma once

/// @file VulkanSwapChain.hpp
/// @brief Vulkanスワップチェーン スタブ実装
/// @details VkSwapchainKHRをラップするISwapChain実装。
///          現時点ではスタブであり、全メソッドがノーオペレーション。
///          MITIRU_HAS_VULKANが定義されている場合のみコンパイルされる。

#ifdef MITIRU_HAS_VULKAN

#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/ISwapChain.hpp>
#include <mitiru/gfx/ITexture.hpp>

namespace mitiru::gfx
{

/// @brief Vulkan用ダミーレンダーターゲット
/// @details スワップチェーンのバックバッファとして返されるスタブ実装。
class VulkanRenderTarget final : public IRenderTarget
{
public:
	/// @brief コンストラクタ
	/// @param width 幅（ピクセル）
	/// @param height 高さ（ピクセル）
	VulkanRenderTarget(int width, int height) noexcept
		: m_width(width)
		, m_height(height)
	{
	}

	/// @brief レンダーターゲット幅を取得する
	[[nodiscard]] int width() const noexcept override
	{
		return m_width;
	}

	/// @brief レンダーターゲット高さを取得する
	[[nodiscard]] int height() const noexcept override
	{
		return m_height;
	}

	/// @brief テクスチャを取得する（スタブ: nullptr）
	[[nodiscard]] ITexture* texture() noexcept override
	{
		return nullptr;
	}

	/// @brief サイズを更新する
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void updateSize(int width, int height) noexcept
	{
		m_width = width;
		m_height = height;
	}

private:
	int m_width;   ///< 幅（ピクセル）
	int m_height;  ///< 高さ（ピクセル）
};

/// @brief Vulkanスワップチェーン スタブ実装
/// @details VkSwapchainKHRのラッパー。
///          現時点ではダミーのレンダーターゲットを返すのみ。
///          将来のフェーズでVulkan SDKを使用した実装に置き換える。
///
/// @code
/// VulkanSwapChain swapChain(1280, 720);
/// auto* backBuffer = swapChain.backBuffer();
/// // 描画...
/// swapChain.present();
/// @endcode
class VulkanSwapChain final : public ISwapChain
{
public:
	/// @brief コンストラクタ
	/// @param width 初期幅（ピクセル）
	/// @param height 初期高さ（ピクセル）
	VulkanSwapChain(int width, int height)
		: m_backBuffer(width, height)
	{
	}

	/// @brief バックバッファを画面に表示する（スタブ: 何もしない）
	/// @note 将来のフェーズでvkQueuePresentKHRを呼び出す
	void present() override
	{
	}

	/// @brief スワップチェーンのサイズを変更する
	/// @param width 新しい幅（ピクセル）
	/// @param height 新しい高さ（ピクセル）
	/// @note 将来のフェーズでスワップチェーンの再作成を行う
	void resize(int width, int height) override
	{
		m_backBuffer.updateSize(width, height);
	}

	/// @brief 現在のバックバッファを取得する
	/// @return ダミーレンダーターゲットへのポインタ
	[[nodiscard]] IRenderTarget* backBuffer() noexcept override
	{
		return &m_backBuffer;
	}

private:
	VulkanRenderTarget m_backBuffer;  ///< ダミーバックバッファ
};

} // namespace mitiru::gfx

#endif // MITIRU_HAS_VULKAN
