#pragma once

/// @file SceneLook.hpp
/// @brief HE2 の `SceneContext` 相当。「絵の設定」を 1 POD に集約する (§1-12、ABI v35)。
/// `ISceneFx` は fog/tonemap/outline/shadow を個別 virtual で持ち、v22〜v29 は「設定を 1 個
/// 増やすたびに ABI 版数を上げる」を繰り返してきた (`ModuleApi.hpp` の版数台帳参照)。本 struct
/// は個別 virtual 自体をやめるのではなく (既存 override との互換が失われるため)、その呼び分けを
/// 1 箇所にまとめる薄い層を足す。末尾の `reserved` を名前付きフィールドへ差し替えるだけなら
/// `ISceneFx` の vtable も `Screen` のメンバー構成も増えないため、以後の「絵の設定を 1 個足す」
/// は ABI 版数を上げる理由にならない (sizeof が変わらない限り)。
/// flat aggregate にしてあるので `module::spawnFromJson<SceneLook>` で scene.json から直接
/// 読めるほか、`AutoReflectCount` の `isNestedAggregateReflectable` によりゲーム側の
/// `MITIRU_REFLECT_AUTO` 済み GameMemory に埋め込めば自動で再帰反映される (この型自体には
/// `MITIRU_REFLECT_AUTO` を付けない。`AutoReflect.hpp` は `Game.hpp` 経由で `Screen.hpp` に
/// 戻る循環 include を持つため、Screen から使う本ファイルに置くと使えなくなる)。
/// sgc::Colorf / sgc::Vec3f はユーザー定義コンストラクタを持ち aggregate ではないため、
/// 色・方向は生 float 配列で持ち、`applySceneLook` の中でだけ組み立て直す。

#include <cstdint>

#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/ISceneFx.hpp>

namespace mitiru::render
{

/// @brief `ISceneFx` の tonemap/ambient/outline/shadow/fog setter 群への入力をまとめた POD。
struct SceneLook
{
	float exposure = 1.0f;  ///< ISceneFx::setTonemapExposure と同じ既定
	float gamma    = 2.2f;  ///< ISceneFx::setTonemapGamma と同じ既定 (sRGB 近似)

	float ambient[3] = {0.15f, 0.15f, 0.15f};  ///< setAmbientColor の RGB (A は常に不透明)

	bool         outline          = false;
	std::int32_t outlineMode      = 0;     ///< render::OutlineMode の値。既定 0 = DepthSobel
	float        outlineWidthPx   = 1.5f;
	float        outlineThreshold = 0.12f;

	bool  shadow             = false;
	float shadowDirection[3] = {0.0f, -1.0f, 0.0f};  ///< setShadowDirection
	bool  shadowCaster       = true;   ///< 以後の描画が影を落とすか (setShadowCaster)
	bool  shadowCascaded     = false;  ///< カスケードシャドウ (setCascadedShadowEnabled)

	bool  fog        = false;
	float fogColor[3] = {0.7f, 0.78f, 0.86f};
	float fogNear    = 30.0f;
	float fogFar     = 90.0f;

	/// @brief カスケード (shadowCascaded) の分割距離と ortho の大きさをカメラ視錐台から毎フレーム決める
	///        (setCascadedShadowAutoFit)。false なら renderer 側の固定値 (split 15 / near 8 / far 20)
	bool  shadowCascadeAutoFit = false;
	/// @brief bloom (setBloom)。しきい値を超えた明部を tonemap 前の HDR 色にぼかして足す。
	///        v41 で追加。末尾に置くと 4 byte 境界の詰め物で reserved が 4 byte 足りなくなるため、
	///        直前の bool の後ろの詰め物 (offset 77) に入れる。他フィールドの offset は動かない
	bool  bloom                = false;
	/// @brief 自動フィット時に影を付ける最遠距離 (ワールド単位)。カメラの far は空まで含んで遠すぎる
	float shadowDistance       = 60.0f;
	/// @brief カスケード数 (setShadowCascadeCount)。0 = shadowCascaded に従う (true なら 2)、3 = 近/中/遠
	std::uint8_t shadowCascadeCount = 0;
	bool  outlineCaster = true;   ///< 以後の描画が輪郭線の検出に入るか (setOutlineCaster)

	// v40 (ADR 0042): bool を先に固めて float の 4 byte 境界の詰め物を 1 回で済ませる
	bool         ao        = false;  ///< SSAO (setAmbientOcclusion)。tonemap 前の色に掛ける
	/// @brief トゥーンの段数 (setToonRamp)。1 = 従来の 2 トーン (影/明)。3 = 影 / toonMidTint / 白。上限 4
	std::uint8_t toonBands = 1;
	float aoRadius   = 0.25f;  ///< 遮蔽を探す半径 (ワールド単位)
	float aoStrength = 1.0f;   ///< 遮蔽の濃さ (遮られていない割合に掛ける指数。1 = 直線)。0 で無効と同じ絵
	float toonSoftness   = 0.12f;  ///< 段の境の幅 (lambert 単位)。0.12 = 従来の smoothstep(0.44, 0.56)
	float toonMidTint[3] = {0.78f, 0.80f, 0.88f};  ///< 3 段以上の中間の帯の色 (最暗は toon3D の shadowTint)
	float toonSpecular      = 0.0f;   ///< 段付きハイライトの強さ 0..1 (setToonSpecular)。材質の specular が乗る
	float toonSpecularPower = 32.0f;  ///< ハイライトの鋭さ (Blinn-Phong 指数)

	// v41 (ADR 0043): reserved 20 byte を使い切った。以後の追加は sizeof を変える (= ABI 版数を上げる) か、
	// bool の後ろの詰め物 (offset 21..23 / 37..39 / 78..79) に収まる 1 byte 幅のものに限る
	float bloomThreshold = 1.0f;  ///< bloom が拾い始める明るさ (HDR 線形。1.0 = 白より明るい所だけ)
	float bloomStrength  = 0.3f;  ///< ぼかした明部を足す係数。0 で無効と同じ絵
	float shadowSoftness = 1.0f;  ///< 影の PCF の端のタップまでの距離 (影マップの texel 単位。setShadowSoftness)。1.0 = 従来
	float saturation     = 1.0f;  ///< ACES 後の彩度 (setColorGrade)。0 でグレー、1 で無変換
	float contrast       = 1.0f;  ///< ACES 後のコントラスト。中間灰 0.18 を軸に伸ばす。1 で無変換

	// v42 (ADR 0044): 詰め物が無かったので sizeof を 140 → 172 に伸ばした。新しい reserved は
	// また 20 byte 積んである (float 5 個ぶん)
	float outlineFadeNear = 0.0f;  ///< ここまでの距離は輪郭線を全部出す (視点からのビュー距離)
	float outlineFadeFar  = 0.0f;  ///< ここで outlineFadeMin まで薄くなる。near 以下なら距離減衰しない (既定)
	float outlineFadeMin  = 0.0f;  ///< far より遠くで残す線の不透明度 0..1

	// v43 (ADR 0045)。ここから下の 48 byte は reserved 20 に収まらないので sizeof が 220 になる
	/// @brief 上から来る空の環境光。ambientGround と両方 0 なら平坦な ambient を使う (既定 = 従来の絵)
	float ambientSky[3]    = {0.0f, 0.0f, 0.0f};
	/// @brief 下から返る地面の環境光。法線の y で ambientSky との間を混ぜる
	float ambientGround[3] = {0.0f, 0.0f, 0.0f};
	float rimStrength = 0.0f;  ///< シルエット際の縁光の強さ。0 = 縁光なし (既定 = 従来の絵)
	float rimPower    = 3.0f;  ///< 縁の細さ。大きいほど際だけに寄る
	float rimColor[3] = {1.0f, 1.0f, 1.0f};
	/// @brief 輪郭線を下の色へどれだけ寄せるか 0..1。1 = 従来のインク色、0.5 で下の色が半分透ける
	float outlineDarken = 1.0f;

	// v44 (ADR 0046): reserved 20 のうち 16 byte を使った。sizeof は 220 のまま
	/// @brief ここより遠い画素 (視点からのビュー距離) からぼかし始める。dofStrength 0 なら被写界深度なし (既定)
	float dofStart    = 0.0f;
	/// @brief ぼけが最大になる距離。start 以下なら start + 1 とみなす
	float dofEnd      = 0.0f;
	/// @brief 最大のぼけ半径 (720p の画素)。高さに比例して伸びる。0 = 無効
	float dofStrength = 0.0f;
	/// @brief 影の比較に足す深度の余白 (ワールド単位)。受ける面が光に傾くほど tan で伸ばす。
	///        0 = 従来の `0.001 * max(shadowSoftness, 1)` (影マップ深度の単位で、奥行き 100 なら 10 cm 以上)
	float shadowBias  = 0.0f;

	std::uint8_t reserved[4] = {};  ///< 次の追加ぶん。ここから名前付きに削るなら sizeof は動かない
};

}  // namespace mitiru::render

namespace mitiru::render
{

// v35 時点でのサイズを固定する。reserved を名前付きフィールドへ差し替えるだけの変更は
// この数値を保てば ABI 版数を上げなくてよい (足りなくなったら reserved を削って詰める)。
static_assert(sizeof(SceneLook) == 220, "SceneLook wire size 固定 (v44。reserved は残り 4 byte = float 1 個ぶん)");

/// @brief SceneLook の値を ISceneFx の既存 setter 群へ一括で流す。個別 setter を毎回
/// 呼び分ける代わりにこの 1 関数を呼べば「絵の設定」がまとめて反映される。呼び先は v22〜v29 の
/// 既存 setter と、v38 で ISceneFx 末尾に足した setCascadedShadowAutoFit、v39 の setOutlineCaster、
/// v40 の setAmbientOcclusion / setToonRamp / setToonSpecular、v41 の setBloom / setShadowSoftness / setColorGrade、
/// v42 の setOutlineFade、v43 の setHemisphereAmbient / setRimLight / setOutlineDarken、v44 の setDepthOfField / setShadowBias。
inline void applySceneLook(ISceneFx& fx, const SceneLook& look) noexcept
{
	fx.setTonemapExposure(look.exposure);
	fx.setTonemapGamma(look.gamma);
	fx.setAmbientColor(sgc::Colorf{look.ambient[0], look.ambient[1], look.ambient[2], 1.0f});
	fx.setHemisphereAmbient(
		sgc::Colorf{look.ambientSky[0], look.ambientSky[1], look.ambientSky[2], 1.0f},
		sgc::Colorf{look.ambientGround[0], look.ambientGround[1], look.ambientGround[2], 1.0f});

	fx.setOutlineEnabled(look.outline);
	fx.setOutlineMode(static_cast<OutlineMode>(look.outlineMode));
	fx.setOutlineParams(look.outlineWidthPx, look.outlineThreshold);
	fx.setOutlineCaster(look.outlineCaster);
	fx.setOutlineFade(look.outlineFadeNear, look.outlineFadeFar, look.outlineFadeMin);
	fx.setOutlineDarken(look.outlineDarken);

	fx.setShadowEnabled(look.shadow);
	fx.setShadowDirection(
		sgc::Vec3f{look.shadowDirection[0], look.shadowDirection[1], look.shadowDirection[2]});
	fx.setShadowCaster(look.shadowCaster);
	fx.setCascadedShadowEnabled(look.shadowCascaded);
	fx.setCascadedShadowAutoFit(look.shadowCascadeAutoFit, look.shadowDistance);
	fx.setShadowCascadeCount(look.shadowCascaded ? (look.shadowCascadeCount >= 2 ? look.shadowCascadeCount : 2) : 1);

	fx.setFog(look.fog,
	          sgc::Colorf{look.fogColor[0], look.fogColor[1], look.fogColor[2], 1.0f},
	          look.fogNear, look.fogFar);

	fx.setAmbientOcclusion(look.ao, look.aoRadius, look.aoStrength);
	fx.setToonRamp(look.toonBands, look.toonSoftness,
	               sgc::Colorf{look.toonMidTint[0], look.toonMidTint[1], look.toonMidTint[2], 1.0f});
	fx.setToonSpecular(look.toonSpecular, look.toonSpecularPower);
	fx.setRimLight(look.rimStrength, look.rimPower,
	               sgc::Colorf{look.rimColor[0], look.rimColor[1], look.rimColor[2], 1.0f});

	fx.setBloom(look.bloom, look.bloomThreshold, look.bloomStrength);
	fx.setShadowSoftness(look.shadowSoftness);
	fx.setColorGrade(look.saturation, look.contrast);

	fx.setDepthOfField(look.dofStart, look.dofEnd, look.dofStrength);
	fx.setShadowBias(look.shadowBias);
}

}  // namespace mitiru::render
