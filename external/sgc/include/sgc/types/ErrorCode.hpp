#pragma once

/// @file ErrorCode.hpp
/// @brief エラーコード列挙とエラー情報構造体
///
/// 型安全なエラーコードとResult型を組み合わせたIOResult型を提供する。
/// ファイルI/O・パース・ネットワーク等の一般的なエラーカテゴリを網羅する。
///
/// @code
/// sgc::IOResult<std::string> readFile(const std::string& path)
/// {
///     if (!exists(path))
///     {
///         return {sgc::ERROR_TAG, sgc::fileNotFound("ファイルが見つかりません: " + path)};
///     }
///     return std::string{"content"};
/// }
/// @endcode

#include <cstdint>
#include <string>

#include "sgc/types/Result.hpp"

namespace sgc
{

/// @brief エラーコード列挙
///
/// 一般的なエラーカテゴリを表す列挙型。
/// ErrorInfoのcodeフィールドとして使用する。
enum class ErrorCode : uint32_t
{
	None = 0,          ///< エラーなし
	ParseError,        ///< パースエラー
	FileNotFound,      ///< ファイルが見つからない
	InvalidArgument,   ///< 無効な引数
	OutOfRange,        ///< 範囲外アクセス
	IOError,           ///< I/Oエラー
	Timeout,           ///< タイムアウト
	NotSupported,      ///< 未サポートの操作
	AlreadyExists,     ///< 既に存在する
};

/// @brief エラー情報構造体
///
/// エラーコードとメッセージ文字列のペア。
/// 等値比較はコードのみで行う（メッセージは無視）。
struct ErrorInfo
{
	ErrorCode code{ErrorCode::None};  ///< エラーコード
	std::string message;               ///< エラーメッセージ

	/// @brief 等値比較（コードのみで判定）
	/// @param other 比較対象
	/// @return コードが等しければtrue
	[[nodiscard]] bool operator==(const ErrorInfo& other) const noexcept
	{
		return code == other.code;
	}
};

/// @brief IOResult型エイリアス（ErrorInfo付きResult）
/// @tparam T 成功時の値の型
template <typename T>
using IOResult = Result<T, ErrorInfo>;

/// @brief ErrorInfoを生成するヘルパー関数
/// @param code エラーコード
/// @param message エラーメッセージ
/// @return 構築されたErrorInfo
[[nodiscard]] inline ErrorInfo makeError(ErrorCode code, std::string message)
{
	return ErrorInfo{code, std::move(message)};
}

/// @brief ParseErrorのErrorInfoを生成する
/// @param message エラーメッセージ
/// @return ErrorCode::ParseErrorのErrorInfo
[[nodiscard]] inline ErrorInfo parseError(std::string message)
{
	return ErrorInfo{ErrorCode::ParseError, std::move(message)};
}

/// @brief FileNotFoundのErrorInfoを生成する
/// @param message エラーメッセージ
/// @return ErrorCode::FileNotFoundのErrorInfo
[[nodiscard]] inline ErrorInfo fileNotFound(std::string message)
{
	return ErrorInfo{ErrorCode::FileNotFound, std::move(message)};
}

} // namespace sgc
