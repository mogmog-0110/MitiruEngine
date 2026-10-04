#pragma once

/// @file DX12DirectML.hpp
/// @brief Renderer3D_DX12 の「raw DirectML」in-pipeline ニューラル後処理 (.inl)。
/// @details DX12Neural.hpp (ORT + DirectML EP・CPU 往復あり) と違い、こちらは engine の
///          **D3D12 device 上に直接 DirectML device を作り**、レンダーターゲットを
///          CPU 往復なしでテンソル化してパイプライン内で推論する低レベル経路。
///          Renderer3D_DX12 の class body 内から include される (DX12Splat/DX12Neural と同流儀)。
///          MITIRU_HAS_DIRECTML が未定義のときは全メソッドが no-op スタブ。

/// @brief engine の m_d3dDevice 上に DirectML device を一度だけ作る。成功で true。
/// @details 使う機能 (enableNeuralFx) を有効にした時点で呼ぶ。使わない実行では作らない。
///          作れなかったときは二度と試さない。DirectML は D3D12 ベースなので同一 device を共有でき、
///          RT を CPU 往復なしでテンソルとして渡せる。
bool ensureDirectMLDx12()
{
#ifdef MITIRU_HAS_DIRECTML
	if (m_dmlDevice) { return true; }
	if (m_dmlInitTried || m_d3dDevice == nullptr) { return false; }
	m_dmlInitTried = true;

	const HRESULT hr = DMLCreateDevice(m_d3dDevice, DML_CREATE_DEVICE_FLAG_NONE,
	                                   IID_PPV_ARGS(m_dmlDevice.GetAddressOf()));
	if (FAILED(hr) || !m_dmlDevice)
	{
		m_dmlDevice.Reset();
		debug::warnOnce("render.directml.create",
			"ニューラル後処理に使う DirectML の準備に失敗したので、後処理を掛けずに描きます。"
			"DirectML.dll が mitiru_host の隣にあるかを確かめてください。");
		return false;
	}
	return true;
#else
	return false;
#endif
}
