#pragma once

/// @file UiaRoot.hpp
/// @brief 窓に UI Automation の根の提供者を 1 つ付け、字幕などの文を「通知」として読み上げソフト (Narrator) に渡す。
/// @details 木は持たない (段階 1)。窓の名前と役割を答え、UiaRaiseNotificationEvent で文を送るだけ。
///          UIAutomationCore.dll は実行時に読む: 通知の入口は Windows 10 1709 以降にしか無く、無い PC でも
///          ゲームは起動し、読み上げが出ないだけにするため。読み上げソフトなどの UIA の客が居ない間は、通知を作らない。
///          提供者の関数は UIA の都合のスレッドから呼ばれるので、名前は鍵で守り、参照数は atomic で数える。

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <uiautomationcore.h>
#include <uiautomationcoreapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <mutex>
#include <string>
#include <string_view>

namespace mitiru::platform
{

// UIAutomationClient.h の定数。あちらは客側の巨大なヘッダーなので、提供者が使う 3 つだけを写す
inline constexpr PROPERTYID kUiaControlTypeProperty = 30003;
inline constexpr PROPERTYID kUiaNameProperty = 30005;
inline constexpr long kUiaWindowControlType = 50032;

/// UIAutomationCore.dll の入口。無い物は nullptr のまま。
struct UiaApi
{
	using ReturnProviderFn = LRESULT(WINAPI*)(HWND, WPARAM, LPARAM, IRawElementProviderSimple*);
	using HostProviderFn = HRESULT(WINAPI*)(HWND, IRawElementProviderSimple**);
	using ListeningFn = BOOL(WINAPI*)();
	using RaiseFn = HRESULT(WINAPI*)(IRawElementProviderSimple*, NotificationKind, NotificationProcessing, BSTR, BSTR);
	using DisconnectFn = HRESULT(WINAPI*)(IRawElementProviderSimple*);

	ReturnProviderFn returnProvider = nullptr;
	HostProviderFn hostProvider = nullptr;
	ListeningFn clientsAreListening = nullptr;
	RaiseFn raiseNotification = nullptr;
	DisconnectFn disconnect = nullptr;

	[[nodiscard]] bool usable() const noexcept
	{
		return returnProvider != nullptr && hostProvider != nullptr && clientsAreListening != nullptr
		    && raiseNotification != nullptr;
	}

	[[nodiscard]] static const UiaApi& get() noexcept
	{
		static const UiaApi api = load();
		return api;
	}

private:
	static UiaApi load() noexcept
	{
		UiaApi a;
		const HMODULE dll = ::LoadLibraryW(L"UIAutomationCore.dll");
		if (dll == nullptr) { return a; }
		a.returnProvider = reinterpret_cast<ReturnProviderFn>(::GetProcAddress(dll, "UiaReturnRawElementProvider"));
		a.hostProvider = reinterpret_cast<HostProviderFn>(::GetProcAddress(dll, "UiaHostProviderFromHwnd"));
		a.clientsAreListening = reinterpret_cast<ListeningFn>(::GetProcAddress(dll, "UiaClientsAreListening"));
		a.raiseNotification = reinterpret_cast<RaiseFn>(::GetProcAddress(dll, "UiaRaiseNotificationEvent"));
		a.disconnect = reinterpret_cast<DisconnectFn>(::GetProcAddress(dll, "UiaDisconnectProvider"));
		return a;
	}
};

namespace detail
{

[[nodiscard]] inline std::wstring utf8ToWide(std::string_view s)
{
	if (s.empty()) { return {}; }
	const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	if (n <= 0) { return {}; }
	std::wstring out(static_cast<std::size_t>(n), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
	return out;
}

/// 窓全体を表す根の提供者。子は持たない。参照数は WRL が atomic で数える。
class UiaRootProvider final
	: public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
	                                      IRawElementProviderSimple>
{
public:
	HRESULT RuntimeClassInitialize(HWND hwnd, std::wstring name)
	{
		m_hwnd = hwnd;
		m_name = std::move(name);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) override
	{
		if (options == nullptr) { return E_POINTER; }
		*options = ProviderOptions_ServerSideProvider;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** out) override
	{
		if (out == nullptr) { return E_POINTER; }
		*out = nullptr;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* out) override
	{
		if (out == nullptr) { return E_POINTER; }
		VariantInit(out);
		if (id == kUiaControlTypeProperty)
		{
			out->vt = VT_I4;
			out->lVal = kUiaWindowControlType;
		}
		else if (id == kUiaNameProperty)
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			out->vt = VT_BSTR;
			out->bstrVal = ::SysAllocStringLen(m_name.data(), static_cast<UINT>(m_name.size()));
		}
		// 答えない性質は VT_EMPTY のまま返し、窓の既定の答え (OS が窓から作る) に任せる
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override
	{
		if (out == nullptr) { return E_POINTER; }
		const UiaApi& api = UiaApi::get();
		*out = nullptr;
		return api.hostProvider != nullptr ? api.hostProvider(m_hwnd, out) : S_OK;
	}

	void setName(std::wstring name)
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_name = std::move(name);
	}

private:
	HWND m_hwnd = nullptr;
	std::mutex m_mutex;
	std::wstring m_name;
};

}  // namespace detail

enum class UiaPoliteness { Polite, Assertive };

class UiaRoot
{
public:
	UiaRoot() = default;
	~UiaRoot() { detach(); }
	UiaRoot(const UiaRoot&) = delete;
	UiaRoot& operator=(const UiaRoot&) = delete;

	/// @brief hwnd の根の提供者を作る。UIA が使えない OS では false で、以後の呼び出しは何もしない。
	bool attach(HWND hwnd, std::wstring_view name)
	{
		detach();
		if (hwnd == nullptr || !UiaApi::get().usable()) { return false; }
		if (FAILED(Microsoft::WRL::MakeAndInitialize<detail::UiaRootProvider>(m_provider.ReleaseAndGetAddressOf(), hwnd,
		                                                                      std::wstring(name))))
		{
			return false;
		}
		m_hwnd = hwnd;
		return true;
	}

	void detach() noexcept
	{
		if (m_provider == nullptr) { return; }
		const UiaApi& api = UiaApi::get();
		if (m_hwnd != nullptr && ::IsWindow(m_hwnd)) { api.returnProvider(m_hwnd, 0, 0, nullptr); }
		if (api.disconnect != nullptr) { api.disconnect(m_provider.Get()); }
		m_provider.Reset();
		m_hwnd = nullptr;
	}

	[[nodiscard]] bool attached() const noexcept { return m_provider != nullptr; }
	[[nodiscard]] HWND window() const noexcept { return m_hwnd; }

	void setName(std::wstring_view name)
	{
		if (m_provider != nullptr) { m_provider->setName(std::wstring(name)); }
	}

	/// @brief 窓手続きの WM_GETOBJECT から呼ぶ。UIA の根の問い合わせだけ答え、MSAA など他の問い合わせは OS の既定に返す。
	[[nodiscard]] LRESULT handleGetObject(HWND hwnd, WPARAM wParam, LPARAM lParam) const
	{
		// lParam の下位 32 bit を符号つきで比べる。UiaRootObjectId は負の値
		if (m_provider != nullptr && static_cast<LONG>(lParam) == static_cast<LONG>(UiaRootObjectId))
		{
			return UiaApi::get().returnProvider(hwnd, wParam, lParam, m_provider.Get());
		}
		return ::DefWindowProcW(hwnd, WM_GETOBJECT, wParam, lParam);
	}

	/// @brief 読み上げソフトなどの UIA の客が 1 つでも居るか。居なければ通知を作らない
	[[nodiscard]] static bool clientsListening() noexcept
	{
		const UiaApi& api = UiaApi::get();
		return api.clientsAreListening != nullptr && api.clientsAreListening() != FALSE;
	}

	/// @brief 文を通知として送る。polite は次の文が来たら古い方を捨て、assertive は読み上げ中でも割り込む。
	/// @return 送れたら true。客が居ない・UIA が無い時は false
	bool announce(std::string_view utf8, UiaPoliteness politeness, std::string_view activity)
	{
		const UiaApi& api = UiaApi::get();
		if (m_provider == nullptr || utf8.empty() || !clientsListening()) { return false; }
		const std::wstring text = detail::utf8ToWide(utf8);
		const std::wstring act = detail::utf8ToWide(activity);
		BSTR bText = ::SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
		BSTR bAct = ::SysAllocStringLen(act.data(), static_cast<UINT>(act.size()));
		const NotificationProcessing mode = politeness == UiaPoliteness::Assertive
			? NotificationProcessing_ImportantMostRecent : NotificationProcessing_MostRecent;
		const HRESULT hr = api.raiseNotification(m_provider.Get(), NotificationKind_Other, mode, bText, bAct);
		::SysFreeString(bText);
		::SysFreeString(bAct);
		return SUCCEEDED(hr);
	}

private:
	Microsoft::WRL::ComPtr<detail::UiaRootProvider> m_provider;
	HWND m_hwnd = nullptr;
};

}  // namespace mitiru::platform
