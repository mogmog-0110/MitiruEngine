#pragma once

/// @file NeuralStyle.hpp
/// @brief 実時間ニューラル style 変換 (ONNX Runtime + DirectML EP, GPU 推論)。
/// @details レンダリング済みフレーム (RGBA8) を fast-neural-style CNN に通して
///          2D 絵画調へ変換する。3D⇄2D の「現像」ギミックの心臓部。
///          推論は **GPU (DirectML)** で走る。入出力テンソルは ORT 管理の CPU バッファ
///          だが、計算自体は DML EP が GPU 実行する。モデルは fully-conv なので
///          動的形状 (任意 H,W) を受ける (tools/make_dynamic.py で dim を動的化済み)。
///          前処理: RGBA8 → 平面 RGB float32 (0..255、正規化なし)。
///          後処理: 出力 float [1,3,H,W] を 0..255 にクランプ → RGBA8。
///
/// MITIRU_HAS_ONNX が未定義のときはスタブ (stylize は false を返す)。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef MITIRU_HAS_ONNX
#include <array>
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#endif

namespace mitiru::render
{

/// @brief ONNX style モデルを保持し、フレームを GPU で 2D 調へ変換する。
class NeuralStyle
{
public:
	/// @brief 指定モデルのセッションを (必要なら) 構築する。成功で true。
	/// @details 同じ path なら再利用。DirectML EP (device 0) を GPU 推論に使う。
	bool ensure(const std::string& modelPath)
	{
		if (m_ready && modelPath == m_path) { return true; }
#ifdef MITIRU_HAS_ONNX
		try
		{
			m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "mitiru_neural");
			Ort::SessionOptions so;
			so.SetExecutionMode(ORT_SEQUENTIAL);
			so.DisableMemPattern();              // DML EP の要件
			so.SetGraphOptimizationLevel(ORT_ENABLE_BASIC);
			so.SetLogSeverityLevel(4);           // WARNING/INFO 抑制 (graph/EP 割当の大量警告を消す)
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(so, 0));

			const std::wstring wpath(modelPath.begin(), modelPath.end());  // ASCII 前提
			m_session = std::make_unique<Ort::Session>(*m_env, wpath.c_str(), so);

			Ort::AllocatorWithDefaultOptions alloc;
			m_inName  = m_session->GetInputNameAllocated(0, alloc).get();
			m_outName = m_session->GetOutputNameAllocated(0, alloc).get();

			m_path  = modelPath;
			m_ready = true;
			return true;
		}
		catch (const std::exception&)
		{
			m_session.reset();
			m_env.reset();
			m_ready = false;
			return false;
		}
#else
		(void)modelPath;
		return false;
#endif
	}

	bool ready() const noexcept { return m_ready; }
	const std::string& model() const noexcept { return m_path; }

	/// @brief RGBA8 フレームを style 変換する。out は w*h*4 (A=255) で埋まる。
	/// @return 成功で true。未準備・例外時 false (呼び元は元フレームを使う)。
	bool stylize(const std::uint8_t* rgba, int w, int h, std::vector<std::uint8_t>& out)
	{
		if (!m_ready || rgba == nullptr || w <= 0 || h <= 0) { return false; }
#ifdef MITIRU_HAS_ONNX
		try
		{
			const std::size_t hw = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);

			// 前処理: RGBA8 → 平面 RGB float32 (0..255)
			std::vector<float> in(hw * 3);
			float* rp = in.data();
			float* gp = in.data() + hw;
			float* bp = in.data() + hw * 2;
			for (std::size_t i = 0; i < hw; ++i)
			{
				rp[i] = static_cast<float>(rgba[i * 4 + 0]);
				gp[i] = static_cast<float>(rgba[i * 4 + 1]);
				bp[i] = static_cast<float>(rgba[i * 4 + 2]);
			}

			const std::array<std::int64_t, 4> shape{1, 3, h, w};
			Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
			Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
				mem, in.data(), in.size(), shape.data(), shape.size());

			const char* inNames[]  = { m_inName.c_str() };
			const char* outNames[] = { m_outName.c_str() };
			auto outputs = m_session->Run(Ort::RunOptions{nullptr},
				inNames, &inputTensor, 1, outNames, 1);
			const float* o = outputs[0].GetTensorData<float>();
			const float* orr = o;
			const float* org = o + hw;
			const float* orb = o + hw * 2;

			// 後処理: float [1,3,H,W] → RGBA8 (0..255 クランプ)
			out.resize(hw * 4);
			for (std::size_t i = 0; i < hw; ++i)
			{
				out[i * 4 + 0] = clamp8(orr[i]);
				out[i * 4 + 1] = clamp8(org[i]);
				out[i * 4 + 2] = clamp8(orb[i]);
				out[i * 4 + 3] = 255;
			}
			return true;
		}
		catch (const std::exception&)
		{
			return false;
		}
#else
		(void)rgba; (void)w; (void)h; (void)out;
		return false;
#endif
	}

private:
	static std::uint8_t clamp8(float v) noexcept
	{
		const int x = static_cast<int>(v + 0.5f);
		return static_cast<std::uint8_t>(x < 0 ? 0 : (x > 255 ? 255 : x));
	}

	std::string m_path;
	bool        m_ready = false;
#ifdef MITIRU_HAS_ONNX
	std::unique_ptr<Ort::Env>     m_env;
	std::unique_ptr<Ort::Session> m_session;
	std::string                   m_inName, m_outName;
#endif
};

}  // namespace mitiru::render
