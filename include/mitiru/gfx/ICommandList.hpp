#pragma once

/// @file ICommandList.hpp
/// @brief コマンドリスト抽象インターフェース
/// @details GPU描画コマンドを記録するコマンドリストの基底インターフェース。

#include <cstdint>

#include <sgc/types/Color.hpp>

#include <mitiru/gfx/GfxTypes.hpp>

namespace mitiru::gfx
{

class IRenderTarget;
class IPipeline;
class IBuffer;
class ITexture;
class IDescriptorHeap;

/// @brief コマンドリストの抽象インターフェース
/// @details 描画コマンドを記録し、GPUに送信するためのインターフェース。
///
/// @code
/// commandList->begin();
/// commandList->setRenderTarget(target);
/// commandList->clearRenderTarget(sgc::Colorf{0.0f, 0.0f, 0.2f, 1.0f});
/// commandList->setPipeline(pipeline);
/// commandList->setVertexBuffer(vb);
/// commandList->setIndexBuffer(ib);
/// commandList->drawIndexed(36, 0, 0);
/// commandList->end();
/// @endcode
class ICommandList
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ICommandList() = default;

	/// @brief コマンド記録を開始する
	virtual void begin() = 0;

	/// @brief コマンド記録を終了する
	virtual void end() = 0;

	/// @brief レンダーターゲットを設定する
	/// @param target 描画先のレンダーターゲット
	virtual void setRenderTarget(IRenderTarget* target) = 0;

	/// @brief レンダーターゲットをクリアする
	/// @param color クリア色
	virtual void clearRenderTarget(const sgc::Colorf& color) = 0;

	/// @brief パイプライン状態を設定する
	/// @param pipeline 使用するパイプラインステートオブジェクト
	virtual void setPipeline(IPipeline* pipeline) = 0;

	/// @brief 頂点バッファを設定する
	/// @param buffer 頂点バッファ
	virtual void setVertexBuffer(IBuffer* buffer) = 0;

	/// @brief インデックスバッファを設定する
	/// @param buffer インデックスバッファ
	virtual void setIndexBuffer(IBuffer* buffer) = 0;

	/// @brief インデックス付き描画を実行する
	/// @param indexCount 描画するインデックス数
	/// @param startIndex 開始インデックスオフセット
	/// @param baseVertex ベース頂点オフセット
	virtual void drawIndexed(std::uint32_t indexCount,
	                         std::uint32_t startIndex = 0,
	                         std::int32_t baseVertex = 0) = 0;

	/// @brief 頂点のみで描画を実行する
	/// @param vertexCount 描画する頂点数
	/// @param startVertex 開始頂点オフセット
	virtual void draw(std::uint32_t vertexCount,
	                  std::uint32_t startVertex = 0) = 0;

	/// @brief ビューポートを設定する
	/// @param width ビューポート幅
	/// @param height ビューポート高さ
	virtual void setViewport(float width, float height)
	{
		/// デフォルト実装（何もしない）
		static_cast<void>(width);
		static_cast<void>(height);
	}

	/// @brief コマンドリストをリセットする（D3D12用）
	/// @details D3D12ではフレーム毎にコマンドリストのリセットが必要。
	///          DX11/Null等のバックエンドでは何もしない。
	virtual void reset() {}

	/// @brief コマンドリストを閉じる（D3D12用）
	/// @details D3D12ではコマンドリストを実行前にクローズする必要がある。
	///          DX11/Null等のバックエンドでは何もしない。
	virtual void close() {}

	/// @brief リソースバリアを発行する
	/// @param resource バリア対象のテクスチャリソース
	/// @param before 遷移前のリソース状態
	/// @param after 遷移後のリソース状態
	/// @details D3D12のリソースステート遷移バリアを発行する。
	///          DX11/Null等のバックエンドでは何もしない。
	virtual void resourceBarrier(
		ITexture* resource, ResourceState before, ResourceState after)
	{
		static_cast<void>(resource);
		static_cast<void>(before);
		static_cast<void>(after);
	}

	/// @brief ルートシグネチャを設定する（D3D12用）
	/// @param rootSignature ルートシグネチャのネイティブポインタ
	/// @details D3D12のID3D12RootSignatureを設定する。
	///          DX11/Null等のバックエンドでは何もしない。
	virtual void setRootSignature(void* rootSignature)
	{
		static_cast<void>(rootSignature);
	}

	/// @brief デスクリプタヒープを設定する（D3D12用）
	/// @param heaps デスクリプタヒープの配列
	/// @param count ヒープ数
	/// @details D3D12のSetDescriptorHeapsに対応する。
	///          DX11/Null等のバックエンドでは何もしない。
	virtual void setDescriptorHeaps(
		IDescriptorHeap* const* heaps, uint32_t count)
	{
		static_cast<void>(heaps);
		static_cast<void>(count);
	}

	/// @brief ルートデスクリプタテーブルを設定する（D3D12用）
	/// @param paramIndex ルートパラメータインデックス
	/// @param handle GPUデスクリプタハンドル
	/// @details D3D12のSetGraphicsRootDescriptorTableに対応する。
	///          DX11/Null等のバックエンドでは何もしない。
	virtual void setGraphicsRootDescriptorTable(
		uint32_t paramIndex, GpuDescriptorHandle handle)
	{
		static_cast<void>(paramIndex);
		static_cast<void>(handle);
	}

	/// @brief ルート定数バッファビューを設定する（D3D12用）
	/// @param paramIndex ルートパラメータインデックス
	/// @param gpuVirtualAddress GPUバッファの仮想アドレス
	/// @details D3D12のSetGraphicsRootConstantBufferViewに対応する。
	///          DX11/Null等のバックエンドでは何もしない。
	virtual void setGraphicsRootCBV(
		uint32_t paramIndex, uint64_t gpuVirtualAddress)
	{
		static_cast<void>(paramIndex);
		static_cast<void>(gpuVirtualAddress);
	}
};

} // namespace mitiru::gfx
