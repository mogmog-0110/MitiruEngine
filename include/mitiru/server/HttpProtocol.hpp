#pragma once

/// @file HttpProtocol.hpp
/// @brief EngineHttpServer のハンドラが受け取るリクエストと返すレスポンス

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mitiru::server
{

/// @brief HTTP リクエスト
struct HttpRequest
{
	std::string method;                             ///< HTTPメソッド
	std::string path;                               ///< リクエストパス（クエリ文字列除去済み）
	std::string rawPath;                            ///< 生のパス（クエリ文字列含む）
	std::string body;                               ///< リクエストボディ
	std::map<std::string, std::string> params;      ///< クエリパラメータ
	std::map<std::string, std::string> headers;     ///< HTTPヘッダー
};

/// @brief HTTP レスポンス
struct HttpResponse
{
	int status = 200;                               ///< ステータスコード
	std::string contentType = "application/json";   ///< Content-Type
	std::vector<std::uint8_t> body;                 ///< レスポンスボディ（バイナリ対応）

	/// @brief 文字列ボディを設定する
	void setBody(const std::string& text)
	{
		body.assign(text.begin(), text.end());
	}
};

} // namespace mitiru::server
