#pragma once
/// @file MotifEngine.hpp
/// @brief モチーフ変奏エンジン — 短い音楽的フレーズから変奏を生成する
/// @details 転回・逆行・移高・拡大・縮小・末尾変化などの古典的作曲技法を
///          プログラマティックに適用し、モチーフ展開を支援する。
///
/// @code
/// using namespace mitiru_mml;
/// Motif m;
/// m.degrees = {1, 3, 5, 8, 7};
/// m.lengths = {8, 8, 8, 4, 4};
/// m.rests   = {false, false, false, false, false};
///
/// auto inv = MotifEngine::invert(m, 5);     // 5度を軸に転回
/// auto ret = MotifEngine::retrograde(m);    // 逆行
/// auto tr  = MotifEngine::transpose(m, 2);  // 2度上に移高
/// auto aug = MotifEngine::augment(m);       // 音価2倍
/// auto dim = MotifEngine::diminish(m);      // 音価半分
///
/// Scale scale(Key::C, ScaleType::Major);
/// std::string mml = MotifEngine::toMml(m, scale, 5);
/// @endcode

#include <mitiru_mml/MusicTheory.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace mitiru_mml
{

/// @brief モチーフ（短い音楽的フレーズ）
struct Motif
{
	std::vector<int> degrees;      ///< スケール度数列（1始まり）
	std::vector<int> lengths;      ///< 音長列（MMLのL値: 4=四分, 8=八分, 16=十六分...）
	std::vector<bool> rests;       ///< 休符フラグ（trueなら該当位置は休符）
};

/// @brief モチーフ変奏エンジン
/// @details 古典的な作曲技法（転回・逆行・移高・拡大・縮小・末尾変化）を
///          モチーフに適用し、変奏を生成する静的ユーティリティクラス。
class MotifEngine
{
public:
	/// @brief モチーフを転回する（音程を軸に対して上下反転）
	/// @param m 元のモチーフ
	/// @param pivot 反転の軸となるスケール度数
	/// @return 転回されたモチーフ
	/// @details 各度数をpivotを中心に反転する。例: pivot=5, degree=3 → 7
	[[nodiscard]] static Motif invert(const Motif& m, int pivot)
	{
		Motif result;
		result.lengths = m.lengths;
		result.rests = m.rests;
		result.degrees.reserve(m.degrees.size());
		for (int d : m.degrees)
		{
			result.degrees.push_back(pivot * 2 - d);
		}
		return result;
	}

	/// @brief モチーフを逆行する（音列を逆順に）
	/// @param m 元のモチーフ
	/// @return 逆行されたモチーフ
	[[nodiscard]] static Motif retrograde(const Motif& m)
	{
		Motif result;
		result.degrees.assign(m.degrees.rbegin(), m.degrees.rend());
		result.lengths.assign(m.lengths.rbegin(), m.lengths.rend());
		result.rests.assign(m.rests.rbegin(), m.rests.rend());
		return result;
	}

	/// @brief モチーフを移高する（全音程をN度上げる）
	/// @param m 元のモチーフ
	/// @param steps 移動するスケール度数（正=上、負=下）
	/// @return 移高されたモチーフ
	[[nodiscard]] static Motif transpose(const Motif& m, int steps)
	{
		Motif result;
		result.lengths = m.lengths;
		result.rests = m.rests;
		result.degrees.reserve(m.degrees.size());
		for (int d : m.degrees)
		{
			result.degrees.push_back(d + steps);
		}
		return result;
	}

	/// @brief モチーフを拡大する（音価を2倍に）
	/// @param m 元のモチーフ
	/// @return 拡大されたモチーフ（L8→L4, L16→L8 等）
	[[nodiscard]] static Motif augment(const Motif& m)
	{
		Motif result;
		result.degrees = m.degrees;
		result.rests = m.rests;
		result.lengths.reserve(m.lengths.size());
		for (int len : m.lengths)
		{
			// 音価を2倍 = L値を半分にする（L8→L4は値が小さくなる）
			const int newLen = std::max(1, len / 2);
			result.lengths.push_back(newLen);
		}
		return result;
	}

	/// @brief モチーフを縮小する（音価を半分に）
	/// @param m 元のモチーフ
	/// @return 縮小されたモチーフ（L4→L8, L8→L16 等）
	[[nodiscard]] static Motif diminish(const Motif& m)
	{
		Motif result;
		result.degrees = m.degrees;
		result.rests = m.rests;
		result.lengths.reserve(m.lengths.size());
		for (int len : m.lengths)
		{
			// 音価を半分 = L値を倍にする（L4→L8は値が大きくなる）
			const int newLen = std::min(64, len * 2);
			result.lengths.push_back(newLen);
		}
		return result;
	}

	/// @brief モチーフの末尾を変化させる（問い→答え変奏）
	/// @param m 元のモチーフ
	/// @param newEndDegree 新しい末尾の度数
	/// @return 末尾が変更されたモチーフ
	/// @details 最後の非休符ノートの度数を置換する。フレーズの「答え」を作るのに有用。
	[[nodiscard]] static Motif varyEnding(const Motif& m, int newEndDegree)
	{
		Motif result;
		result.degrees = m.degrees;
		result.lengths = m.lengths;
		result.rests = m.rests;

		// 末尾から走査して最初の非休符ノートを見つける
		for (int i = static_cast<int>(result.degrees.size()) - 1; i >= 0; --i)
		{
			const auto idx = static_cast<std::size_t>(i);
			if (idx < result.rests.size() && result.rests[idx])
			{
				continue;
			}
			result.degrees[idx] = newEndDegree;
			break;
		}
		return result;
	}

	/// @brief モチーフをMML文字列に変換する
	/// @param m モチーフ
	/// @param scale 使用するスケール
	/// @param octave 基準オクターブ（デフォルト5）
	/// @return MML文字列
	[[nodiscard]] static std::string toMml(
		const Motif& m, const Scale& scale, int octave = 5)
	{
		static const char* NOTE_NAMES[] = {
			"C", "C+", "D", "D+", "E", "F",
			"F+", "G", "G+", "A", "A+", "B"
		};

		const auto ivs = scale.intervals();
		const int scaleSize = static_cast<int>(ivs.size());
		const int rootSemitone = static_cast<int>(scale.root());

		std::string result;
		result += "O" + std::to_string(octave) + " ";

		int currentOctave = octave;

		for (std::size_t i = 0; i < m.degrees.size(); ++i)
		{
			const int len = (i < m.lengths.size()) ? m.lengths[i] : 4;

			// 休符判定
			if (i < m.rests.size() && m.rests[i])
			{
				result += "R" + std::to_string(len) + " ";
				continue;
			}

			// 度数からMIDIノート番号を計算する
			const int degree = m.degrees[i];
			int idx = ((degree - 1) % scaleSize);
			if (idx < 0)
			{
				idx += scaleSize;
			}
			int octShift = 0;
			if (degree > 0)
			{
				octShift = (degree - 1) / scaleSize;
			}
			else
			{
				octShift = (degree - scaleSize) / scaleSize;
			}

			const int semitone = (rootSemitone + ivs[static_cast<std::size_t>(idx)]) % 12;
			const int noteOctave = octave + octShift;

			// オクターブ変更をMMLに出力する
			if (noteOctave != currentOctave)
			{
				if (noteOctave > currentOctave)
				{
					for (int o = 0; o < noteOctave - currentOctave; ++o)
					{
						result += ">";
					}
				}
				else
				{
					for (int o = 0; o < currentOctave - noteOctave; ++o)
					{
						result += "<";
					}
				}
				currentOctave = noteOctave;
			}

			result += NOTE_NAMES[semitone];
			result += std::to_string(len);
			result += " ";
		}

		return result;
	}
};

} // namespace mitiru_mml
