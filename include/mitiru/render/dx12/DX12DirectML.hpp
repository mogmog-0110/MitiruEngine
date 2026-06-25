#pragma once

/// @file DX12DirectML.hpp
/// @brief Renderer3D_DX12 の「raw DirectML」in-pipeline ニューラル後処理 (.inl)。
/// @details DX12Neural.hpp (ORT + DirectML EP・CPU 往復あり) と違い、こちらは engine の
///          **D3D12 device 上に直接 DirectML device を作り**、レンダーターゲットを
///          CPU 往復なしでテンソル化してパイプライン内で推論する低レベル経路。
///          Renderer3D_DX12 の class body 内から include される (DX12Splat/DX12Neural と同流儀)。
///          MITIRU_HAS_DIRECTML が未定義のときは全メソッドが no-op スタブ。
///
///          段階構築: M0 = device 生成 (このハートビート) → tensor interop → 実モデル。

/// @brief engine の m_d3dDevice 上に DirectML device を一度だけ作る。成功で true。
/// @details DirectML は D3D12 ベースなので同一 device を共有でき、RT を CPU 往復なしで
///          テンソルとして渡せる。これが本経路の前提 (一番難しくて価値のある所)。
bool ensureDirectMLDx12()
{
#ifdef MITIRU_HAS_DIRECTML
	if (m_dmlDevice) { return true; }
	if (m_d3dDevice == nullptr) { return false; }

	const HRESULT hr = DMLCreateDevice(m_d3dDevice, DML_CREATE_DEVICE_FLAG_NONE,
	                                   IID_PPV_ARGS(m_dmlDevice.GetAddressOf()));
	if (FAILED(hr) || !m_dmlDevice)
	{
		std::fprintf(stderr, "[DirectML] DMLCreateDevice failed (hr=0x%08lX)\n",
		             static_cast<unsigned long>(hr));
		m_dmlDevice.Reset();
		m_dmlInitTried = true;
		return false;
	}
	std::fprintf(stderr, "[DirectML] device created on the engine's D3D12 device "
	                     "(raw DirectML in-pipeline path ready)\n");
	m_dmlInitTried = true;
	return true;
#else
	return false;
#endif
}
