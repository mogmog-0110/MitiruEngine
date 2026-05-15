#pragma once

/// @file VulkanSwapChain.hpp
/// @brief Vulkanスワップチェーン ユニットテスト用スタブ
/// @details ISwapChainインターフェースのユニットテスト専用実装。
///          実際のスワップチェーン管理（VkSwapchainKHR生成・フレームバッファ・
///          イメージビュー・プレゼント）はすべてVulkanDeviceが担当する。
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

/// @brief Vulkanスワップチェーン ユニットテスト用スタブ
/// @details ISwapChainインターフェースを実装するテスト専用クラス。
///          ダミーのレンダーターゲットを返し、present/resizeはノーオペレーション。
///          実際のVkSwapchainKHRの生成・管理・vkQueuePresentKHRの呼び出しは
///          VulkanDeviceが担当するため、このクラスは実運用では使用されない。
///
///          needsRecreation()フラグはウィンドウリサイズやVK_ERROR_OUT_OF_DATE_KHR
///          受信時にtrueになる。recreate()を呼ぶことでフラグをクリアする。
///
/// @code
/// VulkanSwapChain swapChain(1280, 720);
/// auto* backBuffer = swapChain.backBuffer();
/// swapChain.present();  // テスト用ノーオペレーション
/// if (swapChain.needsRecreation())
/// {
///     swapChain.recreate(device, physDevice, surface, newW, newH);
/// }
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

	/// @brief バックバッファを画面に表示する（テスト用ノーオペレーション）
	/// @note 実際のプレゼント処理はVulkanDevice::endFrame()が行う
	void present() override
	{
	}

	/// @brief スワップチェーンのサイズを変更し、再作成フラグを立てる
	/// @param width 新しい幅（ピクセル）
	/// @param height 新しい高さ（ピクセル）
	/// @note 実際のスワップチェーン再作成はVulkanDevice::recreateSwapChain()が行う
	void resize(int width, int height) override
	{
		m_backBuffer.updateSize(width, height);
		m_needsRecreation = true;
	}

	/// @brief 現在のバックバッファを取得する
	/// @return ダミーレンダーターゲットへのポインタ
	[[nodiscard]] IRenderTarget* backBuffer() noexcept override
	{
		return &m_backBuffer;
	}

	/// @brief スワップチェーンの再作成が必要か確認する
	/// @return ウィンドウリサイズ等で再作成が必要な場合true
	[[nodiscard]] bool needsRecreation() const noexcept
	{
		return m_needsRecreation;
	}

	/// @brief 再作成フラグを手動でセットする
	/// @param needs 再作成が必要か
	void setNeedsRecreation(bool needs) noexcept
	{
		m_needsRecreation = needs;
	}

	/// @brief スワップチェーンを再作成する（テスト用スタブ）
	/// @param device 論理デバイス（未使用）
	/// @param physDevice 物理デバイス（未使用）
	/// @param surface サーフェス（未使用）
	/// @param width 新しい幅（ピクセル）
	/// @param height 新しい高さ（ピクセル）
	/// @note 実際の再作成処理はVulkanDeviceが行う。このメソッドはフラグのクリアのみ行う。
	void recreate(
		VkDevice /*device*/,
		VkPhysicalDevice /*physDevice*/,
		VkSurfaceKHR /*surface*/,
		int width,
		int height)
	{
		m_backBuffer.updateSize(width, height);
		m_needsRecreation = false;
	}

private:
	VulkanRenderTarget m_backBuffer;        ///< ダミーバックバッファ
	bool m_needsRecreation = false;         ///< スワップチェーン再作成フラグ
};

} // namespace mitiru::gfx

#endif // MITIRU_HAS_VULKAN
