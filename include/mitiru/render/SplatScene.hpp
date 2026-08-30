#pragma once

/// @file SplatScene.hpp
/// @brief 3D Gaussian Splatting シーンの読み込み (.splat フォーマット) と GPU 向けデータ。
/// @details 写実スプラットを MitiruEngine の DX12 レンダラーに描くための部品。
///          antimatter15 `.splat` 形式 (32 B/splat、SH なし=フラット色) を読み、シェーダの
///          `StructuredBuffer<Splat>` レイアウトに一致する 64 B の `SplatGPU` 配列へ展開する。
///          詳細・ロードマップ: oscar-rythm/docs/splatting-dx12.md

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace mitiru::render
{

/// @brief GPU 向け 1 スプラット。**DX12SplatShaders.hpp の `struct Splat` と同レイアウト (64 B)**。
/// rot は xyzw 正規化クォータニオン。rgb.a は未使用 (不透明度は opacity)。
struct SplatGPU
{
	float pos[3];   float opacity;   ///< 16: 位置 + 不透明度(0..1)
	float scale[3]; float _pad;      ///< 16: スケール(線形) + パディング
	float rot[4];                    ///< 16: クォータニオン xyzw (正規化)
	float rgb[4];                    ///< 16: 色 r,g,b (a 未使用)
};
static_assert(sizeof(SplatGPU) == 64, "SplatGPU は 64 B (シェーダの cbuffer/StructuredBuffer と一致)");

/// @brief 読み込んだスプラットシーン。
struct SplatScene
{
	std::vector<SplatGPU> splats;
	float boundsMin[3] = {0.0f, 0.0f, 0.0f};
	float boundsMax[3] = {0.0f, 0.0f, 0.0f};
	float center[3]    = {0.0f, 0.0f, 0.0f};
	float radius       = 1.0f;   ///< シーンの境界球半径 (カメラ初期配置の目安)

	[[nodiscard]] bool        empty() const noexcept { return splats.empty(); }
	[[nodiscard]] std::size_t count() const noexcept { return splats.size(); }
};

namespace detail
{
/// @brief 対称 3x3 の Jacobi 固有分解。固有ベクトルは V の列 (正規直交)。
inline void jacobiEigen3(double A[3][3], double V[3][3])
{
	for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) V[i][j] = (i == j) ? 1.0 : 0.0;
	for (int sweep = 0; sweep < 64; ++sweep)
	{
		const double off = std::fabs(A[0][1]) + std::fabs(A[0][2]) + std::fabs(A[1][2]);
		if (off < 1e-14) { break; }
		for (int p = 0; p < 2; ++p) for (int q = p + 1; q < 3; ++q)
		{
			if (std::fabs(A[p][q]) < 1e-18) { continue; }
			const double th = (A[q][q] - A[p][p]) / (2.0 * A[p][q]);
			const double t  = (th >= 0 ? 1.0 : -1.0) / (std::fabs(th) + std::sqrt(th * th + 1.0));
			const double c  = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
			for (int k = 0; k < 3; ++k) { const double a = A[k][p], b = A[k][q]; A[k][p] = c*a - s*b; A[k][q] = s*a + c*b; }
			for (int k = 0; k < 3; ++k) { const double a = A[p][k], b = A[q][k]; A[p][k] = c*a - s*b; A[q][k] = s*a + c*b; }
			for (int k = 0; k < 3; ++k) { const double a = V[k][p], b = V[k][q]; V[k][p] = c*a - s*b; V[k][q] = s*a + c*b; }
		}
	}
}

/// @brief 3x3 回転行列 → クォータニオン (x,y,z,w)。DX12SplatShaders の quatToMat と同規約。
inline void matToQuat(const float R[3][3], float q[4])
{
	const float tr = R[0][0] + R[1][1] + R[2][2];
	if (tr > 0.0f)
	{
		float s = std::sqrt(tr + 1.0f) * 2.0f;
		q[3] = 0.25f * s; q[0] = (R[2][1] - R[1][2]) / s; q[1] = (R[0][2] - R[2][0]) / s; q[2] = (R[1][0] - R[0][1]) / s;
	}
	else if (R[0][0] > R[1][1] && R[0][0] > R[2][2])
	{
		float s = std::sqrt(1.0f + R[0][0] - R[1][1] - R[2][2]) * 2.0f;
		q[3] = (R[2][1] - R[1][2]) / s; q[0] = 0.25f * s; q[1] = (R[0][1] + R[1][0]) / s; q[2] = (R[0][2] + R[2][0]) / s;
	}
	else if (R[1][1] > R[2][2])
	{
		float s = std::sqrt(1.0f + R[1][1] - R[0][0] - R[2][2]) * 2.0f;
		q[3] = (R[0][2] - R[2][0]) / s; q[0] = (R[0][1] + R[1][0]) / s; q[1] = 0.25f * s; q[2] = (R[1][2] + R[2][1]) / s;
	}
	else
	{
		float s = std::sqrt(1.0f + R[2][2] - R[0][0] - R[1][1]) * 2.0f;
		q[3] = (R[1][0] - R[0][1]) / s; q[0] = (R[0][2] + R[2][0]) / s; q[1] = (R[1][2] + R[2][1]) / s; q[2] = 0.25f * s;
	}
}

/// @brief Hamilton 積 (x,y,z,w): out = a ⊗ b (先に b、次に a を適用)。
inline void quatMul(const float a[4], const float b[4], float out[4])
{
	out[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
	out[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
	out[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
	out[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
}
}  // namespace detail

/// @brief object-centric splat シーンを主成分(PCA)で自動的に立て直す。
/// @details 最大主軸(=長手/つま先-かかと)→X, 中間(=高さ/ソール-甲)→Y, 最小(=幅)→Z へ
///          整列。靴は水平に寝かされ側面がカメラ(+Z)を向く。位置を重心まわりに回し、
///          楕円の向き(クォータニオン)も同じ回転で合わせる。flipX/flipY は符号の曖昧さ
///          (上下/つま先左右) をシーンを見て一度だけ補正するための 180° 反転。
inline void uprightByPCA(SplatScene& out, bool flipX, bool flipY)
{
	const std::size_t n = out.splats.size();
	if (n < 16) { return; }

	double c[3] = {0, 0, 0};
	for (const SplatGPU& s : out.splats) { c[0] += s.pos[0]; c[1] += s.pos[1]; c[2] += s.pos[2]; }
	c[0] /= (double)n; c[1] /= (double)n; c[2] /= (double)n;

	double C[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
	for (const SplatGPU& s : out.splats)
	{
		const double d[3] = { s.pos[0]-c[0], s.pos[1]-c[1], s.pos[2]-c[2] };
		for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) C[i][j] += d[i]*d[j];
	}

	double eval[3]; double V[3][3];
	double A[3][3]; for (int i=0;i<3;++i) for(int j=0;j<3;++j) A[i][j]=C[i][j];
	detail::jacobiEigen3(A, V);
	for (int i = 0; i < 3; ++i) { eval[i] = A[i][i]; }

	int order[3] = {0, 1, 2};                       // 固有値 降順 (大=長手, 中=高さ, 小=幅)
	for (int i = 0; i < 2; ++i) for (int j = i+1; j < 3; ++j)
		if (eval[order[j]] > eval[order[i]]) { int t=order[i]; order[i]=order[j]; order[j]=t; }

	// R の行 = 主軸 (大→X, 中→Y, 小→Z)。R*(p-c) で主成分フレームへ。
	float R[3][3];
	for (int r = 0; r < 3; ++r) { for (int k = 0; k < 3; ++k) R[r][k] = (float)V[k][order[r]]; }

	// 正規直交化の符号: 右手系 (det=+1) に揃える。
	const float det =
		R[0][0]*(R[1][1]*R[2][2]-R[1][2]*R[2][1]) -
		R[0][1]*(R[1][0]*R[2][2]-R[1][2]*R[2][0]) +
		R[0][2]*(R[1][0]*R[2][1]-R[1][1]*R[2][0]);
	if (det < 0.0f) { for (int k = 0; k < 3; ++k) R[2][k] = -R[2][k]; }

	// 上下(X軸まわり180°)/つま先左右(Y軸まわり180°) の手動補正。
	if (flipX) { for (int k=0;k<3;++k){ R[1][k]=-R[1][k]; R[2][k]=-R[2][k]; } }
	if (flipY) { for (int k=0;k<3;++k){ R[0][k]=-R[0][k]; R[2][k]=-R[2][k]; } }

	float qU[4]; detail::matToQuat(R, qU);

	for (SplatGPU& s : out.splats)
	{
		const float d[3] = { s.pos[0]-(float)c[0], s.pos[1]-(float)c[1], s.pos[2]-(float)c[2] };
		for (int r = 0; r < 3; ++r) { s.pos[r] = R[r][0]*d[0] + R[r][1]*d[1] + R[r][2]*d[2] + (float)c[r]; }
		float qn[4]; detail::quatMul(qU, s.rot, qn);
		s.rot[0]=qn[0]; s.rot[1]=qn[1]; s.rot[2]=qn[2]; s.rot[3]=qn[3];
	}
}

/// @brief antimatter15 `.splat` を読み込む。
/// @details 32 B/splat: pos f32x3 | scale f32x3 (線形) | color u8x4 (a=不透明度) |
///          rot u8x4 (w-first, 128 バイアス)。失敗時 false。
inline bool loadSplatFile(const std::string& path, SplatScene& out)
{
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f) { return false; }
	const std::streamsize sz = f.tellg();
	if (sz < 32) { return false; }
	f.seekg(0);
	const std::size_t n = static_cast<std::size_t>(sz) / 32u;
	std::vector<std::uint8_t> raw(static_cast<std::size_t>(sz));
	if (!f.read(reinterpret_cast<char*>(raw.data()), sz)) { return false; }

	out.splats.resize(n);
	for (std::size_t i = 0; i < n; ++i)
	{
		const std::uint8_t* r = raw.data() + i * 32u;
		SplatGPU& s = out.splats[i];
		float pos[3], scale[3];
		std::memcpy(pos,   r + 0,  12);
		std::memcpy(scale, r + 12, 12);
		const std::uint8_t* col = r + 24;   // r,g,b,a (a = opacity)
		const std::uint8_t* rot = r + 28;   // w,x,y,z (128 バイアス)

		for (int k = 0; k < 3; ++k) { s.pos[k] = pos[k]; s.scale[k] = scale[k]; }
		s._pad    = 0.0f;
		s.opacity = static_cast<float>(col[3]) / 255.0f;
		s.rgb[0]  = static_cast<float>(col[0]) / 255.0f;
		s.rgb[1]  = static_cast<float>(col[1]) / 255.0f;
		s.rgb[2]  = static_cast<float>(col[2]) / 255.0f;
		s.rgb[3]  = 1.0f;

		// w-first バイアス u8 → xyzw 正規化
		const float qw = (static_cast<float>(rot[0]) - 128.0f) / 128.0f;
		const float qx = (static_cast<float>(rot[1]) - 128.0f) / 128.0f;
		const float qy = (static_cast<float>(rot[2]) - 128.0f) / 128.0f;
		const float qz = (static_cast<float>(rot[3]) - 128.0f) / 128.0f;
		float len = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
		if (len < 1e-8f) { len = 1.0f; }
		s.rot[0] = qx / len; s.rot[1] = qy / len; s.rot[2] = qz / len; s.rot[3] = qw / len;
	}

	// ── フローター除去 ── 写実 .splat の典型ノイズを落として見栄えを上げる。
	// パラメータレス (中央値/パーセンタイル基準) なのでシーンに自動適応:
	//   (a) ほぼ透明 (opacity < 0.03) = 残差ノイズ
	//   (b) 巨大な引き伸ばし splat (max-scale > 中央値×12) = grazing で白く尾を引く外れ値
	//   (c) 重心から最も遠い ~2% = 孤立した遠方フローター
	if (out.splats.size() > 2000)
	{
		const std::size_t cnt = out.splats.size();
		const std::size_t mid = cnt / 2;
		std::vector<float> tmp(cnt);

		float med[3];   // ロバスト重心 = 各軸の中央値 (外れ値に強い)
		for (int k = 0; k < 3; ++k)
		{
			for (std::size_t i = 0; i < cnt; ++i) { tmp[i] = out.splats[i].pos[k]; }
			std::nth_element(tmp.begin(), tmp.begin() + mid, tmp.end());
			med[k] = tmp[mid];
		}

		std::vector<float> maxScale(cnt), dist2(cnt);
		for (std::size_t i = 0; i < cnt; ++i)
		{
			const float* sc = out.splats[i].scale;
			maxScale[i] = std::max(sc[0], std::max(sc[1], sc[2]));
			const float dx = out.splats[i].pos[0] - med[0];
			const float dy = out.splats[i].pos[1] - med[1];
			const float dz = out.splats[i].pos[2] - med[2];
			dist2[i] = dx * dx + dy * dy + dz * dz;
		}
		tmp = maxScale;
		std::nth_element(tmp.begin(), tmp.begin() + mid, tmp.end());
		const float scaleMax = tmp[mid] * 12.0f;
		tmp = dist2;
		const std::size_t pIdx = static_cast<std::size_t>(static_cast<double>(cnt) * 0.98);
		std::nth_element(tmp.begin(), tmp.begin() + pIdx, tmp.end());
		const float dist2Max = tmp[pIdx];

		std::vector<SplatGPU> kept;
		kept.reserve(cnt);
		for (std::size_t i = 0; i < cnt; ++i)
		{
			if (out.splats[i].opacity < 0.03f) { continue; }
			if (maxScale[i] > scaleMax)        { continue; }
			if (dist2[i]    > dist2Max)        { continue; }
			kept.push_back(out.splats[i]);
		}
		if (!kept.empty()) { out.splats.swap(kept); }
	}

	// ── 自動アップライト (PCA) ── このシーンの靴は素直な上向きでないので立て直す。
	// (フローター除去後のクリーンな点群で主成分を取る。bounds はこの後で再計算。)
	uprightByPCA(out, /*flipX=*/false, /*flipY=*/false);

	// 境界球を (除去・整列後の) splat から計算 = カメラ初期配置の目安。
	float mn[3] = {1e30f, 1e30f, 1e30f};
	float mx[3] = {-1e30f, -1e30f, -1e30f};
	for (const SplatGPU& s : out.splats)
	{
		for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], s.pos[k]); mx[k] = std::max(mx[k], s.pos[k]); }
	}
	float r2 = 0.0f;
	for (int k = 0; k < 3; ++k)
	{
		out.boundsMin[k] = mn[k];
		out.boundsMax[k] = mx[k];
		out.center[k]    = 0.5f * (mn[k] + mx[k]);
		const float half = 0.5f * (mx[k] - mn[k]);
		r2 += half * half;
	}
	out.radius = std::sqrt(r2);
	return !out.splats.empty();
}

}  // namespace mitiru::render
