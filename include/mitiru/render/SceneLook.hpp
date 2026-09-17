#pragma once

/// @file SceneLook.hpp
/// @brief HE2 の `SceneContext` 相当。「絵の設定」を 1 POD に集約する (§1-12、ABI v35)。
/// `ISceneFx` は fog/tonemap/outline/shadow を個別 virtual で持ち、v22〜v29 は「設定を 1 個
/// 増やすたびに ABI 版数を上げる」を繰り返してきた (`ModuleApi.hpp` の版数台帳参照)。本 struct
/// は個別 virtual 自体をやめるのではなく (既存 override との互換を壊すため)、その呼び分けを
/// 1 箇所にまとめる薄い層を足す。末尾の `reserved` を名前付きフィールドへ差し替えるだけなら
/// `ISceneFx` の vtable も `Screen` のメンバー構成も増えないため、以後の「絵の設定を 1 個足す」
/// は ABI 版数を上げる理由にならない (sizeof が変わらない限り)。
/// flat aggregate にしてあるので `module::spawnFromJson<SceneLook>` で scene.json から直接
/// 読めるほか、`AutoReflectCount` の `isNestedAggregateReflectable` によりゲーム側の
/// `MITIRU_REFLECT_AUTO` 済み GameMemory に埋め込めば自動で再帰反映される (この型自体には
/// `MITIRU_REFLECT_AUTO` を付けない。`AutoReflect.hpp` は `Game.hpp` 経由で `Screen.hpp` に
/// 戻る循環 include を持つため、Screen から使う本ファイルに置くと壊れる)。
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
	/// @brief 自動フィット時に影を付ける最遠距離 (ワールド単位)。カメラの far は空まで含んで遠すぎる
	float shadowDistance       = 60.0f;
	/// @brief カスケード数 (setShadowCascadeCount)。0 = shadowCascaded に従う (true なら 2)、3 = 近/中/遠
	std::uint8_t shadowCascadeCount = 0;

	/// @brief 将来の絵の設定用の予備領域。ここを名前付きフィールドに変える追加だけなら
	/// sizeof(SceneLook) を保つ限り ABI 版数を上げなくてよい (下の static_assert が壊れたら
	/// 予備領域を削って詰め、版数据え置きのまま追記できる)。
	std::uint8_t reserved[55] = {};
};

}  // namespace mitiru::render

namespace mitiru::render
{

// v35 時点でのサイズを固定する。reserved を名前付きフィールドへ差し替えるだけの変更は
// この数値を保てば ABI 版数を上げなくてよい (足りなくなったら reserved を削って詰める)。
static_assert(sizeof(SceneLook) == 140, "SceneLook wire size 固定 (v35。v38 で reserved 64 → 56 + 名前付き 8 byte)");

/// @brief SceneLook の値を ISceneFx の既存 setter 群へ一括で流す。個別 setter を毎回
/// 呼び分ける代わりにこの 1 関数を呼べば「絵の設定」がまとめて反映される。呼び先は v22〜v29 の
/// 既存 setter と、v38 で ISceneFx 末尾に足した setCascadedShadowAutoFit。
inline void applySceneLook(ISceneFx& fx, const SceneLook& look) noexcept
{
	fx.setTonemapExposure(look.exposure);
	fx.setTonemapGamma(look.gamma);
	fx.setAmbientColor(sgc::Colorf{look.ambient[0], look.ambient[1], look.ambient[2], 1.0f});

	fx.setOutlineEnabled(look.outline);
	fx.setOutlineMode(static_cast<OutlineMode>(look.outlineMode));
	fx.setOutlineParams(look.outlineWidthPx, look.outlineThreshold);

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
}

}  // namespace mitiru::render
