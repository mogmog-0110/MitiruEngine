#pragma once

/// @file Env.hpp
/// @brief 環境変数を読む。MSVC は std::getenv に C4996 を出し、ゲーム DLL がエンジンの
///        ヘッダを /W3 でコンパイルすると利用者のビルドに警告が並ぶ。読む所をここに集め、
///        MSVC では getenv_s を使う。

#include <cstdlib>
#include <string>

namespace mitiru::env
{

/// @brief 環境変数の値。無ければ空文字列 (Windows では空の環境変数を作れないので、空と無いを分けない)
[[nodiscard]] inline std::string value(const char* name)
{
#ifdef _MSC_VER
	std::size_t len = 0;
	if (getenv_s(&len, nullptr, 0, name) != 0 || len == 0) { return {}; }
	std::string out(len, '\0');
	// 長さを聞いてから読むまでに値が変わると ERANGE になる。そのときは無いものとして扱う
	if (getenv_s(&len, out.data(), out.size(), name) != 0 || len == 0) { return {}; }
	out.resize(len - 1);
	return out;
#else
	const char* v = std::getenv(name);
	return v != nullptr ? std::string(v) : std::string();
#endif
}

/// @brief 空でなく、`0` で始まらなければ立っているとみなす (`MITIRU_D3D12_DEBUG=1` のような opt-in)
[[nodiscard]] inline bool flag(const char* name)
{
	const std::string v = value(name);
	return !v.empty() && v[0] != '0';
}

#ifdef _WIN32
/// @brief value の UTF-16 版。ANSI のコードページに無い文字を含むパスを読むときに使う
[[nodiscard]] inline std::wstring wideValue(const wchar_t* name)
{
	std::size_t len = 0;
	if (_wgetenv_s(&len, nullptr, 0, name) != 0 || len == 0) { return {}; }
	std::wstring out(len, L'\0');
	if (_wgetenv_s(&len, out.data(), out.size(), name) != 0 || len == 0) { return {}; }
	out.resize(len - 1);
	return out;
}
#endif

} // namespace mitiru::env
