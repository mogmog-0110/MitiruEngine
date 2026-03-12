#pragma once

/// @file WebGLDevice.hpp
/// @brief WebGL2バックエンド スタブ実装
/// @details Emscripten/WebGL2環境向けのIDevice実装。
///          現時点ではスタブであり、全メソッドがノーオペレーションまたは空値を返す。
///          実際のWebGL2 API統合は将来のフェーズで行う。

#ifdef __EMSCRIPTEN__

#include <cstdint>
#include <memory>
#include <vector>

#include <sgc/types/Color.hpp>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>

namespace mitiru::gfx
{

/// @brief WebGL2用バッファ スタブ実装
/// @details 全操作がノーオペレーションのバッファ。サイズと種別のみ保持する。
class WebGLBuffer final : public IBuffer
{
public:
	/// @brief コンストラクタ
	/// @param bufferType バッファ種別
	/// @param sizeBytes サイズ（バイト）
	WebGLBuffer(BufferType bufferType, std::uint32_t sizeBytes) noexcept
		: m_type(bufferType)
		, m_size(sizeBytes)
	{
	}

	/// @brief バッファサイズを取得する
	[[nodiscard]] std::uint32_t size() const noexcept override
	{
		return m_size;
	}

	/// @brief バッファ種別を取得する
	[[nodiscard]] BufferType type() const noexcept override
	{
		return m_type;
	}

private:
	BufferType m_type;        ///< バッファ種別
	std::uint32_t m_size;     ///< サイズ（バイト）
};

/// @brief WebGL2用コマンドリスト スタブ実装
/// @details 全操作がノーオペレーションのコマンドリスト。
class WebGLCommandList final : public ICommandList
{
public:
	/// @brief コマンド記録を開始する（何もしない）
	void begin() override {}

	/// @brief コマンド記録を終了する（何もしない）
	void end() override {}

	/// @brief レンダーターゲットを設定する（何もしない）
	void setRenderTarget(IRenderTarget*) override {}

	/// @brief レンダーターゲットをクリアする（何もしない）
	void clearRenderTarget(const sgc::Colorf&) override {}

	/// @brief パイプライン状態を設定する（何もしない）
	void setPipeline(IPipeline*) override {}

	/// @brief 頂点バッファを設定する（何もしない）
	void setVertexBuffer(IBuffer*) override {}

	/// @brief インデックスバッファを設定する（何もしない）
	void setIndexBuffer(IBuffer*) override {}

	/// @brief インデックス付き描画を実行する（何もしない）
	void drawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}

	/// @brief 頂点のみで描画を実行する（何もしない）
	void draw(std::uint32_t, std::uint32_t) override {}
};

/// @brief WebGL2 GPUデバイス スタブ実装
/// @details Emscripten環境でWebGL2コンテキストを使用するデバイス。
///          現時点ではNullDeviceと同等のスタブ実装。
///          将来のフェーズでemscripten/html5.hを使用した実装に置き換える。
///
/// @code
/// auto device = std::make_unique<WebGLDevice>();
/// device->beginFrame();
/// // WebGL2描画コマンド...
/// device->endFrame();
/// @endcode
class WebGLDevice final : public IDevice
{
public:
	/// @brief フレームバッファからピクセルを読み取る
	/// @param width 読み取り幅
	/// @param height 読み取り高さ
	/// @return 全て黒（RGBA=0,0,0,255）のピクセルデータ
	/// @note 将来のフェーズでglReadPixelsを使用した実装に置き換える
	[[nodiscard]] std::vector<std::uint8_t> readPixels(
		int width, int height) const override
	{
		const auto pixelCount =
			static_cast<std::size_t>(width) *
			static_cast<std::size_t>(height);
		std::vector<std::uint8_t> data(pixelCount * 4, 0);

		/// アルファチャンネルを255に設定
		for (std::size_t i = 0; i < pixelCount; ++i)
		{
			data[i * 4 + 3] = 255;
		}

		return data;
	}

	/// @brief バックエンド種別を取得する
	/// @return Backend::WebGL
	[[nodiscard]] Backend backend() const noexcept override
	{
		return Backend::WebGL;
	}

	/// @brief フレーム開始処理（スタブ: 何もしない）
	/// @note 将来のフェーズでWebGL2コンテキストのフレーム初期化を行う
	void beginFrame() override
	{
	}

	/// @brief フレーム終了処理（スタブ: 何もしない）
	/// @note 将来のフェーズでバッファスワップを行う
	void endFrame() override
	{
	}

	/// @brief GPUバッファを生成する（スタブ実装）
	/// @param bufferType バッファ種別
	/// @param sizeBytes サイズ（バイト）
	/// @return WebGLBufferインスタンス
	[[nodiscard]] std::unique_ptr<IBuffer> createBuffer(
		BufferType bufferType,
		std::uint32_t sizeBytes,
		bool /*dynamic*/,
		const void* /*initialData*/) override
	{
		return std::make_unique<WebGLBuffer>(bufferType, sizeBytes);
	}

	/// @brief コマンドリストを生成する（スタブ実装）
	/// @return WebGLCommandListインスタンス
	[[nodiscard]] std::unique_ptr<ICommandList> createCommandList() override
	{
		return std::make_unique<WebGLCommandList>();
	}
};

} // namespace mitiru::gfx

#endif // __EMSCRIPTEN__
