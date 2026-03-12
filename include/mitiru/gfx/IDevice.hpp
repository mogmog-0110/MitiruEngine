#pragma once

/// @file IDevice.hpp
/// @brief GPUデバイス抽象インターフェース
/// @details グラフィックスデバイスの最小インターフェースを定義する。
///          Phase 0 では readPixels とバックエンド問い合わせのみ。

#include <cstdint>
#include <memory>
#include <vector>

#include <mitiru/core/Config.hpp>

namespace mitiru::gfx
{

class IBuffer;
class ICommandList;
enum class BufferType;

/// @brief GPUデバイスの抽象インターフェース
/// @details バックエンド固有の実装がこのインターフェースを実装する。
class IDevice
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IDevice() = default;

	/// @brief フレームバッファからピクセルを読み取る（スクリーンショット用）
	/// @param width 読み取り幅
	/// @param height 読み取り高さ
	/// @return RGBA8形式のピクセルデータ（width * height * 4 バイト）
	[[nodiscard]] virtual std::vector<std::uint8_t> readPixels(
		int width, int height) const = 0;

	/// @brief アクティブなバックエンドを取得する
	[[nodiscard]] virtual Backend backend() const noexcept = 0;

	/// @brief フレーム開始処理
	virtual void beginFrame() = 0;

	/// @brief フレーム終了・プレゼント処理
	virtual void endFrame() = 0;

	/// @brief GPUバッファを生成する
	/// @param bufferType バッファ種別
	/// @param sizeBytes バッファサイズ（バイト）
	/// @param dynamic 動的更新が必要か
	/// @param initialData 初期データ（nullptrで初期化なし）
	/// @return 生成されたバッファのユニークポインタ
	[[nodiscard]] virtual std::unique_ptr<IBuffer> createBuffer(
		BufferType bufferType,
		std::uint32_t sizeBytes,
		bool dynamic = false,
		const void* initialData = nullptr) = 0;

	/// @brief コマンドリストを生成する
	/// @return 生成されたコマンドリストのユニークポインタ
	[[nodiscard]] virtual std::unique_ptr<ICommandList> createCommandList() = 0;
};

} // namespace mitiru::gfx
