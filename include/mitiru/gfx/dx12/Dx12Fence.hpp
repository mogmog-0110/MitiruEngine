#pragma once

/// @file Dx12Fence.hpp
/// @brief DirectX 12 GPUフェンス実装
/// @details ID3D12Fenceをラップし、CPU-GPU間同期を提供するIGpuFence実装。
///          トリプルバッファリングのフレーム同期に使用する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <stdexcept>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/IGpuFence.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 12 GPUフェンス実装
/// @details ID3D12Fenceをラップし、フェンス値によるCPU-GPU同期を実現する。
///
/// @code
/// auto fence = device->createFence();
/// fence->signal(1);
/// fence->waitForValue(1);
/// @endcode
class Dx12Fence final : public IGpuFence
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param device D3D12デバイス
	/// @param commandQueue コマンドキュー（シグナル発行用）
	/// @param initialValue フェンスの初期値
	Dx12Fence(ID3D12Device* device,
	          ID3D12CommandQueue* commandQueue,
	          FenceValue initialValue = 0)
		: m_commandQueue(commandQueue)
	{
		if (!device || !commandQueue)
		{
			throw std::runtime_error(
				"Dx12Fence: device or commandQueue is null");
		}

		HRESULT hr = device->CreateFence(
			initialValue,
			D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(m_fence.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Fence: CreateFence failed");
		}

		m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!m_fenceEvent)
		{
			throw std::runtime_error(
				"Dx12Fence: CreateEvent failed");
		}
	}

	/// @brief デストラクタ
	~Dx12Fence() override
	{
		if (m_fenceEvent)
		{
			CloseHandle(m_fenceEvent);
			m_fenceEvent = nullptr;
		}
	}

	/// コピー禁止
	Dx12Fence(const Dx12Fence&) = delete;
	Dx12Fence& operator=(const Dx12Fence&) = delete;

	/// ムーブ許可
	Dx12Fence(Dx12Fence&& other) noexcept
		: m_fence(std::move(other.m_fence))
		, m_commandQueue(other.m_commandQueue)
		, m_fenceEvent(other.m_fenceEvent)
	{
		other.m_commandQueue = nullptr;
		other.m_fenceEvent = nullptr;
	}

	Dx12Fence& operator=(Dx12Fence&& other) noexcept
	{
		if (this != &other)
		{
			if (m_fenceEvent)
			{
				CloseHandle(m_fenceEvent);
			}
			m_fence = std::move(other.m_fence);
			m_commandQueue = other.m_commandQueue;
			m_fenceEvent = other.m_fenceEvent;
			other.m_commandQueue = nullptr;
			other.m_fenceEvent = nullptr;
		}
		return *this;
	}

	/// @brief 現在のフェンス値を取得する
	/// @return GPU側が完了した最新のフェンス値
	[[nodiscard]] FenceValue currentValue() const override
	{
		return m_fence->GetCompletedValue();
	}

	/// @brief コマンドキューにフェンスシグナルを発行する
	/// @param value シグナルするフェンス値
	void signal(FenceValue value) override
	{
		HRESULT hr = m_commandQueue->Signal(m_fence.Get(), value);
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Fence: Signal failed");
		}
	}

	/// @brief 指定したフェンス値の完了を待機する
	/// @param value 待機するフェンス値
	void waitForValue(FenceValue value) override
	{
		if (m_fence->GetCompletedValue() < value)
		{
			HRESULT hr = m_fence->SetEventOnCompletion(value, m_fenceEvent);
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"Dx12Fence: SetEventOnCompletion failed");
			}
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}
	}

	/// @brief 指定したフェンス値が完了済みかどうかを判定する
	/// @param value 判定するフェンス値
	/// @return 完了していればtrue
	[[nodiscard]] bool isComplete(FenceValue value) const override
	{
		return m_fence->GetCompletedValue() >= value;
	}

	/// @brief 内部のID3D12Fenceを取得する
	/// @return フェンスへのポインタ
	[[nodiscard]] ID3D12Fence* nativeFence() const noexcept
	{
		return m_fence.Get();
	}

private:
	ComPtr<ID3D12Fence> m_fence;             ///< D3D12フェンス
	ID3D12CommandQueue* m_commandQueue;       ///< コマンドキュー（非所有）
	HANDLE m_fenceEvent = nullptr;            ///< CPU待機用イベント
};

} // namespace mitiru::gfx

#endif // _WIN32
