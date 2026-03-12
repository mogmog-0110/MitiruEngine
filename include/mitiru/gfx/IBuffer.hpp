#pragma once

/// @file IBuffer.hpp
/// @brief GPUバッファ抽象インターフェース
/// @details 頂点バッファ・インデックスバッファ・定数バッファの基底インターフェース。

#include <cstdint>

namespace mitiru::gfx
{

/// @brief バッファ種別
enum class BufferType
{
	Vertex,   ///< 頂点バッファ
	Index,    ///< インデックスバッファ
	Constant  ///< 定数バッファ（ユニフォーム）
};

/// @brief GPUバッファの抽象インターフェース
/// @details バックエンド固有のバッファ実装がこのインターフェースを実装する。
class IBuffer
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IBuffer() = default;

	/// @brief バッファサイズを取得する
	/// @return サイズ（バイト）
	[[nodiscard]] virtual std::uint32_t size() const noexcept = 0;

	/// @brief バッファ種別を取得する
	/// @return バッファの用途種別
	[[nodiscard]] virtual BufferType type() const noexcept = 0;

	/// @brief バッファが有効かどうかを判定する
	/// @return 正常に構築されていればtrue
	[[nodiscard]] virtual bool isValid() const noexcept
	{
		return size() > 0;
	}
};

} // namespace mitiru::gfx
