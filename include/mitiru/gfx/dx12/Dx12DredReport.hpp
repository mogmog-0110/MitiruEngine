#pragma once

/// @file Dx12DredReport.hpp
/// @brief device-removed の理由と DRED の breadcrumbs、page fault を stderr に出す
/// @details debug layer は描画タイミングを変えて資源解放の競合を隠すことがあるため、GPU 側の足取りだけを記録する DRED を使う。
///          `MITIRU_D3D12_DRED=1` で有効にし、喪失時に `report()` を呼ぶ。Debug の host は対話起動でこれを既定で立てる (#79)。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace mitiru::gfx::dred
{

/// @brief 環境変数が空でなく、`0` でもないか
[[nodiscard]] inline bool envEnabled(const char* name) noexcept
{
	const char* e = std::getenv(name);
	return e != nullptr && e[0] != '\0' && e[0] != '0';
}

/// @brief D3D12CreateDevice より前に呼ぶ。`MITIRU_D3D12_DRED` が立っていなければ何もしない
inline void enableIfRequested()
{
	if (!envEnabled("MITIRU_D3D12_DRED")) { return; }
	Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> settings;
	if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(settings.GetAddressOf()))))
	{
		std::fprintf(stderr, "[mitiru] MITIRU_D3D12_DRED=1 だが DRED 設定インターフェースが取れない (SDK レイヤ未導入?)\n");
		return;
	}
	settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
	settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
	bool contexts = false;
#ifdef __ID3D12DeviceRemovedExtendedDataSettings1_INTERFACE_DEFINED__
	// 3D は 1 本のリストに全パスを積むので、op の番号だけではどのパスか分からない。SetMarker の文字列を残させる
	Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings1;
	if (SUCCEEDED(settings.As(&settings1)))
	{
		settings1->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		contexts = true;
	}
#endif
	std::fprintf(stderr, "[mitiru] DRED 有効 (auto breadcrumbs%s + page fault。MITIRU_D3D12_DRED=0 で外す)\n",
		contexts ? " + パス名" : "");
}

/// @brief DRED の breadcrumb にパス名を残す。DRED が無効なら何も残らない (op 1 個ぶんの記録だけ)
inline void markPass(ID3D12GraphicsCommandList* cl, const wchar_t* name) noexcept
{
	if (cl == nullptr || name == nullptr) { return; }
	cl->SetMarker(0, name, static_cast<UINT>((std::wcslen(name) + 1) * sizeof(wchar_t)));
}

/// @brief このリストでは n 番目の SetMarker が names[n] であることを、報告用に登録する
/// @details SetMarker の文字列 (DRED 1.2 の context) を残さないドライバでも、番号から名前を引けるようにする。
///          表は静的な寿命のものを渡す。今の利用者は `Renderer3D main` の 1 本だけなので 1 枠で足りる。
struct PassOrder
{
	const wchar_t* listName = nullptr;
	const wchar_t* const* names = nullptr;
	int count = 0;
};

[[nodiscard]] inline PassOrder& passOrderSlot() noexcept
{
	static PassOrder order;
	return order;
}

inline void registerPassOrder(const wchar_t* listName, const wchar_t* const* names, int count) noexcept
{
	passOrderSlot() = PassOrder{listName, names, count};
}

/// @brief `%ls` は C ロケールで ASCII 外の文字に当たるとそこで出力が切れるので、UTF-8 に直して出す
struct Utf8Name
{
	char text[160] = {};
	explicit Utf8Name(const wchar_t* w) noexcept
	{
		if (w == nullptr) { std::snprintf(text, sizeof(text), "(無名)"); return; }
		const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, text, static_cast<int>(sizeof(text)) - 1, nullptr, nullptr);
		if (n <= 0) { std::snprintf(text, sizeof(text), "(読めない名前)"); }
		text[sizeof(text) - 1] = '\0';
	}
};

[[nodiscard]] inline const char* removedReasonName(HRESULT hr) noexcept
{
	switch (hr)
	{
	case DXGI_ERROR_DEVICE_HUNG:           return "DXGI_ERROR_DEVICE_HUNG";
	case DXGI_ERROR_DEVICE_REMOVED:        return "DXGI_ERROR_DEVICE_REMOVED";
	case DXGI_ERROR_DEVICE_RESET:          return "DXGI_ERROR_DEVICE_RESET";
	case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
	case DXGI_ERROR_INVALID_CALL:          return "DXGI_ERROR_INVALID_CALL";
	case DXGI_ERROR_ACCESS_DENIED:         return "DXGI_ERROR_ACCESS_DENIED";
	case E_OUTOFMEMORY:                    return "E_OUTOFMEMORY";
	case S_OK:                             return "S_OK (removed ではない。フェンスが進まないだけならドライバ側の hang)";
	default:                              return "(unknown)";
	}
}

[[nodiscard]] inline const char* breadcrumbOpName(D3D12_AUTO_BREADCRUMB_OP op) noexcept
{
	switch (op)
	{
	case D3D12_AUTO_BREADCRUMB_OP_SETMARKER:                return "SetMarker";
	case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT:               return "BeginEvent";
	case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT:                 return "EndEvent";
	case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:            return "DrawInstanced";
	case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED:     return "DrawIndexedInstanced";
	case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT:          return "ExecuteIndirect";
	case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:                 return "Dispatch";
	case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:         return "CopyBufferRegion";
	case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:        return "CopyTextureRegion";
	case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:             return "CopyResource";
	case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE:       return "ResolveSubresource";
	case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW:    return "ClearRenderTargetView";
	case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW: return "ClearUnorderedAccessView";
	case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:    return "ClearDepthStencilView";
	case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:          return "ResourceBarrier";
	case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE:            return "ExecuteBundle";
	case D3D12_AUTO_BREADCRUMB_OP_PRESENT:                  return "Present";
	case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA:         return "ResolveQueryData";
	case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION:          return "BeginSubmission";
	case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION:            return "EndSubmission";
	case D3D12_AUTO_BREADCRUMB_OP_WRITEBUFFERIMMEDIATE:     return "WriteBufferImmediate";
	default:                                                return "(other)";
	}
}

[[nodiscard]] inline const char* allocationTypeName(D3D12_DRED_ALLOCATION_TYPE t) noexcept
{
	switch (t)
	{
	case D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE:      return "CommandQueue";
	case D3D12_DRED_ALLOCATION_TYPE_COMMAND_ALLOCATOR:  return "CommandAllocator";
	case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_STATE:     return "PipelineState";
	case D3D12_DRED_ALLOCATION_TYPE_COMMAND_LIST:       return "CommandList";
	case D3D12_DRED_ALLOCATION_TYPE_FENCE:              return "Fence";
	case D3D12_DRED_ALLOCATION_TYPE_DESCRIPTOR_HEAP:    return "DescriptorHeap";
	case D3D12_DRED_ALLOCATION_TYPE_HEAP:               return "Heap";
	case D3D12_DRED_ALLOCATION_TYPE_QUERY_HEAP:         return "QueryHeap";
	case D3D12_DRED_ALLOCATION_TYPE_COMMAND_SIGNATURE:  return "CommandSignature";
	case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_LIBRARY:   return "PipelineLibrary";
	case D3D12_DRED_ALLOCATION_TYPE_RESOURCE:           return "Resource";
	case D3D12_DRED_ALLOCATION_TYPE_PASS:               return "Pass";
	case D3D12_DRED_ALLOCATION_TYPE_COMMAND_POOL:       return "CommandPool";
	case D3D12_DRED_ALLOCATION_TYPE_COMMAND_RECORDER:   return "CommandRecorder";
	case D3D12_DRED_ALLOCATION_TYPE_STATE_OBJECT:       return "StateObject";
	case D3D12_DRED_ALLOCATION_TYPE_METACOMMAND:        return "MetaCommand";
	case D3D12_DRED_ALLOCATION_TYPE_SCHEDULINGGROUP:    return "SchedulingGroup";
	default:                                            return "(other)";
	}
}

inline void printAllocationList(const char* title, const D3D12_DRED_ALLOCATION_NODE* node)
{
	std::fprintf(stderr, "[mitiru]   %s:\n", title);
	if (!node) { std::fprintf(stderr, "[mitiru]     (なし)\n"); return; }
	int shown = 0;
	for (; node && shown < 32; node = node->pNext, ++shown)
	{
		std::fprintf(stderr, "[mitiru]     %-16s %s\n", allocationTypeName(node->AllocationType),
			node->ObjectNameA ? node->ObjectNameA : "(無名)");
	}
	if (node) { std::fprintf(stderr, "[mitiru]     ... (省略)\n"); }
}

/// @brief breadcrumbs の 1 ノード。DRED 1.2 のパス名 (contexts) が無い環境では contexts が空
struct BreadcrumbView
{
	const wchar_t* listName = nullptr;
	const wchar_t* queueName = nullptr;
	UINT count = 0;
	UINT last = 0;
	const D3D12_AUTO_BREADCRUMB_OP* history = nullptr;
	UINT contextCount = 0;
	const D3D12_DRED_BREADCRUMB_CONTEXT* contexts = nullptr;
};

[[nodiscard]] inline const wchar_t* contextAt(const BreadcrumbView& v, UINT index) noexcept
{
	for (UINT i = 0; i < v.contextCount; ++i)
	{
		if (v.contexts[i].BreadcrumbIndex == index) { return v.contexts[i].pContextString; }
	}
	return nullptr;
}

/// @brief op index の SetMarker の名前。文字列が残っていればそれを、無ければ登録された順番表から引く
[[nodiscard]] inline const wchar_t* markerName(const BreadcrumbView& v, UINT index, int ordinal) noexcept
{
	if (const wchar_t* s = contextAt(v, index)) { return s; }
	const PassOrder& order = passOrderSlot();
	if (order.names == nullptr || v.listName == nullptr || std::wcscmp(order.listName, v.listName) != 0) { return nullptr; }
	return (ordinal >= 0 && ordinal < order.count) ? order.names[ordinal] : nullptr;
}

/// @brief 止まった op より前で最後に置かれた markPass の名前
[[nodiscard]] inline const wchar_t* passAtStop(const BreadcrumbView& v) noexcept
{
	if (!v.history) { return nullptr; }
	const wchar_t* pass = nullptr;
	int ordinal = 0;
	for (UINT i = 0; i <= v.last && i < v.count; ++i)
	{
		if (v.history[i] != D3D12_AUTO_BREADCRUMB_OP_SETMARKER) { continue; }
		if (const wchar_t* s = markerName(v, i, ordinal)) { pass = s; }
		++ordinal;
	}
	return pass;
}

inline void printBreadcrumbNode(const BreadcrumbView& v, bool detail)
{
	const bool done = v.last >= v.count;
	const char* state = done ? "全部完了"
		: (v.last == 0 ? "未着手 (前のリストの完了待ち)" : "← 実行中に止まった");
	const wchar_t* pass = (done || v.last == 0) ? nullptr : passAtStop(v);
	std::fprintf(stderr, "[mitiru]   cmdlist=%s queue=%s ops=%u 完了=%u (文字列 %u 件) %s%s%s\n",
		Utf8Name(v.listName).text, Utf8Name(v.queueName).text, v.count, v.last, v.contextCount, state,
		pass ? " パス=" : "", pass ? Utf8Name(pass).text : "");
	if (!detail || !v.history || done) { return; }
	const UINT from = (v.last > 12) ? v.last - 12 : 0;
	const UINT to   = (v.last + 4 < v.count) ? v.last + 4 : v.count;
	int ordinal = 0;
	for (UINT i = 0; i < from; ++i)
	{
		if (v.history[i] == D3D12_AUTO_BREADCRUMB_OP_SETMARKER) { ++ordinal; }
	}
	for (UINT i = from; i < to; ++i)
	{
		const bool marker = v.history[i] == D3D12_AUTO_BREADCRUMB_OP_SETMARKER;
		const wchar_t* name = marker ? markerName(v, i, ordinal) : contextAt(v, i);
		std::fprintf(stderr, "[mitiru]     %s[%u] %s%s%s\n", (i == v.last) ? "*" : " ", i,
			breadcrumbOpName(v.history[i]), name ? " " : "", name ? Utf8Name(name).text : "");
		if (marker) { ++ordinal; }
	}
}

/// @brief ノード列を古い順に受け取り、末尾の数本を 1 行ずつ、完了していないものは op 列も出す
inline void printBreadcrumbs(const BreadcrumbView* views, int n)
{
	constexpr int kShownLists = 12;
	constexpr int kDetailedLists = 3;
	std::fprintf(stderr, "[mitiru]   DRED breadcrumbs (%d 本、古い順。最後の %d 本):\n", n, kShownLists);
	int detailed = 0;
	for (int i = (n > kShownLists ? n - kShownLists : 0); i < n; ++i)
	{
		const bool pending = views[i].last < views[i].count;
		const bool detail = pending && detailed < kDetailedLists;
		if (detail) { ++detailed; }
		printBreadcrumbNode(views[i], detail);
	}
}

inline void reportBreadcrumbs(ID3D12Device* device)
{
	constexpr int kMaxNodes = 64;
	BreadcrumbView views[kMaxNodes] = {};
	int n = 0;
#ifdef __ID3D12DeviceRemovedExtendedData1_INTERFACE_DEFINED__
	Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred1;
	D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 crumbs1 = {};
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(dred1.GetAddressOf())))
		&& SUCCEEDED(dred1->GetAutoBreadcrumbsOutput1(&crumbs1)))
	{
		// 先頭から数えると古いノードで上限に達するので、ring として最後の kMaxNodes 本を残す
		int total = 0;
		for (const auto* p = crumbs1.pHeadAutoBreadcrumbNode; p; p = p->pNext, ++total)
		{
			views[total % kMaxNodes] = { p->pCommandListDebugNameW, p->pCommandQueueDebugNameW,
				p->BreadcrumbCount, p->pLastBreadcrumbValue ? *p->pLastBreadcrumbValue : 0,
				p->pCommandHistory, p->BreadcrumbContextsCount, p->pBreadcrumbContexts };
		}
		n = total < kMaxNodes ? total : kMaxNodes;
		if (total > kMaxNodes)
		{
			BreadcrumbView ordered[kMaxNodes];
			for (int i = 0; i < kMaxNodes; ++i) { ordered[i] = views[(total + i) % kMaxNodes]; }
			for (int i = 0; i < kMaxNodes; ++i) { views[i] = ordered[i]; }
		}
		if (n == 0) { std::fprintf(stderr, "[mitiru]   DRED breadcrumbs: 記録なし\n"); return; }
		printBreadcrumbs(views, n);
		return;
	}
#endif
	Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
	D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT crumbs = {};
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(dred.GetAddressOf())))
		|| FAILED(dred->GetAutoBreadcrumbsOutput(&crumbs)) || !crumbs.pHeadAutoBreadcrumbNode)
	{
		std::fprintf(stderr, "[mitiru]   DRED breadcrumbs: 取得できず\n");
		return;
	}
	for (const auto* p = crumbs.pHeadAutoBreadcrumbNode; p && n < kMaxNodes; p = p->pNext, ++n)
	{
		views[n] = { p->pCommandListDebugNameW, p->pCommandQueueDebugNameW, p->BreadcrumbCount,
			p->pLastBreadcrumbValue ? *p->pLastBreadcrumbValue : 0, p->pCommandHistory, 0, nullptr };
	}
	printBreadcrumbs(views, n);
}

/// @brief device-removed の理由と DRED を stderr に出す。DRED が無効なら理由だけを出す
inline void report(ID3D12Device* device, HRESULT reason)
{
	std::fprintf(stderr, "[mitiru] GetDeviceRemovedReason = 0x%08lX %s\n",
		static_cast<unsigned long>(reason), removedReasonName(reason));
	if (!device) { return; }

	Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(dred.GetAddressOf()))))
	{
		std::fprintf(stderr, "[mitiru]   DRED なし (MITIRU_D3D12_DRED=1 で breadcrumbs / page fault が取れる)\n");
		return;
	}

	reportBreadcrumbs(device);

	D3D12_DRED_PAGE_FAULT_OUTPUT fault = {};
	if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&fault)))
	{
		std::fprintf(stderr, "[mitiru]   page fault VA = 0x%016llX\n",
			static_cast<unsigned long long>(fault.PageFaultVA));
		printAllocationList("VA を含む生存中の割り当て", fault.pHeadExistingAllocationNode);
		printAllocationList("VA を含む最近解放された割り当て", fault.pHeadRecentFreedAllocationNode);
	}
	else
	{
		std::fprintf(stderr, "[mitiru]   page fault 情報: 取得できず (page fault ではない、か DRED 無効)\n");
	}
	std::fflush(stderr);
}

} // namespace mitiru::gfx::dred

#endif // _WIN32
