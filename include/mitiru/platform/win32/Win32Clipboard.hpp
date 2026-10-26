#pragma once

/// @file Win32Clipboard.hpp
/// @brief OS のクリップボード (CF_UNICODETEXT) の読み書き

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <mitiru/platform/IClipboard.hpp>

#include <cstring>

namespace mitiru::platform
{

class Win32Clipboard final : public IClipboard
{
public:
	/// @param owner クリップボードを開く窓。書き込みは持ち主の窓が要る
	explicit Win32Clipboard(HWND owner) noexcept : m_owner(owner) {}

	bool setText(std::string_view utf8) override
	{
		const std::string crlf = toCrlf(utf8);
		const int wideLen = crlf.empty() ? 0
			: MultiByteToWideChar(CP_UTF8, 0, crlf.data(), static_cast<int>(crlf.size()), nullptr, 0);
		if (wideLen < 0 || (wideLen == 0 && !crlf.empty())) { return false; }

		HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (static_cast<SIZE_T>(wideLen) + 1) * sizeof(wchar_t));
		if (mem == nullptr) { return false; }
		auto* dst = static_cast<wchar_t*>(GlobalLock(mem));
		if (dst == nullptr) { GlobalFree(mem); return false; }
		if (wideLen > 0) { MultiByteToWideChar(CP_UTF8, 0, crlf.data(), static_cast<int>(crlf.size()), dst, wideLen); }
		dst[wideLen] = L'\0';
		GlobalUnlock(mem);

		if (!open())
		{
			GlobalFree(mem);
			return false;
		}
		bool ok = false;
		if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, mem) != nullptr)
		{
			ok = true;   // 成功すると mem は OS の持ち物になる
		}
		else
		{
			GlobalFree(mem);
		}
		CloseClipboard();
		return ok;
	}

	[[nodiscard]] std::optional<std::string> getText() override
	{
		if (!open()) { return std::nullopt; }
		std::optional<std::string> result = std::string();
		if (IsClipboardFormatAvailable(CF_UNICODETEXT))
		{
			result = std::nullopt;
			if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
			{
				if (const auto* src = static_cast<const wchar_t*>(GlobalLock(data)))
				{
					result = fromWide(src, GlobalSize(data) / sizeof(wchar_t));
					GlobalUnlock(data);
				}
			}
		}
		CloseClipboard();
		return result;
	}

private:
	// 他のプロセスが開いている間は OpenClipboard が失敗する。数回だけ待ち直す (UI の入力 1 回分なので短く)
	bool open() const noexcept
	{
		for (int attempt = 0; attempt < kOpenAttempts; ++attempt)
		{
			if (OpenClipboard(m_owner) != FALSE) { return true; }
			Sleep(kOpenWaitMs);
		}
		return false;
	}

	// 文字列の終端は OS が保証しないので、確保された大きさまでで切る
	static std::string fromWide(const wchar_t* src, std::size_t capacity)
	{
		std::size_t len = 0;
		while (len < capacity && src[len] != L'\0') { ++len; }
		if (len == 0) { return {}; }
		const int bytes = WideCharToMultiByte(CP_UTF8, 0, src, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
		if (bytes <= 0) { return {}; }
		std::string utf8(static_cast<std::size_t>(bytes), '\0');
		WideCharToMultiByte(CP_UTF8, 0, src, static_cast<int>(len), utf8.data(), bytes, nullptr, nullptr);
		return toLf(utf8);
	}

	static constexpr int kOpenAttempts = 3;
	static constexpr DWORD kOpenWaitMs = 8;

	HWND m_owner;
};

} // namespace mitiru::platform

#endif // _WIN32
