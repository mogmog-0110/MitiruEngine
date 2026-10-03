#pragma once

/// @file Dx12CopyQueue.hpp
/// @brief 読み込みの転送を描画のキューから外して流す COPY キュー。どのスレッドからでも積める
/// @details 転送を記録したリストは提出のたびにフェンスを進め、その値を返す。ワーカーは値を待ってから
///          結果を描画側へ渡すので、描画側は COPY キューを待たずに資源を使える。COPY キューで触った資源は
///          提出の完了で COMMON に戻るので、描画側は COMMON からの遷移を自分のリストに積む。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/dx12/Dx12FenceWait.hpp>

namespace mitiru::render::dx12
{

class Dx12CopyQueue
{
public:
	Dx12CopyQueue() = default;
	Dx12CopyQueue(const Dx12CopyQueue&) = delete;
	Dx12CopyQueue& operator=(const Dx12CopyQueue&) = delete;
	~Dx12CopyQueue() { shutdown(); }

	[[nodiscard]] bool init(ID3D12Device* device)
	{
		shutdown();
		if (device == nullptr) { return false; }
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
		if (FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(m_queue.GetAddressOf()))) ||
		    FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.GetAddressOf()))))
		{
			m_queue.Reset();
			m_fence.Reset();
			return false;
		}
		m_queue->SetName(L"mitiru streaming copy");
		m_device = device;
		return true;
	}

	[[nodiscard]] bool ready() const noexcept { return m_queue != nullptr; }

	/// @brief record(list) が積んだ転送を提出する。記録と提出は 1 本ずつ進むので、記録は転送の命令だけにする
	/// @return 待つフェンスの値。失敗は 0
	template <typename Record>
	[[nodiscard]] std::uint64_t submit(Record&& record)
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_queue) { return 0; }
		Slot* slot = freeSlot();
		if (slot == nullptr ||
		    FAILED(slot->allocator->Reset()) || FAILED(slot->list->Reset(slot->allocator.Get(), nullptr)))
		{
			return 0;
		}
		record(slot->list.Get());
		if (FAILED(slot->list->Close())) { return 0; }
		ID3D12CommandList* lists[] = {slot->list.Get()};
		m_queue->ExecuteCommandLists(1, lists);
		const std::uint64_t value = ++m_lastSubmitted;
		if (FAILED(m_queue->Signal(m_fence.Get(), value))) { return 0; }
		slot->fence = value;
		return value;
	}

	[[nodiscard]] bool isComplete(std::uint64_t value) const noexcept
	{
		return m_fence != nullptr && m_fence->GetCompletedValue() >= value;
	}

	/// @brief どのスレッドからでも待てる。装置が失われたか時間切れなら false
	[[nodiscard]] bool wait(std::uint64_t value) const
	{
		if (value == 0 || m_fence == nullptr) { return false; }
		if (isComplete(value)) { return true; }
		const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (event == nullptr) { return false; }
		gfx::FenceWaitBounds bounds = gfx::kWarpFenceWait;
		bounds.keepWindowResponsive = false;
		const auto r = gfx::waitForFenceBounded(m_fence.Get(), value, event, m_device, bounds);
		CloseHandle(event);
		return r == gfx::FenceWaitResult::Completed;
	}

	void waitIdle() const
	{
		std::uint64_t last = 0;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			last = m_lastSubmitted;
		}
		if (last > 0) { (void)wait(last); }
	}

	void shutdown()
	{
		if (m_queue) { waitIdle(); }
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_slots.clear();
		m_fence.Reset();
		m_queue.Reset();
		m_device = nullptr;
		m_lastSubmitted = 0;
	}

	/// @brief 提出した回数 (テストと計測用)
	[[nodiscard]] std::uint64_t submittedCount() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_lastSubmitted;
	}

private:
	struct Slot
	{
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    allocator;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
		std::uint64_t                                     fence = 0;
	};

	/// @brief GPU が使い終えたアロケータを使い回す。全部使用中なら 1 組足す
	[[nodiscard]] Slot* freeSlot()
	{
		const std::uint64_t done = m_fence->GetCompletedValue();
		for (auto& s : m_slots)
		{
			if (s->fence <= done) { return s.get(); }
		}
		auto slot = std::make_unique<Slot>();
		if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,
		                                            IID_PPV_ARGS(slot->allocator.GetAddressOf()))) ||
		    FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, slot->allocator.Get(), nullptr,
		                                       IID_PPV_ARGS(slot->list.GetAddressOf()))) ||
		    FAILED(slot->list->Close()))
		{
			return nullptr;
		}
		m_slots.push_back(std::move(slot));
		return m_slots.back().get();
	}

	mutable std::mutex                          m_mutex;
	ID3D12Device*                               m_device = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue>  m_queue;
	Microsoft::WRL::ComPtr<ID3D12Fence>         m_fence;
	std::vector<std::unique_ptr<Slot>>          m_slots;
	std::uint64_t                               m_lastSubmitted = 0;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
