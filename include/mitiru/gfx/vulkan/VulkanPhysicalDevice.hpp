#pragma once

/// @file VulkanPhysicalDevice.hpp
/// @brief Vulkan物理デバイス選択
/// @details GPUの列挙・スコアリング・キューファミリ探索を提供する。
///          MITIRU_HAS_VULKANが定義されている場合のみコンパイルされる。

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::gfx
{

/// @brief キューファミリ種別
enum class QueueFamily : std::uint8_t
{
	Graphics = 0,  ///< グラフィックスキュー
	Compute,       ///< コンピュートキュー
	Transfer,      ///< 転送キュー
	Present        ///< プレゼントキュー
};

/// @brief キューファミリインデックス情報
/// @details 物理デバイスが対応するキューファミリのインデックスを保持する。
struct QueueFamilyIndices
{
	std::optional<std::uint32_t> graphics;   ///< グラフィックスキューファミリ
	std::optional<std::uint32_t> compute;    ///< コンピュートキューファミリ
	std::optional<std::uint32_t> transfer;   ///< 転送キューファミリ
	std::optional<std::uint32_t> present;    ///< プレゼントキューファミリ

	/// @brief 必須キューファミリが全て揃っているか確認する
	/// @return グラフィックスとプレゼントが揃っていればtrue
	[[nodiscard]] bool isComplete() const noexcept
	{
		return graphics.has_value() && present.has_value();
	}

	/// @brief 全キューファミリが揃っているか確認する
	/// @return 全4種が揃っていればtrue
	[[nodiscard]] bool hasAll() const noexcept
	{
		return graphics.has_value()
			&& compute.has_value()
			&& transfer.has_value()
			&& present.has_value();
	}
};

/// @brief GPU種別
enum class GpuType : std::uint8_t
{
	Unknown = 0,       ///< 不明
	IntegratedGpu,     ///< 統合GPU
	DiscreteGpu,       ///< ディスクリートGPU
	VirtualGpu,        ///< 仮想GPU
	Cpu                ///< CPUソフトウェアレンダリング
};

/// @brief 物理デバイス情報
/// @details Vulkan物理デバイスの静的な属性情報を保持する。
///          実際のVkPhysicalDeviceに依存せず、テストで使用可能。
struct PhysicalDeviceInfo
{
	std::string deviceName;                            ///< デバイス名
	std::uint32_t vendorId = 0;                        ///< ベンダーID
	std::uint32_t deviceId = 0;                        ///< デバイスID
	GpuType gpuType = GpuType::Unknown;                ///< GPU種別
	std::uint32_t apiVersion = 0;                      ///< サポートするVulkan APIバージョン
	std::uint32_t driverVersion = 0;                   ///< ドライバーバージョン
	std::uint64_t dedicatedVideoMemory = 0;            ///< 専用ビデオメモリ（バイト）
	QueueFamilyIndices queueFamilies;                  ///< キューファミリインデックス
	std::vector<std::string> supportedExtensions;      ///< サポートする拡張名リスト

	/// @brief 指定の拡張をサポートしているか確認する
	/// @param extensionName 拡張名
	/// @return サポートしていればtrue
	[[nodiscard]] bool supportsExtension(const std::string& extensionName) const noexcept
	{
		return std::find(
			supportedExtensions.begin(),
			supportedExtensions.end(),
			extensionName) != supportedExtensions.end();
	}
};

/// @brief 物理デバイスのスコアリング関数
/// @details ディスクリートGPUを優先し、ビデオメモリ量でスコアを付ける。
/// @param info 物理デバイス情報
/// @return スコア（高いほど優先される）
[[nodiscard]] inline int scorePhysicalDevice(const PhysicalDeviceInfo& info) noexcept
{
	int score = 0;

	/// GPU種別による基本スコア
	switch (info.gpuType)
	{
	case GpuType::DiscreteGpu:
		score += 1000;
		break;
	case GpuType::IntegratedGpu:
		score += 100;
		break;
	case GpuType::VirtualGpu:
		score += 50;
		break;
	case GpuType::Cpu:
		score += 10;
		break;
	case GpuType::Unknown:
		break;
	}

	/// 専用ビデオメモリ（MB単位でスコア加算）
	score += static_cast<int>(info.dedicatedVideoMemory / (1024 * 1024));

	/// 必須キューファミリの存在
	if (info.queueFamilies.isComplete())
	{
		score += 500;
	}

	/// スワップチェーン拡張のサポート
	if (info.supportsExtension("VK_KHR_swapchain"))
	{
		score += 200;
	}

	return score;
}

/// @brief 物理デバイスリストから最適なデバイスを選択する
/// @param devices 物理デバイス情報のリスト
/// @return 最適なデバイスのインデックス（リストが空なら std::nullopt）
[[nodiscard]] inline std::optional<std::size_t> selectBestDevice(
	const std::vector<PhysicalDeviceInfo>& devices) noexcept
{
	if (devices.empty())
	{
		return std::nullopt;
	}

	std::size_t bestIndex = 0;
	int bestScore = scorePhysicalDevice(devices[0]);

	for (std::size_t i = 1; i < devices.size(); ++i)
	{
		const int currentScore = scorePhysicalDevice(devices[i]);
		if (currentScore > bestScore)
		{
			bestScore = currentScore;
			bestIndex = i;
		}
	}

	return bestIndex;
}

#ifdef MITIRU_HAS_VULKAN

/// @brief Vulkan物理デバイスラッパー
/// @details VkPhysicalDeviceの選択とキューファミリ探索を管理する。
///
/// @code
/// VulkanPhysicalDevice physDevice(instance);
/// auto info = physDevice.info();
/// if (info.queueFamilies.isComplete())
/// {
///     // 論理デバイスの生成に進む
/// }
/// @endcode
class VulkanPhysicalDevice
{
public:
	/// @brief コンストラクタ（デバイス情報を直接指定）
	/// @param info 物理デバイス情報
	explicit VulkanPhysicalDevice(const PhysicalDeviceInfo& info)
		: m_info(info)
	{
	}

	/// @brief 物理デバイス情報を取得する
	/// @return PhysicalDeviceInfoへのconst参照
	[[nodiscard]] const PhysicalDeviceInfo& info() const noexcept
	{
		return m_info;
	}

	/// @brief キューファミリインデックスを取得する
	/// @return QueueFamilyIndicesへのconst参照
	[[nodiscard]] const QueueFamilyIndices& queueFamilies() const noexcept
	{
		return m_info.queueFamilies;
	}

	/// @brief デバイスのスコアを取得する
	/// @return スコア値
	[[nodiscard]] int score() const noexcept
	{
		return scorePhysicalDevice(m_info);
	}

private:
	PhysicalDeviceInfo m_info;  ///< デバイス情報
};

#endif // MITIRU_HAS_VULKAN

} // namespace mitiru::gfx
