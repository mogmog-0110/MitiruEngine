#pragma once

/// @file FilmTypes.hpp
/// @brief 映像 1 本 (JSON 1 ファイル) を読み取り専用の表にしたもの。書式は docs/FILM.md。
/// @details narrative::Sequence の項目に、小道具、光、ルック、レンズの軌跡を加える。
/// 全ての値は時刻だけから求める (FilmEval.hpp)。GameMemory には時刻だけを置くため、
/// 巻き戻し、リプレイ、同じ時刻からの再撮影で同じ結果になる。

#include <cstdint>
#include <string>
#include <vector>

#include <mitiru/animation/VisemeTrack.hpp>
#include <mitiru/film/FilmEditTypes.hpp>
#include <mitiru/film/FilmScatter.hpp>
#include <mitiru/narrative/Sequence.hpp>
#include <mitiru/render/GpuParticles.hpp>
#include <mitiru/render/SceneLook.hpp>

namespace mitiru::film
{

using narrative::Vec3;

template <class T>
struct TKey
{
	float t = 0.0f;
	T     v{};
};

/// @brief 時刻つきの値の列。空ならこの項目を動かさない
template <class T>
struct Track
{
	std::vector<TKey<T>> keys;
	bool                 smooth = true;

	[[nodiscard]] bool empty() const noexcept { return keys.empty(); }
};

enum class LightKind : std::uint8_t { Sun, Point, Spot };

struct Light
{
	std::string name;
	LightKind   kind = LightKind::Point;
	Vec3        pos{};
	Vec3        dir{0.0f, -1.0f, 0.0f};   ///< 光の進む向き (太陽とスポット)
	Vec3        color{1.0f, 1.0f, 1.0f};
	float       intensity = 1.0f;
	float       range = 10.0f;
	float       innerDeg = 20.0f;
	float       outerDeg = 30.0f;
	bool        castShadow = false;
	Track<Vec3>  posTrack;
	Track<Vec3>  dirTrack;
	Track<Vec3>  colorTrack;
	Track<float> intensityTrack;
	Track<float> rangeTrack;
};

/// @brief モデルに渡すアニメの区間。区間外では重みが 0
struct PropClip
{
	std::string name;
	float       start = 0.0f;
	float       end = 1.0e9f;
	float       offset = 0.0f;    ///< start の時のクリップの時刻
	float       speed = 1.0f;
	float       blendIn = 0.0f;
	float       blendOut = 0.0f;
	bool        loop = true;
};

/// @brief モーフの動かし方 1 本。visemes が空ならキーで name のモーフ (または表情) を動かし、あれば口の表を引く (FilmMorph.hpp)
struct MorphChannel
{
	std::string            name;       ///< キー: 動かす名前。口の表: 開き具合で動かす 1 本の名前 (空なら aa ih ou ee oh の 5 本)
	Track<float>           weight;     ///< キー
	std::string            visemes;    ///< 口の表の JSON のパス (読み込みで track に読む)
	float                  start = 0.0f;   ///< 口の表の 0 秒にあたる映像の時刻
	float                  gain = 1.0f;
	animation::VisemeTrack track;
};

struct Prop
{
	std::string   name;
	std::string   shape;      ///< 組み込みメッシュ (cube / sphere / plane など)。model と同時には書けない
	std::string   model;      ///< glb のパス (assets からの相対)
	Vec3          pos{};
	Vec3          rotDeg{};   ///< x, y, z の順に回す度 (y が上まわり)
	Vec3          scale{1.0f, 1.0f, 1.0f};
	Vec3          color{1.0f, 1.0f, 1.0f};
	float         alpha = 1.0f;
	std::vector<PropClip> clips;   ///< model のアニメの区間 (JSON の clip は全期間の 1 区間として入る)
	std::vector<MorphChannel> morphs;   ///< model のモーフの重み (口・まばたき・表情)。空なら今までの描き方のまま
	Track<Vec3>   posTrack;
	Track<Vec3>   rotTrack;
	Track<Vec3>   scaleTrack;
	Track<Vec3>   colorTrack;
	Track<float>  alphaTrack;
	Track<float>  visibleTrack;   ///< 0.5 を超えると見える。空なら常に見える
};

/// @brief パーティクルの発生源。パーティクルは経過秒 (age = t - start) だけで決まり、同じ時刻では同じ姿になる
struct Emitter
{
	std::string                name;
	float                      start = 0.0f;
	float                      end = 1.0e9f;    ///< この時刻を過ぎたら描かない (パーティクルのライフタイムが残っていても)
	render::ParticleEmitterDesc desc;
};

/// @brief カメラのレンズ。フィルムの大きさと焦点距離から画角を、絞りとピント距離からぼけを決める
struct Lens
{
	float        sensorHeightMm = 20.25f;   ///< 36 mm 幅の 16:9 のフィルム
	Track<float> focalMm;
	Track<float> fstop;
	Track<float> focusDist;
	Track<float> evBias;                    ///< 露出の補正 (段)
	Track<float> distortion;                ///< 樽型 (+) と糸巻き型 (-) の歪み。後処理で掛ける (tools/film_lens.py)。|k| は 0.3 以下
};

/// @brief カメラの手ぶれ。位置 (m) と向き (度) を、周波数 freq の 2 つの正弦の和で揺らす。時刻だけで決まり、乱数は使わない
struct CameraShake
{
	float         posAmp = 0.0f;
	float         rotAmpDeg = 0.0f;
	float         freq = 1.5f;
	std::uint32_t seed = 1;
	[[nodiscard]] bool active() const noexcept { return posAmp > 0.0f || rotAmpDeg > 0.0f; }
};

/// @brief カメラが小道具を見続け、ピントを合わせ続ける指定。相対値で書くため、座標の推測が要らない
/// @brief ピントを別の小道具へ移す指定。t から over 秒かけて (smoothstep)、前の被写体までの距離から、この被写体までの距離へ動く
struct FocusRack
{
	float       t = 0.0f;
	float       over = 1.0f;
	std::string prop;
	int         index = -1;
	Vec3        offset{};
};

struct CameraRig
{
	std::string lookAtProp;        ///< 空なら鍵の target のまま
	Vec3        lookAtOffset{};
	int         lookAtIndex = -1;  ///< props の添字 (読み込みで決める)
	std::string focusProp;         ///< 空なら lens.focus の軌跡のまま。小道具までの距離にピントを合わせる
	int         focusIndex = -1;
	Vec3        focusOffset{};
	std::vector<FocusRack> racks;   ///< t の昇順。focusOn の後でピントを移す
	CameraShake shake;
};

/// @brief 時刻 t の描画に使う値を集めた結果。FilmDraw が Screen に渡す
struct LensSample
{
	bool  hasFocal = false;
	float fovDeg = 0.0f;
	bool  hasDof = false;
	float dofStart = 0.0f;
	float dofEnd = 0.0f;
	float dofStrengthPx = 0.0f;
	float evBias = 0.0f;
	float distortion = 0.0f;       ///< 歪みの k。映像を撮る側 (tools/film.py) が後処理で掛ける
	float focusDist = 0.0f;       ///< 0 = ピント距離の指定なし
	float apertureRadius = 0.0f;  ///< 口径の半径 (ワールドの単位 = m)。蓄積で本物のボケを作る時に使う
};

struct Film
{
	narrative::Sequence seq;                 ///< カメラ・字幕・合図・暗転・上下のバンド (カットシーンと同じ書式)
	int                 width = 1280;
	int                 height = 720;
	std::uint32_t       clearColor = 0x000000;
	bool                hasSky = false;
	Vec3                skyZenith{0.2f, 0.4f, 0.8f};
	Vec3                skyNadir{0.8f, 0.85f, 0.9f};
	Vec3                envZenith{0.0f, 0.0f, 0.0f};
	Vec3                envNadir{0.0f, 0.0f, 0.0f};
	bool                hasEnv = false;
	std::string         envHdr;                  ///< 空でなければ空と環境光に使う正距円筒の HDR (JSON の hdri、ADR 0148)
	float               envHdrIntensity = 1.0f;
	float               envHdrYawDeg = 0.0f;
	float               shadowPenumbraDeg = 0.0f;   ///< 太陽の見かけの半径 (度)。0 = 硬い影。標本ごとに光の向きをずらして半影を作る (ADR 0150)
	render::SceneLook   look{};
	std::vector<std::pair<std::string, std::vector<narrative::FloatKey>>> lookTracks;   ///< 項目名 → 時刻つきの値
	std::vector<Prop>   props;
	std::vector<Light>  lights;
	std::vector<Emitter> emitters;
	Outdoor              outdoor;            ///< 地形・水面の world.json (world が空なら無し)
	std::vector<Scatter> scatters;           ///< 草・木・岩のような数の多い置き物 (FilmScatter.hpp)
	std::vector<Lens>   lenses;             ///< seq.cameras と同じ並び
	std::vector<CameraRig> rigs;             ///< seq.cameras と同じ並び
	std::vector<std::string> sounds;         ///< 参照している音の名前 (確認用)
	EditData            edit;                ///< ショット・トランジション・time_remap の対応・タイトル (FilmEdit.hpp)
};

}  // namespace mitiru::film
