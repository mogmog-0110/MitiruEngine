#pragma once

/// @file TemporalContour.hpp
/// @brief 輪郭線の時間的安定化（velocity reproject + objectId gate + EMA + ヒステリシス）
/// @details `ContourDetect` が出す生輪郭は、カメラ/オブジェクトが動くとフレーム間で
///          ちらつく（flickering）。本ファイルは前フレームの安定化結果を velocity で
///          現フレームへ reproject し、objectId で correspondence を検証してから EMA で
///          ブレンドすることで線を時間的に安定させる。`#1 velocity` / `#2 ContourDetect` /
///          `#4 objectId` / `#6 previousGBuffer` の上に乗る NPR/トゥーン描画の実用機能。
///
/// 研究ハーネス（`tests/mitiru/TestTemporalContour.cpp`）で定量実証済み:
///   - S1 flickering を TCF −63.4%（velocity reproject + EMA）
///   - S4 disocclusion ゴーストを accuracy −20.5%（objectId gate）
///   - velocity を切ると +51.9% 悪化＝最大寄与
///
/// @code
/// mitiru::render::TemporalContourStabilizer stab;
/// std::vector<float> prevStable;       // 初回は空
/// // 毎フレーム:
/// auto raw    = mitiru::render::detectContours(pipeline.gBuffer());
/// auto stable = stab.stabilize(pipeline.gBuffer(), pipeline.previousGBuffer(),
///                              raw, prevStable, {});
/// prevStable = stable;                 // 次フレームの履歴として保持
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <mitiru/render/ContourDetect.hpp>   // clampCoord
#include <mitiru/render/GBuffer.hpp>

namespace mitiru::render
{

/// @brief 時間的安定化のパラメータ
struct TemporalContourParams
{
	float alpha = 0.8f;          ///< EMA 履歴重み [0,1]（0=生のみ / 1=履歴のみ）
	/// @brief EMA 履歴重みの velocity 減衰係数。0 = 減衰なし（従来挙動）。
	/// @details 移動量に応じて履歴重みを下げる: `alphaEff = alpha * clamp(1 - falloff*|velocity|, 0, 1)`。
	///          速く動く線で履歴の再サンプリングが motion blur 状のぼやけ（線の太り）になるのを抑え、
	///          静止部は従来どおり強い履歴で flicker を抑える（要 useVelocity）。
	float alphaMotionFalloff = 0.0f;
	bool  useVelocity = true;    ///< false: reproject せず同一画素の履歴を使う（ablation）
	bool  useObjId = true;       ///< false: objectId gate を無効化（ablation）
	bool  useHysteresis = true;  ///< false: ヒステリシス床なし
	float tauOn = 0.5f;          ///< 一度立った線を維持するヒステリシス下限
	/// @brief ヒステリシス床の velocity 減衰係数（#9）。0 = 減衰なし（従来挙動）。
	/// @details 移動量に応じて床を弱める: `floorCap = tauOn * clamp(1 - falloff*|velocity|, 0, 1)`。
	///          静止部は床を維持して flicker を抑え、移動エッジでは床を弱めて trailing halo（線幅の
	///          蓄積）を抑える。剛体移動シルエットで太る/脈打つ症状の緩和に使う（要 useVelocity）。
	float floorMotionFalloff = 0.0f;
	/// @brief raw 近傍 gate 付き床（#10、option c）。true で有効。
	/// @details 現フレームの raw 輪郭が近傍（3x3）に全く立っていない画素には床を適用しない。
	///          ＝完全に動き去った線の floor 維持（trailing halo）を構造的に断つ。velocity 減衰
	///          （#9）が線幅 jitter を 2% しか緩和できなかったのに対し、消えた線の halo を直接除去する。
	bool floorRawGate = false;
	/// @brief raw gate のしきい値。近傍 raw の最大がこれ未満なら床を適用しない。
	float rawGateThreshold = 0.1f;
};

/// @brief 輪郭線の時間的安定化器（ステートレス。履歴は呼び出し側が保持する）
class TemporalContourStabilizer
{
public:
	/// @brief 生輪郭を前フレームの安定化結果でブレンドして時間的に安定させる。
	/// @param cur        現フレーム GBuffer（velocity / objectId を読む）
	/// @param prev       前フレーム GBuffer（reproject 先の objectId を読む。`previousGBuffer()`）
	/// @param rawContour 現フレームの生輪郭（`detectContours()`）。cur と同サイズ
	/// @param prevStable 前フレームの安定化済み輪郭（初回は空 → raw をそのまま返す）
	/// @param p          パラメータ
	/// @return 安定化済み輪郭 [0,1]（cur と同サイズ）
	[[nodiscard]] std::vector<float> stabilize(
		const GBuffer& cur,
		const GBuffer& prev,
		const std::vector<float>& rawContour,
		const std::vector<float>& prevStable,
		const TemporalContourParams& p = {}) const
	{
		const int w = cur.width(), h = cur.height();
		std::vector<float> out = rawContour;

		// 初回 / サイズ不整合 / 前フレーム未対応 → 生をそのまま（disocclusion ゴースト防止）。
		const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
		if (w < 1 || h < 1 || rawContour.size() != n || prevStable.size() != n)
		{
			return out;
		}

		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				const GBufferPixel& c = cur.readPixel(x, y);
				const float vx = p.useVelocity ? c.velocity.x : 0.0f;
				const float vy = p.useVelocity ? c.velocity.y : 0.0f;
				const float sx = static_cast<float>(x) - vx;   // 前フレーム対応位置
				const float sy = static_cast<float>(y) - vy;
				if (sx < 0.0f || sx > w - 1 || sy < 0.0f || sy > h - 1) { continue; }

				if (p.useObjId)
				{
					const int ix = clampCoord(static_cast<int>(std::lround(sx)), w);
					const int iy = clampCoord(static_cast<int>(std::lround(sy)), h);
					if (prev.readPixel(ix, iy).objectId != c.objectId) { continue; }
				}

				const float hist = bilinear(prevStable, w, h, sx, sy);
				const std::size_t i = static_cast<std::size_t>(y * w + x);
				const float speed = std::sqrt(vx * vx + vy * vy);
				float alphaEff = p.alpha;
				if (p.alphaMotionFalloff > 0.0f)   // 移動エッジは履歴を弱めて EMA のぼやけを抑える
				{
					alphaEff *= std::clamp(1.0f - p.alphaMotionFalloff * speed, 0.0f, 1.0f);
				}
				float s = rawContour[i] * (1.0f - alphaEff) + hist * alphaEff;   // EMA
				// #10 option c: 現フレームの raw が近傍に無い画素は床を適用しない
				//   （完全に動き去った線の trailing halo を構造的に除去）。
				const bool floorAllowed =
					!p.floorRawGate || neighborMaxRaw(rawContour, w, h, x, y) >= p.rawGateThreshold;
				if (p.useHysteresis && hist >= p.tauOn && floorAllowed)
				{
					float floorCap = p.tauOn;
					if (p.floorMotionFalloff > 0.0f)   // #9: 移動エッジは床を弱めて halo を抑える
					{
						floorCap *= std::clamp(1.0f - p.floorMotionFalloff * speed, 0.0f, 1.0f);
					}
					s = std::max(s, std::min(hist, floorCap));   // ヒステリシス床（velocity 認識）
				}
				out[i] = s;
			}
		}
		return out;
	}

private:
	/// @brief (x,y) の 3x3 近傍における raw 輪郭の最大値（#10 raw gate 用）。
	[[nodiscard]] static float neighborMaxRaw(const std::vector<float>& raw, int w, int h,
	                                          int x, int y) noexcept
	{
		float m = 0.0f;
		for (int dy = -1; dy <= 1; ++dy)
		{
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int nx = clampCoord(x + dx, w), ny = clampCoord(y + dy, h);
				m = std::max(m, raw[static_cast<std::size_t>(ny * w + nx)]);
			}
		}
		return m;
	}

	/// @brief クランプ付き bilinear サンプル（row-major float マップ）。
	[[nodiscard]] static float bilinear(const std::vector<float>& f, int w, int h,
	                                     float fx, float fy) noexcept
	{
		const int x0 = clampCoord(static_cast<int>(std::floor(fx)), w);
		const int y0 = clampCoord(static_cast<int>(std::floor(fy)), h);
		const int x1 = clampCoord(x0 + 1, w);
		const int y1 = clampCoord(y0 + 1, h);
		const float tx = fx - std::floor(fx);
		const float ty = fy - std::floor(fy);

		const float a = f[static_cast<std::size_t>(y0 * w + x0)];
		const float b = f[static_cast<std::size_t>(y0 * w + x1)];
		const float c = f[static_cast<std::size_t>(y1 * w + x0)];
		const float d = f[static_cast<std::size_t>(y1 * w + x1)];
		return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
	}
};

// ── dropout-fill 安定化（#15）─────────────────────────────────────────────────
//
// EMA (`TemporalContourStabilizer`) は幅 1px の線を bilinear 再サンプルで**必然的にぼかす**
// （実モデル比較で「無対策が一番綺麗 / EMA はぼやける」の主因）。dropout-fill は raw を一切
// ぼかさず、**瞬間的な欠落（フレーム間のチラつき）だけ**を履歴で補填する代替演算子。
//
// 棲み分け:
//   - 色など密で平滑が欲しい信号 → EMA (`stabilize`)。
//   - 線など疎な knife-edge 信号 → dropout-fill（こちら）。S6 で accuracy（ぼけ）が EMA 比 −70%。

/// @brief dropout-fill のパラメータ
struct DropoutFillParams
{
	float fillThreshold = 0.3f;    ///< raw がこれ未満の画素だけ補填対象（線の芯はそのまま）
	float histThreshold = 0.35f;   ///< reproject 履歴がこれ以上 =「前フレームここに線があった」
	float supportThreshold = 0.3f; ///< 3x3 近傍 raw の最大がこれ以上 = 線が近くに生きている
	float decay = 0.75f;           ///< 補填値の減衰。消えた線は decay^n でフェードアウト（ghost 防止）
	bool  useObjId = true;         ///< objectId gate（別物体の履歴を混ぜない）
};

/// @brief raw のシャープさを保ったまま瞬間欠落だけ履歴で補填する（EMA の代替、#15）。
/// @param cur        現フレーム GBuffer（velocity / objectId）
/// @param prev       前フレーム GBuffer（reproject 先 objectId、`previousGBuffer()`）
/// @param raw        現フレーム生輪郭（`detectContours()`）。これは一切ぼかさない
/// @param prevStable 前フレームの安定化結果（初回は空 → raw をそのまま返す）
/// @return 安定化済み輪郭（raw と同サイズ）
[[nodiscard]] inline std::vector<float> dropoutFill(
	const GBuffer& cur, const GBuffer& prev,
	const std::vector<float>& raw, const std::vector<float>& prevStable,
	const DropoutFillParams& p = {})
{
	std::vector<float> out = raw;
	const int w = cur.width(), h = cur.height();
	const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
	if (prevStable.empty() || w < 1 || h < 1 || raw.size() != n || prevStable.size() != n)
	{
		return out;   // 初回 / サイズ不整合 → raw をそのまま
	}

	auto bilinear = [&](const std::vector<float>& f, float fx, float fy) noexcept {
		const int x0 = clampCoord(static_cast<int>(std::floor(fx)), w);
		const int y0 = clampCoord(static_cast<int>(std::floor(fy)), h);
		const int x1 = clampCoord(x0 + 1, w), y1 = clampCoord(y0 + 1, h);
		const float tx = fx - std::floor(fx), ty = fy - std::floor(fy);
		const float a = f[static_cast<std::size_t>(y0 * w + x0)];
		const float b = f[static_cast<std::size_t>(y0 * w + x1)];
		const float c = f[static_cast<std::size_t>(y1 * w + x0)];
		const float d = f[static_cast<std::size_t>(y1 * w + x1)];
		return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
	};
	auto neighborMax = [&](const std::vector<float>& f, int x, int y) noexcept {
		float best = 0.0f;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				best = std::max(best, f[static_cast<std::size_t>(
					clampCoord(y + dy, h) * w + clampCoord(x + dx, w))]);
		return best;
	};

	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			const std::size_t i = static_cast<std::size_t>(y * w + x);
			if (raw[i] >= p.fillThreshold) { continue; }   // 線の芯はそのまま（ぼかさない）

			const GBufferPixel& c = cur.readPixel(x, y);
			const float sx = static_cast<float>(x) - c.velocity.x;
			const float sy = static_cast<float>(y) - c.velocity.y;
			if (sx < 0.0f || sx > w - 1 || sy < 0.0f || sy > h - 1) { continue; }
			if (p.useObjId)
			{
				const int ix = clampCoord(static_cast<int>(std::lround(sx)), w);
				const int iy = clampCoord(static_cast<int>(std::lround(sy)), h);
				if (prev.readPixel(ix, iy).objectId != c.objectId) { continue; }
			}

			const float hist = bilinear(prevStable, sx, sy);
			if (hist < p.histThreshold) { continue; }                    // 前に線が無い
			if (neighborMax(raw, x, y) < p.supportThreshold) { continue; } // 線が死んだ

			out[i] = std::max(raw[i], hist * p.decay);   // 瞬間欠落を減衰付きで補填
		}
	}
	return out;
}

} // namespace mitiru::render
