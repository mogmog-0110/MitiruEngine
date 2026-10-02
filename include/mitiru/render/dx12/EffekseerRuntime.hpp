#pragma once

/// @file EffekseerRuntime.hpp
/// @brief Effekseer (パーティクル演出) をエンジンの DX12 デバイスとコマンドリストの上で描く。-DMITIRU_WITH_EFFEKSEER=ON の時だけ実体がある
///
/// エフェクトは「出した時刻からの経過秒」で描く。毎フレーム draw(path, 位置, ..., 経過秒) を積むと、
/// その経過秒の姿 (60fps 基準の整数フレームへ切り捨て) を描く。経過秒はゲームが GameMemory に持つ値から
/// 出すので、巻き戻し・replay・分岐でも同じフレームには同じ姿が出る。前のフレームで描いた同じ
/// エフェクトの続きなら差分だけ進め、時刻が戻っていれば作り直して進め直す。そのフレームに積まれなかった
/// エフェクトは消える (描くのをやめれば終わる)。
///
/// Effekseer のヘッダはここに出さない (src/effekseer/EffekseerRuntime.cpp に閉じる)。

#if defined(MITIRU_HAS_EFFEKSEER)

#include <d3d12.h>
#include <dxgiformat.h>

#include <memory>
#include <string>

#include "sgc/math/Vec3.hpp"

namespace mitiru::render::fx
{

/// @brief Screen::camera3D と同じ規約のカメラ (右手系、縦 FOV はラジアン、深度 0..1)
struct EffectCamera
{
	sgc::Vec3f eye;
	sgc::Vec3f target;
	sgc::Vec3f up{0.0f, 1.0f, 0.0f};
	float fovYRad = 0.7853982f;
	float aspect = 16.0f / 9.0f;
	float nearZ = 0.1f;
	float farZ = 500.0f;
};

struct EffekseerTarget
{
	DXGI_FORMAT colorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	DXGI_FORMAT depthFormat = DXGI_FORMAT_D32_FLOAT;
	int sampleCount = 1;
	int framesInFlight = 3;
};

class EffekseerRuntime
{
public:
	/// @return 作れなければ nullptr と error に理由
	[[nodiscard]] static std::unique_ptr<EffekseerRuntime> create(ID3D12Device* device, ID3D12CommandQueue* queue,
		const EffekseerTarget& target, std::string& error);

	virtual ~EffekseerRuntime() = default;

	/// @brief このフレームに描くエフェクトを積む
	/// @param path .efkefc / .efk (host の作業ディレクトリからの相対パス)。テクスチャはその隣から読む
	/// @param key 同じ path を同時に複数出す時の区別 (null / 空文字でよい)
	/// @param ageSec 出してからの経過秒 (負なら描かない)
	virtual void draw(const char* path, const sgc::Vec3f& position, float rotYDeg, float scale, const char* key,
		float ageSec) = 0;

	/// @brief 積んだエフェクトを cmd に記録する。呼ぶ側が描き先 (色 + 深度) と viewport を束縛しておく
	virtual void render(ID3D12GraphicsCommandList* cmd, const EffectCamera& camera) = 0;
};

} // namespace mitiru::render::fx

#endif // MITIRU_HAS_EFFEKSEER
