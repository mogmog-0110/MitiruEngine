#pragma once

/// @file CrashUpload.hpp
/// @brief 扱っていないクラッシュ報告 1 件を、作者の受け口へ multipart/form-data で送る。
/// @details minidump は breakpad の形 (upload_file_minidump) で送るので、Sentry の minidump の受け口や
///          BugSplat などの breakpad 互換の受け口にそのまま送れる。minidump には実行中のメモリが入るため、
///          送り先は https に限る (http は手元の 127.0.0.1 / localhost だけ受ける)。送るかは呼ぶ側が
///          利用者の答え (CrashInbox.hpp の decideCrashStep) で決める。

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <mitiru/debug/CrashInbox.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// onnxruntime の dml_provider_factory.h が windows.h の OPTIONAL を消すので、先に読まれていたら戻す
#ifndef OPTIONAL
#define OPTIONAL
#endif
#include <winhttp.h>
#if defined(_MSC_VER)
#pragma comment(lib, "winhttp.lib")
#endif
#endif

namespace mitiru::debug
{

struct CrashUploadTarget
{
	bool secure = false;
	std::string host;
	std::uint16_t port = 0;
	std::string path;  ///< 先頭の / と query を含む
};

/// @brief 送り先の URL を分ける。https 以外 (手元の http を除く) や読めない URL は false と error。
[[nodiscard]] inline bool parseCrashUploadUrl(std::string_view url, CrashUploadTarget& out, std::string& error)
{
	const bool https = url.rfind("https://", 0) == 0;
	const bool http = url.rfind("http://", 0) == 0;
	if (!https && !http) { error = "crashReportUrl は https:// で始める"; return false; }
	const std::string_view rest = url.substr(https ? 8 : 7);
	const std::size_t slash = rest.find('/');
	const std::string_view authority = rest.substr(0, slash);
	const std::size_t colon = authority.rfind(':');
	out.secure = https;
	out.host = std::string(authority.substr(0, colon));
	out.port = static_cast<std::uint16_t>(https ? 443 : 80);
	if (colon != std::string_view::npos)
	{
		int port = 0;
		for (const char c : authority.substr(colon + 1))
		{
			if (c < '0' || c > '9' || port > 65535) { port = -1; break; }
			port = port * 10 + (c - '0');
		}
		if (port <= 0 || port > 65535) { error = "crashReportUrl の port が読めない"; return false; }
		out.port = static_cast<std::uint16_t>(port);
	}
	out.path = slash == std::string_view::npos ? std::string("/") : std::string(rest.substr(slash));
	if (out.host.empty()) { error = "crashReportUrl に host が無い"; return false; }
	if (http && out.host != "127.0.0.1" && out.host != "localhost")
	{
		error = "minidump には実行中のメモリが入るので、crashReportUrl は https に限る";
		return false;
	}
	return true;
}

namespace detail
{

[[nodiscard]] inline std::string readBytes(const std::filesystem::path& p)
{
	std::ifstream in(p, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline void appendPart(std::string& body, std::string_view boundary, std::string_view name,
                       std::string_view filename, std::string_view type, std::string_view bytes)
{
	body += "--";
	body += boundary;
	body += "\r\nContent-Disposition: form-data; name=\"";
	body += name;
	body += "\"";
	if (!filename.empty()) { body += "; filename=\"" + std::string(filename) + "\""; }
	body += "\r\nContent-Type: ";
	body += type;
	body += "\r\n\r\n";
	body += bytes;
	body += "\r\n";
}

}  // namespace detail

/// @brief 送る本文。minidump は upload_file_minidump、報告の本文は mitiru_report、ログは mitiru_log。
[[nodiscard]] inline std::string crashUploadBody(const PendingCrash& c, std::string_view boundary)
{
	std::string body;
	detail::appendPart(body, boundary, "mitiru_report", {}, "text/plain; charset=utf-8", detail::readBytes(c.report));
	if (!c.dump.empty())
	{
		detail::appendPart(body, boundary, "upload_file_minidump", "crash.dmp", "application/octet-stream",
		                   detail::readBytes(c.dump));
	}
	if (!c.log.empty())
	{
		detail::appendPart(body, boundary, "mitiru_log", "last_run.log", "text/plain; charset=utf-8",
		                   detail::readBytes(c.log));
	}
	body += "--" + std::string(boundary) + "--\r\n";
	return body;
}

inline constexpr std::string_view kCrashUploadBoundary = "mitiru-crash-6b1f0c2e9d";

#if defined(_WIN32)

namespace detail
{

struct WinHttpHandle
{
	HINTERNET h = nullptr;
	~WinHttpHandle() { if (h != nullptr) { ::WinHttpCloseHandle(h); } }
};

[[nodiscard]] inline std::wstring widen(std::string_view s)
{
	std::wstring w(s.size(), L'\0');
	const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(),
	                                    static_cast<int>(w.size()));
	w.resize(n > 0 ? static_cast<std::size_t>(n) : 0);
	return w;
}

}  // namespace detail

/// @brief 1 件送る。受け口が 2xx を返した時だけ true。ネットワークを待つので、呼ぶ側は描画のスレッドで呼ばない。
[[nodiscard]] inline bool uploadCrash(std::string_view url, const PendingCrash& c, std::string& error)
{
	CrashUploadTarget t;
	if (!parseCrashUploadUrl(url, t, error)) { return false; }
	const std::string body = crashUploadBody(c, kCrashUploadBoundary);
	detail::WinHttpHandle session{ ::WinHttpOpen(L"MitiruEngine-crash/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
	                                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
	// 繋がらない時にゲームの終了を長く待たせない (host は終わる時に送り終わりを待つ)
	if (session.h != nullptr) { ::WinHttpSetTimeouts(session.h, 5000, 5000, 15000, 15000); }
	detail::WinHttpHandle connect{ session.h ? ::WinHttpConnect(session.h, detail::widen(t.host).c_str(), t.port, 0) : nullptr };
	detail::WinHttpHandle request{ connect.h ? ::WinHttpOpenRequest(connect.h, L"POST", detail::widen(t.path).c_str(),
		nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, t.secure ? WINHTTP_FLAG_SECURE : 0) : nullptr };
	if (request.h == nullptr) { error = "送り先に繋げない (WinHTTP " + std::to_string(::GetLastError()) + ")"; return false; }
	const std::wstring header = L"Content-Type: multipart/form-data; boundary=" + detail::widen(kCrashUploadBoundary);
	const BOOL sent = ::WinHttpSendRequest(request.h, header.c_str(), static_cast<DWORD>(-1L),
	                                       const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
	                                       static_cast<DWORD>(body.size()), 0);
	if (!sent || !::WinHttpReceiveResponse(request.h, nullptr))
	{
		error = "送れなかった (WinHTTP " + std::to_string(::GetLastError()) + ")";
		return false;
	}
	DWORD status = 0;
	DWORD size = sizeof(status);
	::WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
	                      &status, &size, WINHTTP_NO_HEADER_INDEX);
	if (status < 200 || status >= 300) { error = "受け口が " + std::to_string(status) + " を返した"; return false; }
	return true;
}

#else

[[nodiscard]] inline bool uploadCrash(std::string_view, const PendingCrash&, std::string& error)
{
	error = "クラッシュ報告の送信は Windows だけ";
	return false;
}

#endif

}  // namespace mitiru::debug
