#pragma once
/// @file MmlParser.hpp
/// @brief MML文字列パーサー
/// @details MML文字列を解析してコマンド列に変換する。
///
/// @code
/// auto cmds = mitiru_mml::MmlParser::parse("T120 O4 L8 CDEFGAB>C");
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>
#include <cctype>
#include <string_view>
#include <stdexcept>

namespace mitiru_mml
{

/// @brief MML文字列パーサー
class MmlParser
{
public:
	/// @brief MML文字列をパースしてコマンド列を返す
	/// @param mml MML文字列
	/// @return コマンド列
	[[nodiscard]] static CommandList parse(std::string_view mml)
	{
		CommandList result;
		std::size_t pos = 0;

		while (pos < mml.size())
		{
			const char c = mml[pos];

			// 空白をスキップ
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			{
				++pos;
				continue;
			}

			// コメント: ; から行末まで
			if (c == ';')
			{
				while (pos < mml.size() && mml[pos] != '\n') ++pos;
				continue;
			}

			// テンポ
			if (c == 'T' || c == 't')
			{
				++pos;
				const int val = readNumber(mml, pos, 120);
				result.push_back({CommandType::Tempo, val, 0, false, false});
				continue;
			}

			// オクターブ
			if (c == 'O' || c == 'o')
			{
				++pos;
				const int val = readNumber(mml, pos, 4);
				result.push_back({CommandType::Octave, val, 0, false, false});
				continue;
			}

			// オクターブ上下
			if (c == '>')
			{
				++pos;
				result.push_back({CommandType::OctaveUp, 0, 0, false, false});
				continue;
			}
			if (c == '<')
			{
				++pos;
				result.push_back({CommandType::OctaveDown, 0, 0, false, false});
				continue;
			}

			// デフォルト音長
			if (c == 'L' || c == 'l')
			{
				++pos;
				const int val = readNumber(mml, pos, 4);
				result.push_back({CommandType::Length, val, 0, false, false});
				continue;
			}

			// 音量（V=トラック音量）
			if (c == 'V')
			{
				++pos;
				const int val = readNumber(mml, pos, 12);
				result.push_back({CommandType::Volume, val, 0, false, false});
				continue;
			}

			// ノート毎ベロシティ（v小文字）
			if (c == 'v')
			{
				++pos;
				const int val = readNumber(mml, pos, 12);
				MmlCommand cmd;
				cmd.type = CommandType::Velocity;
				cmd.value = val;
				result.push_back(cmd);
				continue;
			}

			// 波形 / FM合成プリセット
			if (c == '@')
			{
				++pos;
				// @FM0 ~ @FM3: FM合成プリセット
				if (pos + 1 < mml.size()
					&& (mml[pos] == 'F' || mml[pos] == 'f')
					&& (mml[pos + 1] == 'M' || mml[pos + 1] == 'm'))
				{
					pos += 2;
					const int preset = readNumber(mml, pos, 0);
					MmlCommand cmd;
					cmd.type = CommandType::FmWave;
					cmd.value = preset;
					result.push_back(cmd);
				}
				else
				{
					const int val = readNumber(mml, pos, 0);
					result.push_back({CommandType::Waveform, val, 0, false, false});
				}
				continue;
			}

			// クオンタイズ
			if (c == 'Q' || c == 'q')
			{
				++pos;
				const int val = readNumber(mml, pos, 8);
				result.push_back({CommandType::Quantize, val, 0, false, false});
				continue;
			}

			// タイ
			if (c == '&')
			{
				++pos;
				if (!result.empty())
				{
					result.back().tied = true;
				}
				continue;
			}

			// スラー（レガート）: ~ はkey-offスキップを明示
			if (c == '~')
			{
				++pos;
				if (!result.empty())
				{
					result.back().tied = true;
					result.back().slur = true;
				}
				continue;
			}

			// デューティ比: W12, W25, W50, W75
			if (c == 'W' || c == 'w')
			{
				// 次の文字が数字ならデューティ比コマンド
				if (pos + 1 < mml.size()
					&& std::isdigit(static_cast<unsigned char>(mml[pos + 1])))
				{
					++pos;
					const int val = readNumber(mml, pos, 50);
					MmlCommand cmd;
					cmd.type = CommandType::Duty;
					cmd.value = val;
					result.push_back(cmd);
					continue;
				}
				// それ以外は不明文字としてスキップ
				++pos;
				continue;
			}

			// ADSRエンベロープ: EA10, ED20, ES70, ER30
			// 注意: 'E'は音符名でもあるため、EA/ED/ES/ERの2文字を先読みする
			if ((c == 'E' || c == 'e')
				&& pos + 1 < mml.size()
				&& isAdsrSuffix(mml[pos + 1]))
			{
				++pos;
				const char sub = mml[pos];
				++pos;
				const int val = readNumber(mml, pos, 0);
				MmlCommand cmd;
				cmd.type = CommandType::Adsr;
				// value: 0=A, 1=D, 2=S, 3=R
				switch (sub)
				{
				case 'A': case 'a': cmd.value = 0; break;
				case 'D': case 'd': cmd.value = 1; break;
				case 'S': case 's': cmd.value = 2; break;
				case 'R': case 'r': cmd.value = 3; break;
				default: cmd.value = 0; break;
				}
				cmd.extra = val;
				result.push_back(cmd);
				continue;
			}

			// デチューン: H10, H-5
			if (c == 'H' || c == 'h')
			{
				++pos;
				const bool negative = (pos < mml.size() && mml[pos] == '-');
				if (negative) ++pos;
				const int val = readNumber(mml, pos, 0);
				MmlCommand cmd;
				cmd.type = CommandType::Detune;
				cmd.value = negative ? -val : val;
				result.push_back(cmd);
				continue;
			}

			// ビブラート: M5,30 or M5,30,100 (speed Hz, depth cents, delay ms)
			if (c == 'M' || c == 'm')
			{
				++pos;
				const int speed = readNumber(mml, pos, 0);
				int depth = 0;
				int delay = 0;
				if (pos < mml.size() && mml[pos] == ',')
				{
					++pos;
					depth = readNumber(mml, pos, 0);
				}
				if (pos < mml.size() && mml[pos] == ',')
				{
					++pos;
					delay = readNumber(mml, pos, 0);
				}
				MmlCommand cmd;
				cmd.type = CommandType::Vibrato;
				cmd.value = speed;
				cmd.extra = depth;
				cmd.extra2 = delay;
				result.push_back(cmd);
				continue;
			}

			// トレモロ: Y速度,深さ (AM変調)
			if (c == 'Y' || c == 'y')
			{
				++pos;
				const int speed = readNumber(mml, pos, 0);
				int depth = 0;
				if (pos < mml.size() && mml[pos] == ',')
				{
					++pos;
					depth = readNumber(mml, pos, 0);
				}
				MmlCommand cmd;
				cmd.type = CommandType::Tremolo;
				cmd.value = speed;
				cmd.extra = depth;
				result.push_back(cmd);
				continue;
			}

			// ポルタメント: _ (次の音符にスライド)
			if (c == '_')
			{
				++pos;
				result.push_back({CommandType::Portamento, 0, 0, false, false});
				continue;
			}

			// グレースノート: {CD}E
			if (c == '{')
			{
				++pos;
				result.push_back({CommandType::GraceStart, 0, 0, false, false});
				continue;
			}
			if (c == '}')
			{
				++pos;
				result.push_back({CommandType::GraceEnd, 0, 0, false, false});
				continue;
			}

			// クレッシェンド/デクレッシェンド: ( )
			if (c == '(')
			{
				++pos;
				result.push_back({CommandType::CrescStart, 0, 0, false, false});
				continue;
			}
			if (c == ')')
			{
				++pos;
				result.push_back({CommandType::DecrescStart, 0, 0, false, false});
				continue;
			}

			// パン定位: P0=左, P1=中央, P2=右
			if (c == 'P' || c == 'p')
			{
				if (pos + 1 < mml.size()
					&& std::isdigit(static_cast<unsigned char>(mml[pos + 1])))
				{
					++pos;
					const int val = readNumber(mml, pos, 1);
					MmlCommand cmd;
					cmd.type = CommandType::Pan;
					cmd.value = val;
					result.push_back(cmd);
					continue;
				}
			}

			// ループポイント: $ (ループ先頭マーカー)
			if (c == '$')
			{
				++pos;
				result.push_back({CommandType::LoopPoint, 0, 0, false, false});
				continue;
			}

			// SSGハードウェアエンベロープ: SE形状,周期
			if (c == 'S')
			{
				if (pos + 1 < mml.size() && (mml[pos + 1] == 'E' || mml[pos + 1] == 'e'))
				{
					pos += 2;
					const int shape = readNumber(mml, pos, 9);
					int period = 0;
					if (pos < mml.size() && mml[pos] == ',')
					{
						++pos;
						period = readNumber(mml, pos, 0);
					}
					MmlCommand cmd;
					cmd.type = CommandType::SsgEnvelope;
					cmd.value = shape;
					cmd.extra = period;
					result.push_back(cmd);
					continue;
				}
			}

			// ループ開始
			if (c == '[')
			{
				++pos;
				result.push_back({CommandType::LoopStart, 0, 0, false, false});
				continue;
			}

			// ループ終了
			if (c == ']')
			{
				++pos;
				int count = readNumber(mml, pos, 2); // デフォルト2回
				result.push_back({CommandType::LoopEnd, count, 0, false, false});
				continue;
			}

			// 休符
			if (c == 'R' || c == 'r')
			{
				++pos;
				const int len = readOptionalNumber(mml, pos);
				const bool dot = readDot(mml, pos);
				result.push_back({CommandType::Rest, 0, len, dot, false});
				continue;
			}

			// 音符: C D E F G A B
			const int noteOffset = noteCharToOffset(c);
			if (noteOffset >= 0)
			{
				++pos;
				// シャープ/フラット
				int semitoneAdj = 0;
				if (pos < mml.size() && (mml[pos] == '+' || mml[pos] == '#'))
				{
					semitoneAdj = 1;
					++pos;
				}
				else if (pos < mml.size() && mml[pos] == '-')
				{
					semitoneAdj = -1;
					++pos;
				}

				const int len = readOptionalNumber(mml, pos);
				const bool dot = readDot(mml, pos);

				MmlCommand cmd;
				cmd.type = CommandType::Note;
				cmd.value = noteOffset + semitoneAdj;
				cmd.duration = len;
				cmd.dotted = dot;
				// 直前がPortamentoコマンドならフラグを設定する
				if (!result.empty() && result.back().type == CommandType::Portamento)
				{
					result.pop_back();
					cmd.portamento = true;
				}
				result.push_back(cmd);
				continue;
			}

			// 不明文字はスキップ
			++pos;
		}

		return result;
	}

private:
	/// @brief ノート文字を半音オフセット（C=0）に変換する
	/// @return 0-11 or -1 if not a note
	[[nodiscard]] static int noteCharToOffset(char c) noexcept
	{
		switch (c)
		{
		case 'C': case 'c': return 0;
		case 'D': case 'd': return 2;
		case 'E': case 'e': return 4;
		case 'F': case 'f': return 5;
		case 'G': case 'g': return 7;
		case 'A': case 'a': return 9;
		case 'B': case 'b': return 11;
		default: return -1;
		}
	}

	/// @brief 数値を読み取る（数字が無ければdefaultVal）
	static int readNumber(std::string_view s, std::size_t& pos, int defaultVal)
	{
		if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos])))
		{
			return defaultVal;
		}
		int val = 0;
		while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])))
		{
			val = val * 10 + (s[pos] - '0');
			++pos;
		}
		return val;
	}

	/// @brief オプショナルな数値を読み取る（数字が無ければ0=デフォルト使用）
	static int readOptionalNumber(std::string_view s, std::size_t& pos)
	{
		return readNumber(s, pos, 0);
	}

	/// @brief ADSR接尾辞かどうか判定する（EA/ED/ES/ER）
	[[nodiscard]] static bool isAdsrSuffix(char c) noexcept
	{
		return c == 'A' || c == 'a'
			|| c == 'D' || c == 'd'
			|| c == 'S' || c == 's'
			|| c == 'R' || c == 'r';
	}

	/// @brief 付点記号を読み取る
	static bool readDot(std::string_view s, std::size_t& pos) noexcept
	{
		if (pos < s.size() && s[pos] == '.')
		{
			++pos;
			return true;
		}
		return false;
	}
};

} // namespace mitiru_mml
