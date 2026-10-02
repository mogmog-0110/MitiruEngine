// mitiru::Engine の detail header。直接 include 禁止。core/Engine.hpp 経由で include される
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── 色覚のフィルタ (UI を重ねた後のフレーム全体) のクラス外定義 ─────────

MITIRU_INLINE void mitiru::Engine::tickColorFilter()
{
	if (!render::colorFilterActive(m_config.colorFilter) || !m_device) { return; }
#ifdef _WIN32
	MITIRU_ZONE_NAMED("Engine::ColorFilter");
	auto* dx12 = dynamic_cast<gfx::Dx12Device*>(m_device.get());
	if (dx12 == nullptr)
	{
		debug::warnOnce("a11y.colorfilter.backend", "色覚のフィルタは DX12 の描画でだけ掛かる (この backend では掛けない)");
		return;
	}
	// 描画先は tickUiComposite と同じ実バックバッファ。UI まで含めて掛ける
	ID3D12Resource* target = nullptr;
	if (auto* swap = dx12->getSwapChain()) { target = swap->getBackBufferResource(swap->currentBackBufferIndex()); }
	else if (auto* offscreen = dx12->currentBackBuffer()) { target = offscreen->nativeResource(); }
	if (target == nullptr) { return; }
	if (!m_colorFilterPass) { m_colorFilterPass = std::make_unique<gfx::Dx12ColorFilterPass>(); }
	if (!m_colorFilterPass->apply(dx12->nativeDevice(), dx12->commandQueue(), target,
	                              render::colorFilterMatrix(m_config.colorFilter)))
	{
		debug::warnOnce("a11y.colorfilter.failed", "色覚のフィルタを掛けられなかった (このフレームはフィルタ無しで出す)");
	}
#endif
}
