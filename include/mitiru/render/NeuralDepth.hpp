#pragma once

/// @file NeuralDepth.hpp
/// @brief 単眼深度推定 (ONNX Runtime + DirectML EP, GPU 推論)。
/// @details レンダリング済みフレーム (RGBA8) の **キャラ領域クロップ**を Depth-Anything-V2 に通し、
///          相対深度マップ (0..1, 大=手前) を得る。フラットなアニメ絵でも、部屋を外して人物に
///          密着クロップすると「体の丸み」が推論される (全画面だと部屋の奥行きを拾い平面に潰れる)。
///          前処理: crop → 14 の倍数 (長辺 518) へ bilinear 縮小 → ImageNet 正規化 → NCHW fp32。
///          後処理: 出力 (1,dH,dW) を min-max で 0..1 へ。
///          MITIRU_HAS_ONNX 未定義時はスタブ (infer は false)。

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef MITIRU_HAS_ONNX
#include <array>
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#endif

namespace mitiru::render
{

class NeuralDepth
{
public:
	~NeuralDepth() { if (m_loadThread.joinable()) m_loadThread.join(); }

	/// @brief モデルを**非同期**でロード (99MB + DirectML グラフコンパイルで数十秒。render thread を
	///        ブロックしないよう別スレッドで構築)。ロード完了まで false → relight は輝度プロキシで動く。
	bool ensure(const std::string& modelPath)
	{
		if (m_ready.load()) { return modelPath == m_path; }
#ifdef MITIRU_HAS_ONNX
		if (m_loadStarted) { return false; }   // ロード進行中
		m_path = modelPath; m_loadStarted = true;
		m_loadThread = std::thread([this, modelPath]() {
			try
			{
				auto env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "mitiru_depth");
				Ort::SessionOptions so;
				so.SetExecutionMode(ORT_SEQUENTIAL);
				so.DisableMemPattern();
				so.SetGraphOptimizationLevel(ORT_ENABLE_BASIC);
				so.SetLogSeverityLevel(4);
				Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(so, 0));
				const std::wstring wpath(modelPath.begin(), modelPath.end());
				auto sess = std::make_unique<Ort::Session>(*env, wpath.c_str(), so);
				Ort::AllocatorWithDefaultOptions alloc;
				m_inName  = sess->GetInputNameAllocated(0, alloc).get();
				m_outName = sess->GetOutputNameAllocated(0, alloc).get();
				m_env = std::move(env); m_session = std::move(sess);
				m_ready.store(true);   // release: 上の代入が render thread から見える
				std::fprintf(stderr, "[Depth] DirectML depth model ready (async load done)\n");
			}
			catch (const std::exception& e) { std::fprintf(stderr, "[Depth] load failed: %s\n", e.what()); }
		});
		return false;
#else
		(void)modelPath; return false;
#endif
	}
	bool ready() const noexcept { return m_ready.load(); }

	/// @brief フレーム RGBA8 のクロップ領域 [cx0,cy0,cx1,cy1] (フレーム比 0..1) を深度推定する。
	///        成功で m_depth(0..1, 大=手前) を outW×outH で埋め true。
	bool infer(const std::uint8_t* rgba, int fw, int fh,
	           float cx0, float cy0, float cx1, float cy1)
	{
		if (!m_ready.load() || !rgba || fw <= 0 || fh <= 0) { return false; }
#ifdef MITIRU_HAS_ONNX
		try
		{
			// クロップ画素矩形
			const int x0 = clampi((int)(cx0 * fw), 0, fw - 2), x1 = clampi((int)(cx1 * fw), x0 + 1, fw);
			const int y0 = clampi((int)(cy0 * fh), 0, fh - 2), y1 = clampi((int)(cy1 * fh), y0 + 1, fh);
			const int cw = x1 - x0, chh = y1 - y0;
			// 14 の倍数・長辺 518 へ
			const float sc = 518.0f / (float)((cw > chh) ? cw : chh);
			int W = mul14((int)(cw * sc)), H = mul14((int)(chh * sc));
			if (W < 14) W = 14; if (H < 14) H = 14;
			m_inW = W; m_inH = H;

			// crop → bilinear 縮小 → ImageNet 正規化 → NCHW
			const std::size_t hw = (std::size_t)W * H;
			if (m_buf.size() != hw * 3) { m_buf.assign(hw * 3, 0.0f); }
			float* rp = m_buf.data(); float* gp = rp + hw; float* bp = gp + hw;
			const float mr = 0.485f, mg = 0.456f, mb = 0.406f, sr = 0.229f, sg = 0.224f, sb = 0.225f;
			for (int yy = 0; yy < H; ++yy)
			{
				const float fy = (yy + 0.5f) / H * chh - 0.5f; int sy = (int)fy; float ty = fy - sy;
				sy = clampi(sy, 0, chh - 1); int sy1 = clampi(sy + 1, 0, chh - 1);
				for (int xx = 0; xx < W; ++xx)
				{
					const float fx = (xx + 0.5f) / W * cw - 0.5f; int sx = (int)fx; float tx = fx - sx;
					sx = clampi(sx, 0, cw - 1); int sx1 = clampi(sx + 1, 0, cw - 1);
					float r, g, b;
					sampleBilinear(rgba, fw, x0, y0, sx, sy, sx1, sy1, tx, ty, r, g, b);
					const std::size_t i = (std::size_t)yy * W + xx;
					rp[i] = (r / 255.0f - mr) / sr; gp[i] = (g / 255.0f - mg) / sg; bp[i] = (b / 255.0f - mb) / sb;
				}
			}

			const std::array<std::int64_t, 4> shape{1, 3, H, W};
			Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
			Ort::Value in = Ort::Value::CreateTensor<float>(mem, m_buf.data(), m_buf.size(), shape.data(), shape.size());
			const char* inN[] = {m_inName.c_str()}; const char* outN[] = {m_outName.c_str()};
			auto outs = m_session->Run(Ort::RunOptions{nullptr}, inN, &in, 1, outN, 1);
			// 出力 (1,dH,dW)
			auto info = outs[0].GetTensorTypeAndShapeInfo();
			auto os = info.GetShape();   // {1,dH,dW}
			const int dH = (int)os[os.size() - 2], dW = (int)os[os.size() - 1];
			const float* o = outs[0].GetTensorData<float>();
			const std::size_t n = (std::size_t)dW * dH;
			float mn = o[0], mx = o[0];
			for (std::size_t i = 1; i < n; ++i) { if (o[i] < mn) mn = o[i]; if (o[i] > mx) mx = o[i]; }
			const float inv = (mx > mn) ? 1.0f / (mx - mn) : 0.0f;
			m_depth.resize(n);
			for (std::size_t i = 0; i < n; ++i) { m_depth[i] = (o[i] - mn) * inv; }   // 0..1 (大=手前)
			m_dW = dW; m_dH = dH;
			m_cx0 = cx0; m_cy0 = cy0; m_cx1 = cx1; m_cy1 = cy1;
			return true;
		}
		catch (const std::exception&) { return false; }
#else
		(void)rgba; (void)fw; (void)fh; (void)cx0; (void)cy0; (void)cx1; (void)cy1; return false;
#endif
	}

	const std::vector<float>& depth() const { return m_depth; }
	int depthW() const { return m_dW; }
	int depthH() const { return m_dH; }
	void cropRect(float& x0, float& y0, float& x1, float& y1) const { x0 = m_cx0; y0 = m_cy0; x1 = m_cx1; y1 = m_cy1; }

private:
	static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
	static int mul14(int v) { return (v / 14) * 14; }
	static void sampleBilinear(const std::uint8_t* rgba, int fw, int x0, int y0,
	                           int sx, int sy, int sx1, int sy1, float tx, float ty,
	                           float& r, float& g, float& b)
	{
		auto px = [&](int x, int y, int c) -> float {
			return (float)rgba[(((std::size_t)(y0 + y) * fw) + (x0 + x)) * 4 + c]; };
		for (int c = 0; c < 3; ++c)
		{
			const float a = px(sx, sy, c) * (1 - tx) + px(sx1, sy, c) * tx;
			const float bb = px(sx, sy1, c) * (1 - tx) + px(sx1, sy1, c) * tx;
			const float v = a * (1 - ty) + bb * ty;
			if (c == 0) r = v; else if (c == 1) g = v; else b = v;
		}
	}

	std::string m_path; std::atomic<bool> m_ready{false}; bool m_loadStarted = false;
	std::thread m_loadThread;
	std::vector<float> m_depth, m_buf;
	int m_dW = 0, m_dH = 0, m_inW = 0, m_inH = 0;
	float m_cx0 = 0, m_cy0 = 0, m_cx1 = 1, m_cy1 = 1;
#ifdef MITIRU_HAS_ONNX
	std::unique_ptr<Ort::Env> m_env;
	std::unique_ptr<Ort::Session> m_session;
	std::string m_inName, m_outName;
#endif
};

}  // namespace mitiru::render
