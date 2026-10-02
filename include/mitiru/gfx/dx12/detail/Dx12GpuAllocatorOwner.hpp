#pragma once

/// @file Dx12GpuAllocatorOwner.hpp
/// @brief デバイス 1 台分の D3D12MA アロケータと、GPU 完了待ちの返却キュー。

#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include <D3D12MemAlloc.h>

namespace mitiru::gfx::detail
{

/// @brief アロケータと、手放したがまだ GPU が読んでいるかもしれない確保の置き場。
/// @details 配置リソースはメモリを即座にアロケータへ返すと、同じ場所に置かれた次のリソースへの
///          書き込みが、まだ実行中のフレームの読み出しと重なる。そこで手放された確保は、
///          登録されたキュー全部のフェンスが「手放した時点の次のシグナル」を越えるまで保持しておく。
///          キューが 1 本も登録されていない (Dx12Device を使わない) ときはその場で返す。
///
///          ゲーム DLL も engine を header-only で取り込み、draw(Screen&) の中で host の
///          RenderPipeline2D にテクスチャを作らせる。そこでアロケータはモジュールごとの static ではなく
///          デバイスの private data から引き (findFor / findOrPublish)、確保は作ったモジュールのコードで行う
///          (createResource)。D3D12MA の Allocation は仮想関数を持ち、vtable は CreateResource を実行した
///          モジュールに置かれるので、DLL のコードで確保すると DLL を外した後の解放が消えたコードを呼ぶ。
class GpuAllocatorOwner : public std::enable_shared_from_this<GpuAllocatorOwner>
{
public:
	/// @param device allocator が AddRef しているので、この持ち主より先には消えない
	GpuAllocatorOwner(ID3D12Device* device, Microsoft::WRL::ComPtr<D3D12MA::Allocator> allocator) noexcept
		: m_device(device)
		, m_allocator(std::move(allocator))
		, m_createResource(&createResourceHere)
	{
	}

	~GpuAllocatorOwner()
	{
		std::lock_guard lock(registryMutex());
		if (publishedFor(m_device) == this) { m_device->SetPrivateData(kPrivateDataKey, 0, nullptr); }
	}

	GpuAllocatorOwner(const GpuAllocatorOwner&) = delete;
	GpuAllocatorOwner& operator=(const GpuAllocatorOwner&) = delete;

	/// @brief どのモジュールから呼んでも、device に登録済みの持ち主を返す (無ければ null)
	[[nodiscard]] static std::shared_ptr<GpuAllocatorOwner> findFor(ID3D12Device* device)
	{
		if (!device) { return nullptr; }
		std::lock_guard lock(registryMutex());
		return findLocked(device);
	}

	/// @brief device に登録済みの持ち主を返し、無ければ make で作って登録する
	template<class Make>
	[[nodiscard]] static std::shared_ptr<GpuAllocatorOwner> findOrPublish(ID3D12Device* device, Make&& make)
	{
		if (!device) { return nullptr; }
		std::lock_guard lock(registryMutex());
		if (auto alive = findLocked(device)) { return alive; }
		std::shared_ptr<GpuAllocatorOwner> owner = make(device);
		if (owner)
		{
			const GpuAllocatorOwner* raw = owner.get();
			device->SetPrivateData(kPrivateDataKey, sizeof(raw), &raw);
		}
		return owner;
	}

	[[nodiscard]] D3D12MA::Allocator* allocator() const noexcept { return m_allocator.Get(); }

	[[nodiscard]] HRESULT createResource(const D3D12MA::ALLOCATION_DESC& allocDesc,
	                                     const D3D12_RESOURCE_DESC& desc,
	                                     D3D12_RESOURCE_STATES initialState,
	                                     const D3D12_CLEAR_VALUE* clearValue,
	                                     D3D12MA::Allocation** out) const
	{
		return m_createResource(m_allocator.Get(), &allocDesc, &desc, initialState, clearValue, out);
	}

	/// @param lastSignaled キューに最後に Signal した値。手放した時点の値 + 1 を待つ。
	void attachQueue(const void* key, ID3D12Fence* fence, const std::atomic<std::uint64_t>* lastSignaled)
	{
		std::lock_guard lock(m_mutex);
		m_queues.push_back({key, fence, lastSignaled});
	}

	/// @brief 呼び出し側がキューの完了を待ち終えてから外すこと (残っている待ちはもう満たされている)
	void detachQueue(const void* key)
	{
		std::lock_guard lock(m_mutex);
		const auto it = std::find_if(m_queues.begin(), m_queues.end(),
			[key](const Queue& q) { return q.key == key; });
		if (it == m_queues.end()) { return; }
		const ID3D12Fence* fence = it->fence;
		m_queues.erase(it);
		for (auto& r : m_retired)
		{
			std::erase_if(r.waits, [fence](const Wait& w) { return w.fence == fence; });
		}
		releaseCompletedLocked();
	}

	void retire(Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation) noexcept
	{
		std::lock_guard lock(m_mutex);
		releaseCompletedLocked();
		if (m_queues.empty()) { return; }
		try
		{
			Retired r{std::move(allocation), {}};
			for (const auto& q : m_queues) { r.waits.push_back({q.fence, q.lastSignaled->load() + 1}); }
			m_retired.push_back(std::move(r));
		}
		catch (...)
		{
			// 待ち行列に積めないほど切迫しているなら、その場で返す (従来の committed と同じ扱い)
		}
	}

	void releaseCompleted()
	{
		std::lock_guard lock(m_mutex);
		releaseCompletedLocked();
	}

	[[nodiscard]] std::size_t retiredCount() const
	{
		std::lock_guard lock(m_mutex);
		return m_retired.size();
	}

private:
	using CreateResourceFn = HRESULT (*)(D3D12MA::Allocator*, const D3D12MA::ALLOCATION_DESC*,
	                                     const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
	                                     const D3D12_CLEAR_VALUE*, D3D12MA::Allocation**);

	/// 値は参照を数えない GpuAllocatorOwner*。強参照をデバイスに持たせると allocator の AddRef と
	/// 循環して消えない。このクラスの配置を変えたら鍵も変える (古い配置を知るモジュールに読ませない)。
	static constexpr GUID kPrivateDataKey =
		{0x28130997, 0x8486, 0x476b, {0xa2, 0x2e, 0xf0, 0x06, 0x93, 0x1f, 0x89, 0x57}};

	/// 同じモジュールのスレッド同士で、最後の参照が消える持ち主を引かないようにする。DLL のスレッドと
	/// host のスレッドは別の mutex になるが、DLL が描く間は Dx12Device が参照を持っている。
	[[nodiscard]] static std::mutex& registryMutex() noexcept
	{
		static std::mutex m;
		return m;
	}

	[[nodiscard]] static GpuAllocatorOwner* publishedFor(ID3D12Device* device) noexcept
	{
		GpuAllocatorOwner* owner = nullptr;
		UINT size = sizeof(owner);
		if (FAILED(device->GetPrivateData(kPrivateDataKey, &size, &owner)) || size != sizeof(owner))
		{
			return nullptr;
		}
		return owner;
	}

	[[nodiscard]] static std::shared_ptr<GpuAllocatorOwner> findLocked(ID3D12Device* device)
	{
		GpuAllocatorOwner* owner = publishedFor(device);
		return owner ? owner->weak_from_this().lock() : nullptr;
	}

	static HRESULT createResourceHere(D3D12MA::Allocator* allocator,
	                                  const D3D12MA::ALLOCATION_DESC* allocDesc,
	                                  const D3D12_RESOURCE_DESC* desc,
	                                  D3D12_RESOURCE_STATES initialState,
	                                  const D3D12_CLEAR_VALUE* clearValue,
	                                  D3D12MA::Allocation** out)
	{
		return allocator->CreateResource(allocDesc, desc, initialState, clearValue, out, IID_NULL, nullptr);
	}

	struct Queue
	{
		const void* key;
		ID3D12Fence* fence;
		const std::atomic<std::uint64_t>* lastSignaled;
	};
	struct Wait
	{
		ID3D12Fence* fence;
		std::uint64_t value;
	};
	struct Retired
	{
		Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation;
		std::vector<Wait> waits;
	};

	[[nodiscard]] static bool completed(const Retired& r)
	{
		return std::all_of(r.waits.begin(), r.waits.end(),
			[](const Wait& w) { return w.fence->GetCompletedValue() >= w.value; });
	}

	void releaseCompletedLocked()
	{
		std::erase_if(m_retired, [](const Retired& r) { return completed(r); });
	}

	ID3D12Device* m_device;
	// m_retired の確保はアロケータより先に返す必要があるので、アロケータを先に宣言する
	Microsoft::WRL::ComPtr<D3D12MA::Allocator> m_allocator;
	CreateResourceFn m_createResource;  ///< この持ち主を作ったモジュールの createResourceHere
	mutable std::mutex m_mutex;
	std::vector<Queue> m_queues;
	std::deque<Retired> m_retired;
};

} // namespace mitiru::gfx::detail

#endif // _WIN32
