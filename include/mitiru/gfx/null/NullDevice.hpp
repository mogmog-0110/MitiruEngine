#pragma once

/// @file NullDevice.hpp
/// @brief ヌルGPUデバイス実装
/// @details 全操作がノーオペレーションのGPUデバイス。
///          ヘッドレスモードやテスト時に使用する。

#include <cstdint>
#include <memory>
#include <vector>

#include <sgc/types/Color.hpp>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/IPipeline.hpp>

namespace mitiru::gfx
{

/// @brief ヌルGPUバッファ
/// @details 全操作が何もしないバッファ実装。
class NullBuffer final : public IBuffer
{
public:
	/// @brief コンストラクタ
	/// @param bufferType バッファ種別
	/// @param sizeBytes サイズ（バイト）
	NullBuffer(BufferType bufferType, std::uint32_t sizeBytes) noexcept
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

/// @brief ヌルコマンドリスト
/// @details 全操作が何もしないコマンドリスト実装。
class NullCommandList final : public ICommandList
{
public:
	/// @brief コマンド記録を開始する
	void begin() override {}

	/// @brief コマンド記録を終了する
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

/// @brief ヌルGPUデバイス
/// @details 全操作が何もしない実装。readPixels は黒ピクセルを返す。
class NullDevice final : public IDevice
{
public:
	/// @brief フレームバッファからピクセルを読み取る
	/// @param width 読み取り幅
	/// @param height 読み取り高さ
	/// @return 全て0（黒）のRGBA8ピクセルデータ
	[[nodiscard]] std::vector<std::uint8_t> readPixels(
		int width, int height) const override
	{
		/// 全ピクセルを黒（RGBA = 0,0,0,255）で返す
		const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
		std::vector<std::uint8_t> data(pixelCount * 4, 0);

		/// アルファチャンネルを255に設定
		for (std::size_t i = 0; i < pixelCount; ++i)
		{
			data[i * 4 + 3] = 255;
		}

		return data;
	}

	/// @brief バックエンド種別
	[[nodiscard]] Backend backend() const noexcept override
	{
		return Backend::Null;
	}

	/// @brief フレーム開始（何もしない）
	void beginFrame() override
	{
	}

	/// @brief フレーム終了（何もしない）
	void endFrame() override
	{
	}

	/// @brief GPUバッファを生成する（ヌル実装）
	[[nodiscard]] std::unique_ptr<IBuffer> createBuffer(
		BufferType bufferType,
		std::uint32_t sizeBytes,
		bool /*dynamic*/,
		const void* /*initialData*/) override
	{
		return std::make_unique<NullBuffer>(bufferType, sizeBytes);
	}

	/// @brief コマンドリストを生成する（ヌル実装）
	[[nodiscard]] std::unique_ptr<ICommandList> createCommandList() override
	{
		return std::make_unique<NullCommandList>();
	}
};

} // namespace mitiru::gfx
