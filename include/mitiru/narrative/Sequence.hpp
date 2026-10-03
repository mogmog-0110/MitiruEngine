#pragma once

/// @file Sequence.hpp
/// @brief カットシーン 1 本 (JSON 1 ファイル) を読み取り専用の表にしたもの。書式は docs/CUTSCENES.md。
/// @details 軌跡は時刻だけの関数として評価する (SequencePlayer.hpp)。GameMemory には再生の時刻だけを置くので、
///          巻き戻し・リプレイ・分岐・Rewind 窓の scrub がそのままカットシーンにも効く。
///          仮想カメラは区間 [start, end) と priority を持ち、各時刻で一番高いものが映る。替わり目は入る側の
///          blendIn 秒かけて前のカメラから混ぜる。この「どの時刻に誰が映るか」は読み込みの時に区間の表にしておく。

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/action/ActionMath.hpp>
#include <mitiru/core/Localization.hpp>
#include <mitiru/narrative/NarrativeCue.hpp>

namespace mitiru::narrative
{

using Vec3 = action::Vec3;

struct CameraKey
{
	float t = 0.0f;
	Vec3  eye{};
	Vec3  target{0.0f, 0.0f, 1.0f};
	float fovDeg = 55.0f;
	float rollDeg = 0.0f;   ///< 視線まわりの傾き。正で画面の右へ傾く (camera3D と同じ向き)
};

struct SequenceCamera
{
	std::string            name;
	int                    priority = 0;
	float                  start = 0.0f;
	float                  end = 0.0f;
	float                  blendIn = 0.0f;  ///< 映り始めに前のカメラから混ぜる秒数 (0 で切り替え)
	float                  nearDist = 0.1f;
	float                  farDist = 500.0f;
	bool                   smooth = true;   ///< false で鍵の間を直線で結ぶ
	std::vector<CameraKey> keys;
};

struct ActorKey
{
	float t = 0.0f;
	Vec3  position{};
	float yawDeg = 0.0f;   ///< 上 (+Y) まわりの向き。0 で +Z を向く。巻かずに書く (350 → 370 で 20 度回る)
};

struct SequenceActor
{
	std::string           name;
	std::uint32_t         id = 0;
	bool                  smooth = true;
	std::vector<ActorKey> keys;
};

/// @brief 役者に流すアニメの区間。名前はゲームがモデルのクリップ (findClip) へ引き当てる
struct SequenceClip
{
	int         actor = 0;   ///< actors の添字
	std::string clip;
	float       start = 0.0f;
	float       end = 0.0f;
	float       offset = 0.0f;   ///< start の時のクリップの時刻
	float       speed = 1.0f;
	float       blendIn = 0.0f;
	float       blendOut = 0.0f;
};

struct SequenceCue
{
	float t = 0.0f;
	Cue   cue;
};

struct Subtitle
{
	float       start = 0.0f;
	float       end = 0.0f;
	std::string speaker;
	std::string text;     ///< 表示する文言 (localize で訳に替わる)
	std::string source;   ///< 書かれたままの文言
	std::string key;      ///< 訳の鍵。空なら訳さない
};

struct FloatKey
{
	float t = 0.0f;
	float v = 0.0f;
};

/// @brief ある時刻の区間で映るカメラ。winner / previous が -1 なら、ゲームのカメラ
struct CameraSegment
{
	float start = 0.0f;
	float end = 0.0f;
	int   winner = -1;
	int   previous = -1;   ///< 替わる前に映っていたもの
	int   runFirst = 0;    ///< winner が映り続けている最初の区間の添字
};

class Sequence
{
public:
	std::string                name;
	std::uint32_t              id = 0;
	float                      duration = 0.0f;
	bool                       skippable = true;
	float                      blendOut = 0.0f;   ///< カメラが無くなった後、ゲームのカメラへ戻す秒数
	std::vector<SequenceCamera> cameras;
	std::vector<SequenceActor> actors;
	std::vector<SequenceClip>  clips;
	std::vector<SequenceCue>   cues;       ///< 時刻の昇順 (同じ時刻は書いた順)
	std::vector<Subtitle>      subtitles;
	std::vector<FloatKey>      fade;       ///< 黒で覆う量 0..1
	std::vector<FloatKey>      letterbox;  ///< 上下の帯の量 0..1
	std::vector<CameraSegment> segments;   ///< 時刻の昇順。最初の区間の前と最後の区間の後はゲームのカメラ

	[[nodiscard]] int findActor(std::string_view actorName) const noexcept
	{
		for (std::size_t i = 0; i < actors.size(); ++i)
		{
			if (actors[i].name == actorName) { return static_cast<int>(i); }
		}
		return -1;
	}

	/// @brief 字幕を language の訳に替える。その言語の訳が無ければ書いたまま
	void localize(const LocalizationManager& loc, const std::string& language)
	{
		for (Subtitle& s : subtitles)
		{
			s.text = s.key.empty() ? s.source : loc.find(s.key, language).value_or(s.source);
		}
	}
};

/// @brief cameras から区間の表を作る (読み込みの最後に 1 度)
inline void buildCameraSegments(Sequence& seq)
{
	std::vector<float> cuts;
	for (const SequenceCamera& c : seq.cameras) { cuts.push_back(c.start); cuts.push_back(c.end); }
	std::sort(cuts.begin(), cuts.end());
	cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
	seq.segments.clear();
	for (std::size_t i = 0; i + 1 < cuts.size(); ++i)
	{
		CameraSegment s;
		s.start = cuts[i];
		s.end = cuts[i + 1];
		const float mid = 0.5f * (s.start + s.end);
		for (std::size_t c = 0; c < seq.cameras.size(); ++c)
		{
			const SequenceCamera& cam = seq.cameras[c];
			const bool active = cam.start <= mid && mid < cam.end;
			if (active && (s.winner < 0 || cam.priority > seq.cameras[static_cast<std::size_t>(s.winner)].priority))
			{
				s.winner = static_cast<int>(c);
			}
		}
		const CameraSegment* before = seq.segments.empty() ? nullptr : &seq.segments.back();
		const bool continues = before != nullptr && before->winner == s.winner;
		s.previous = continues ? before->previous : (before != nullptr ? before->winner : -1);
		s.runFirst = continues ? before->runFirst : static_cast<int>(seq.segments.size());
		seq.segments.push_back(s);
	}
}

}  // namespace mitiru::narrative

#include <mitiru/narrative/detail/SequenceLoad.hpp>
