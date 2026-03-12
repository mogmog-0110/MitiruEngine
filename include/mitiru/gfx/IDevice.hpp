#pragma once

/// @file IDevice.hpp
/// @brief GPUデバイス抽象インターフェース
/// @details グラフィックスデバイスの最小インターフェースを定義する。
///          Phase 0 では readPixels とバックエンド問い合わせのみ。

#include <cstdint>
#include <memory>
#include <vector>

#include <mitiru/core/Config.hpp>
#include <mitiru/gfx/IDescriptorHeap.hpp>
#include <mitiru/gfx/IGpuFence.hpp>

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

	/// @brief 現在のフレームインデックスを取得する（トリプルバッファリング用）
	/// @return フレームインデックス（デフォルトは0）
	/// @details D3D12のトリプルバッファリングで使用する。
	///          DX11/Null等のバックエンドでは常に0を返す。
	[[nodiscard]] virtual uint32_t currentFrameIndex() const { return 0; }

	/// @brief フレームインフライト数を取得する
	/// @return 同時にGPUで処理中になりうるフレーム数（デフォルトは1）
	/// @details D3D12のトリプルバッファリングでは通常2〜3を返す。
	///          DX11/Null等のバックエンドでは1を返す。
	[[nodiscard]] virtual uint32_t frameInFlightCount() const { return 1; }

	/// @brief GPUフェンスを作成する
	/// @return 生成されたフェンスのユニークポインタ（未対応バックエンドではnullptr）
	/// @details D3D12のID3D12Fenceに対応するフェンスを生成する。
	///          DX11/Null等のバックエンドではnullptrを返す。
	[[nodiscard]] virtual std::unique_ptr<IGpuFence> createFence()
	{
		return nullptr;
	}

	/// @brief デスクリプタヒープを作成する
	/// @param heapType ヒープ種別
	/// @param capacity 最大デスクリプタ数
	/// @return 生成されたヒープのユニークポインタ（未対応バックエンドではnullptr）
	/// @details D3D12のID3D12DescriptorHeapを生成する。
	///          DX11/Null等のバックエンドではnullptrを返す。
	[[nodiscard]] virtual std::unique_ptr<IDescriptorHeap> createDescriptorHeap(
		DescriptorHeapType heapType, uint32_t capacity)
	{
		static_cast<void>(heapType);
		static_cast<void>(capacity);
		return nullptr;
	}

	/// @brief コマンドリストを実行する（D3D12用）
	/// @param lists コマンドリストの配列
	/// @param count コマンドリスト数
	/// @details D3D12のID3D12CommandQueue::ExecuteCommandListsに対応する。
	///          DX11では即時コンテキストが直接実行するため不要。
	virtual void executeCommandLists(
		ICommandList* const* lists, uint32_t count)
	{
		static_cast<void>(lists);
		static_cast<void>(count);
	}

	/// @brief GPU処理の完了を待機する
	/// @details GPUキュー上の全コマンドが完了するまでCPUをブロックする。
	///          DX11/Null等のバックエンドでは何もしない。
	virtual void waitForGpu() {}
};

} // namespace mitiru::gfx
