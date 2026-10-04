#pragma once

/// @file ISceneFx.hpp
/// @brief 「絵の設定」(skybox/ambient/shadow/outline/tonemap/shaderMode) 用インターフェース
/// @details §6-2: 肥大化した IRenderer3D から fx 系 setter/getter を分離したもの。
///          `IRenderer3D::sceneFx()` が対応バックエンドでは自分自身を返す（未対応なら nullptr）。
///          既定実装は全て IRenderer3D 分割前と同じ no-op / 既定値（後方互換）。

#include <cstdint>

#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/RendererEnums3D.hpp>

namespace mitiru::render
{

/// @brief 3D シーンの見た目設定インターフェース
class ISceneFx
{
public:
	virtual ~ISceneFx() = default;

	// ── Skybox / 環境キューブマップ ──────────────────────────────

	/// @brief キューブマップ skybox をセットする
	/// @details バックエンドは内部で GPU リソースを構築し、
	///          以降の `beginFrame()` の直後（メッシュ描画より前）に
	///          深度=1.0 の最遠面として skybox を描画する。
	///          DX11 / DX12 で実装される。空 cubemap を渡すと未設定状態に戻す。
	virtual void setSkybox(const Cubemap& /*cubemap*/) {}

	/// @brief skybox 描画の有効/無効
	/// @details `setSkybox()` で cubemap がセット済みでも、これを false に
	///          すれば一時的に skybox 描画をスキップできる。
	virtual void setSkyboxEnabled(bool /*enabled*/) {}

	/// @brief skybox 描画が有効か
	[[nodiscard]] virtual bool isSkyboxEnabled() const noexcept { return false; }

	/// @brief IBL (Image-Based Lighting) 用の環境キューブマップをセットする
	/// @details `setSkybox()` とは別系統。バックエンドは内部で `Cubemap::irradiance()` /
	///          `Cubemap::prefilterSpecular()` を使い diffuse/specular 用の畳み込み済み
	///          キューブマップを生成して GPU へ upload する。`ShaderMode3D::PBR` の
	///          描画でのみ参照される。DX12 のみ実装。DX11 では no-op（IBL なしの
	///          定数アンビエントにフォールバック）。空 cubemap を渡すと未設定に戻る。
	virtual void setEnvironment(const Cubemap& /*cubemap*/) {}

	// ── アンビエント ──────────────────────────────────────────

	/// @brief シーンのアンビエント色を設定する
	/// @param color RGB アンビエント色（A は通常無視される）
	/// @details 既存の Renderer3DConfig::defaultAmbient と同じ意味のシーン全体ベース光。
	///          初期値はバックエンド初期化時の defaultAmbient と一致するため、
	///          このメソッドを呼ばなければ既存挙動と同一。
	///          DX11 / DX12 で実装され、それ以外のバックエンドでは no-op。
	virtual void setAmbientColor(const sgc::Colorf& /*color*/) {}

	/// @brief 現在のシーンアンビエント色を返す
	/// @return 直前に setAmbientColor() で設定された色、未設定なら defaultAmbient
	[[nodiscard]] virtual sgc::Colorf ambientColor() const noexcept
	{
		return sgc::Colorf{0.15f, 0.15f, 0.15f, 1.0f};
	}

	// ── シェーダーモード ──────────────────────────────────────

	/// @brief シェーダーモードを設定する（トゥーン、フラット等）
	/// @param mode シェーダーモード
	virtual void setShaderMode([[maybe_unused]] ShaderMode3D mode) {}

	// ── 影（DX12 実装、DX11 は no-op） ──

	/// @brief 影（シャドウマップ）の有効/無効を設定する
	virtual void setShadowEnabled(bool /*enabled*/) {}
	/// @brief 影を落とす平行光の向きを設定する（通常はライトの direction と揃える）
	virtual void setShadowDirection(const sgc::Vec3f& /*dir*/) {}
	/// @brief 以後の描画が影を落とすかを切り替える (ABI v29)
	/// @details 一人称の武器のように、画面には出るが世界には影を落とさないものに使う。
	///          フレーム頭で true に戻る。未対応では no-op。
	virtual void setShadowCaster(bool enabled) { (void)enabled; }

	/// @brief カスケードシャドウ (B13) の有効/無効を設定する
	/// @details 2 カスケード (近距離/遠距離) を距離で切り替え、近距離側はテクセル密度を
	///          上げた狭い ortho box を使う。既定は無効 (従来の単一シャドウマップ)。
	///          DX12 実装、DX11 は no-op。
	virtual void setCascadedShadowEnabled(bool /*enabled*/) {}

	/// @brief カスケードシャドウが有効か
	[[nodiscard]] virtual bool isCascadedShadowEnabled() const noexcept { return false; }

	// ── アウトライン（DX12 実装、DX11 は no-op） ──

	/// @brief アウトライン描画の有効/無効を設定する
	virtual void setOutlineEnabled(bool /*enabled*/) {}

	/// @brief アウトライン描画が有効かどうかを返す
	[[nodiscard]] virtual bool isOutlineEnabled() const noexcept { return false; }

	/// @brief アウトラインモードを設定する
	virtual void setOutlineMode(OutlineMode /*mode*/) {}

	/// @brief 現在のアウトラインモードを返す
	[[nodiscard]] virtual OutlineMode outlineMode() const noexcept { return OutlineMode::DepthSobel; }

	/// @brief アウトラインの線幅 (px) と検出しきい値を設定する (ABI v27)
	/// @details しきい値が小さいほど線が増える。未対応バックエンドでは no-op。
	virtual void setOutlineParams(float widthPx, float threshold)
	{
		(void)widthPx;
		(void)threshold;
	}

	// ── 絵づくり (ABI v27, v28) ───────────────────────────────

	/// @brief トゥーンの影部でアルベドに掛ける係数を設定する
	/// @details 未対応バックエンドでは no-op。
	virtual void setToonShadowTint(const sgc::Colorf& tint) { (void)tint; }

	/// @brief 距離フォグを設定する (ABI v28)
	/// @details nearDist から farDist にかけて color へ染める。未対応では no-op。
	virtual void setFog(bool enabled, const sgc::Colorf& color, float nearDist,
	                    float farDist)
	{
		(void)enabled;
		(void)color;
		(void)nearDist;
		(void)farDist;
	}

	// ── HDR トーンマップ (ENG-106) ─────────────────────────────

	/// @brief 露出 (exposure) を設定する
	/// @details トーンマップ前の線形係数。1.0 が標準。明るくしたいなら 1 超、
	///          暗くしたいなら 1 未満。屋外シーンは 0.5〜1.0、暗所は 1.5〜3.0 が目安。
	///          DX12 のみ実装。DX11 では no-op。
	virtual void setTonemapExposure(float /*exposure*/) {}

	/// @brief 現在の exposure 値を返す
	[[nodiscard]] virtual float tonemapExposure() const noexcept { return 1.0f; }

	/// @brief 表示ガンマを設定する (default 2.2 = sRGB そのもの)
	/// @details tonemap 後の線形の色に `pow(c, 2.2 / gamma)` を掛けてから sRGB へ戻す。2.2 より大きいと中間調が明るくなる。
	virtual void setTonemapGamma(float /*gamma*/) {}

	/// @brief 現在の gamma 値を返す
	[[nodiscard]] virtual float tonemapGamma() const noexcept { return 2.2f; }

	/// @brief カスケードシャドウの分割距離と ortho の大きさをカメラ視錐台から毎フレーム決める (v38)。
	///        maxDistance は影を付ける最遠距離。DX12 のみ実装 (`DirectionalShadowConfig::autoFitCascades`)
	virtual void setCascadedShadowAutoFit(bool /*enabled*/, float /*maxDistance*/) {}

	/// @brief カスケード数を 1〜3 で指定する (v38)。1 = エンジンの既定 (DX12 は視錐台に合わせた段)、2 = setCascadedShadowEnabled(true) と同じ、
	///        3 = 近/中/遠。DX12 のみ実装
	virtual void setShadowCascadeCount(int /*count*/) {}

	/// @brief 以後の描画を輪郭線 (post-process outline) の検出から外す (v39、ADR 0041)。
	///        テクスチャ付きの板の立ち絵のように、面の外周で深度が段になるが絵の周りに枠を
	///        引きたくないものに false で挟む。フレーム頭で true に戻る。DX12 のみ実装
	///        (法線 RT に印を書き、outline パスがその画素を縁として扱わない)
	virtual void setOutlineCaster(bool /*enabled*/) {}

	// ── 商業トゥーン寄せ (v40、ADR 0042) ─────────────────────────

	/// @brief SSAO (画面空間の環境遮蔽)。物と床の継ぎ目・物どうしの隙間を暗くする。
	///        radius は遮蔽を探す半径 (ワールド単位)、strength は濃さ (0 で素通し)。
	///        DX12 のみ実装 (深度 + 法線 RT から半球サンプル 16 本 → 深度で重み付けした分離ぼかし →
	///        tonemap 前の HDR 色に乗算)。描画単位の除外は無い
	virtual void setAmbientOcclusion(bool /*enabled*/, float /*radius*/, float /*strength*/) {}

	/// @brief トゥーンの段数と境の柔らかさ。bands=1 (と 2) は従来の 2 トーン (影 = setToonShadowTint、明 = 白)、
	///        3 以上は影 / midTint / 白を等分の lambert しきい値で刻む (上限 4)。softness は境の幅
	///        (lambert 単位。0.12 が従来の smoothstep(0.44, 0.56) と同じ)。
	///        bands=0 は段を作らず、softness を巻き込み量にした wrap lambert の滑らかな陰にする (v43)。
	///        DX12 のみ実装
	virtual void setToonRamp(int /*bands*/, float /*softness*/, const sgc::Colorf& /*midTint*/) {}

	/// @brief 段付き Blinn-Phong ハイライト。strength 0 で無し、1 で光源色 × 材質 specular がそのまま乗る。
	///        power は指数 (大きいほど点が小さい)。境の幅は setToonRamp の softness を共有。DX12 のみ実装
	virtual void setToonSpecular(float /*strength*/, float /*power*/) {}

	// ── 商業トゥーン寄せ 残り 3 つ (v41、ADR 0043) ─────────────────

	/// @brief bloom。threshold (HDR 線形) を超えた明部を 1/2 → 1/4 に落としてぼかし、tonemap 前の HDR 色に
	///        strength 倍して足す。DX12 のみ実装 (半解像 RT 2 枚 + 1/4 解像 1 枚、tent フィルタで戻す)
	virtual void setBloom(bool /*enabled*/, float /*threshold*/, float /*strength*/) {}

	/// @brief 影の PCF の端のタップまでの距離 (影マップの texel 単位)。1.0 以下は 3x3 で従来の絵、それより広いと
	///        5x5 のテント重みで間を埋める (3x3 のまま広げると縁が 3 段に分かれた)。2〜3 で境が柔らかくなる。
	///        描画結果だけが変わり、InputSnapshot / リプレイには乗らない。DX12 のみ実装
	virtual void setShadowSoftness(float /*texels*/) {}

	/// @brief tonemap の後・sRGB へ戻す前に掛ける彩度とコントラスト。両方 1.0 で無変換。
	///        saturation 0 でグレー。contrast は中間灰 0.18 を軸に伸縮する。DX12 のみ実装
	virtual void setColorGrade(float /*saturation*/, float /*contrast*/) {}

	// ── 輪郭線の距離減衰 (v42、ADR 0044) ───────────────────────

	/// @brief 輪郭線を距離で薄くする。nearDist まで全部、farDist で minStrength (0..1) まで線形に
	///        落とし、それより遠くは minStrength のまま。薄くするのは線の不透明度だけで、太さは変えない
	///        (幅を細らせると 1 px を割った所で線が途切れてちらつく)。farDist <= nearDist なら減衰しない
	///        = 既定 (0, 0, 0) は従来の絵。OutlineMode::DepthSobel の DX12 実装のみ
	virtual void setOutlineFade(float /*nearDist*/, float /*farDist*/, float /*minStrength*/) {}

	// ── 半球アンビエント / 縁光 / 輪郭線の色 (v43、ADR 0045) ──────

	/// @brief 環境光を上下 2 色にする。世界法線の y で `lerp(ground, sky, n.y * 0.5 + 0.5)`。
	///        **両方が真っ黒なら setAmbientColor の平坦な色に落ちる** ので、既定は従来の絵と bit 同一。
	///        空色を上から、地面の照り返しを下から当てると、平坦な 1 色より立体の向きが読める。
	///        DX12 の `ShaderMode3D::Toon` のみ実装
	virtual void setHemisphereAmbient(const sgc::Colorf& /*sky*/, const sgc::Colorf& /*ground*/) {}

	/// @brief シルエット際を光らせる縁光。`pow(1 - NdotV, power)` に strength と color を掛けて足す。
	///        strength 0 で無し (既定 = 従来の絵)。DX12 の `ShaderMode3D::Toon` のみ実装
	virtual void setRimLight(float /*strength*/, float /*power*/, const sgc::Colorf& /*color*/) {}

	/// @brief 輪郭線を下の色へどれだけ寄せるか 0..1。線の画素は `lerp(下の色, インク色, 被覆率 * darken)` になる。
	///        1 が従来の絵、0 で線が消える。純黒の線は彩度の高い絵の中で浮くので下の色を透かす用
	virtual void setOutlineDarken(float /*darken*/) {}

	// ── 遠景のぼけ / 影の余白 (v44、ADR 0046) ─────────────────────

	/// @brief 遠くだけをぼかす被写界深度。ビュー距離 start から end にかけてぼけ半径を 0 → strength
	///        (720p の画素、画面の高さに比例) へ伸ばす。tonemap の後・FXAA の前に掛けるので HUD はぼけない。
	///        手前の物の縁が奥のぼけへ滲まないよう、各タップは自分の深度で決まる半径が中心まで届くときだけ数える。
	///        strength 0 で無し (既定 = 従来の絵)。DX12 のみ実装
	virtual void setDepthOfField(float /*start*/, float /*end*/, float /*strength*/) {}

	/// @brief 影の比較に足す深度の余白をワールド単位で与える。受ける面が光に対して傾くほど tan で伸ばす。
	///        0 で従来の余白 (影マップ深度で 0.001 × max(softness, 1)。奥行き 100 の影マップでは 10 cm 以上あり、
	///        それより低い物の落ち影が消える)。DX12 のみ実装
	virtual void setShadowBias(float /*worldUnits*/) {}
};

} // namespace mitiru::render
