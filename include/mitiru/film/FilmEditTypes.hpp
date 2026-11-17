#pragma once

/// @file FilmEditTypes.hpp
/// @brief 映像編集の読み取り専用テーブル。ショット、トランジション、time_remap の対応、タイトルを扱う。書式は docs/FILM_EDIT.md。
/// @details 時間軸は、映像の時間とワールドの時間 τ(t) の 2 つ。映像の時間は fade、letterbox、字幕、キュー、タイトル、トランジションに使う。ワールドの時間はカメラ、小道具、光、パーティクル、look_tracks に使う。time_remap は τ だけを変える。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/film/FilmLook.hpp>
#include <mitiru/narrative/Sequence.hpp>

namespace mitiru::film
{

struct Film;

enum class TransitionKind : std::uint8_t { None, Dissolve, Wipe, Dip };
enum class WipeDir : std::uint8_t { Right, Left, Down, Up };   ///< 縁が動いていく向き

/// @brief 入ってくるショットの先頭から duration 秒かけて、前のショットから切り替える
struct Transition
{
	TransitionKind kind = TransitionKind::None;
	float          duration = 0.0f;
	WipeDir        dir = WipeDir::Right;
	float          softness = 0.05f;   ///< ワイプの縁のぼかし (画面の幅に対する割合)
};

struct LookPatch
{
	const LookField* field = nullptr;
	float            v[3]{};
};

struct ShotEdit
{
	std::string              name;
	std::string              source;       ///< include したファイル名 (書き出しの記録用)
	float                    start = 0.0f;   ///< 映像の時間
	float                    end = 0.0f;
	float                    worldStart = 0.0f;   ///< start に対応するワールドの時間
	float                    cueShift = 0.0f;     ///< キューをこの秒数ずらした (負が J カット、正が L カット。書き出しの記録用)
	std::vector<std::string> cameraNames;
	std::vector<int>         cameras;       ///< seq.cameras の添字 (読み込みで決める)
	std::vector<LookPatch>   look;          ///< このショットだけ上書きする SceneLook のフィールド
	bool                     hasFade = false;
	std::vector<narrative::FloatKey> fade;  ///< 映像の時間。あればルートの fade の代わりにこの区間で効く
	Transition               transitionIn;
};

/// @brief 映像の時間 [start, end) で、ワールドの時間が speed 倍で進む区間
struct TimeSeg
{
	float start = 0.0f;
	float end = 0.0f;
	float speed = 1.0f;
};

enum class TitleKind : std::uint8_t { Title, LowerThird };

struct TitleCard
{
	TitleKind   kind = TitleKind::Title;
	std::string text;
	std::string sub;
	float       start = 0.0f;
	float       end = 0.0f;
	float       fadeIn = 0.4f;
	float       fadeOut = 0.4f;
	float       x = 0.5f;      ///< 画面に対する割合。Title は文字の中心、LowerThird は左端
	float       y = 0.5f;
	float       size = 0.08f;  ///< 文字の高さ (画面の高さに対する割合)
	float       color[3]{1.0f, 1.0f, 1.0f};
	float       panel = 0.0f;  ///< 背面の黒いパネルの濃さ 0..1 (LowerThird の既定は 0.55)
};

struct EditData
{
	std::vector<ShotEdit>  shots;
	std::vector<TimeSeg>   timeMap;
	std::vector<TitleCard> titles;
	/// shots と同じ並び。トランジション前後のショットだけを映す Film。カメラの区間は広げてある。無い要素は null。
	std::vector<std::shared_ptr<const Film>> layerFilms;

	[[nodiscard]] bool empty() const noexcept { return shots.empty() && timeMap.empty() && titles.empty(); }
};

}  // namespace mitiru::film
