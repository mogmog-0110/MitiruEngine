#pragma once
/// @file MmlExtensions.hpp
/// @brief 現代的な音源のための MML 文法の拡張 (X パラメーターと ' 和音 ')
/// @details パーサー本体にはフック 2 つだけを置き、解釈はこのファイルに集める。
///          パラメーターの名前と id の表 (modern_param) は、シンセ、ドラムシンセ、サンプラーの
///          Driver が同じ表を使う。チップが知らない id は無視される。
///
/// 文法
///   Xcutoff=2000          値を設定する
///   Xcutoff=500>4000/8    500 から 4000 まで 8 分音符の長さで動かす (時間は進まない)
///   Xcutoff=>4000/8       直前の値 (無ければ既定値) から動かす
///   Xwave=saw             名前で選べる項目は名前でも書ける
///   'CEG'4                和音。音は上へ向かって並べ、1 つの音符として扱う

#include <mitiru_mml/MmlTypes.hpp>

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

namespace mitiru_mml
{

/// @brief ChipDriver::setParam の id (複数のチップで共通。1000 番台)
namespace modern_param
{
inline constexpr int CUTOFF = 1000;   ///< フィルターのカットオフ (Hz)
inline constexpr int RESONANCE = 1001; ///< レゾナンス 0〜100
inline constexpr int FENV = 1002;     ///< フィルターエンベロープの量 (半音)
inline constexpr int FKEY = 1003;     ///< キートラッキング (%)
inline constexpr int FTYPE = 1004;    ///< 0 = LP、1 = HP、2 = BP
inline constexpr int DETUNE = 1005;   ///< 2 つ目のオシレーターの離調 (セント)
inline constexpr int UDETUNE = 1006;  ///< ユニゾンの離調の幅 (セント)
inline constexpr int SPREAD = 1007;   ///< ユニゾンの左右の広がり (%)
inline constexpr int UNISON = 1008;   ///< ユニゾンの本数 1〜7
inline constexpr int PW = 1009;       ///< 矩形波のパルス幅 (%)
inline constexpr int WAVE = 1010;     ///< オシレーター 1 の波形
inline constexpr int WAVE2 = 1011;    ///< オシレーター 2 の波形
inline constexpr int MIX2 = 1012;     ///< オシレーター 2 の量 (%)
inline constexpr int SUB = 1013;      ///< サブオシレーター (1 オクターブ下の正弦波) の量 (%)
inline constexpr int NOISE = 1014;    ///< ノイズの量 (%)
inline constexpr int TUNE2 = 1015;    ///< オシレーター 2 の移調 (半音)
inline constexpr int ATK = 1016;      ///< アンプのアタック (ミリ秒)
inline constexpr int DEC = 1017;      ///< アンプのディケイ (ミリ秒)
inline constexpr int SUS = 1018;      ///< アンプのサステイン (%)
inline constexpr int REL = 1019;      ///< アンプのリリース (ミリ秒)
inline constexpr int FATK = 1020;     ///< フィルターエンベロープのアタック (ミリ秒)
inline constexpr int FDEC = 1021;     ///< フィルターエンベロープのディケイ (ミリ秒)
inline constexpr int FSUS = 1022;     ///< フィルターエンベロープのサステイン (%)
inline constexpr int FREL = 1023;     ///< フィルターエンベロープのリリース (ミリ秒)
inline constexpr int LFOR = 1024;     ///< LFO の速さ (0.1 Hz 単位)
inline constexpr int LFOWAVE = 1025;  ///< LFO の波形
inline constexpr int LFOP = 1026;     ///< LFO のピッチへの深さ (セント)
inline constexpr int LFOF = 1027;     ///< LFO のカットオフへの深さ (半音)
inline constexpr int LFOA = 1028;     ///< LFO の音量への深さ (%)
inline constexpr int LFOPW = 1029;    ///< LFO のパルス幅への深さ (%)
inline constexpr int LFODELAY = 1030; ///< LFO が効き始めるまでの時間 (ミリ秒)
inline constexpr int GAIN = 1031;     ///< 音色の音量 (%)
inline constexpr int VOL = 1032;      ///< チャンネルの音量 0〜127 (V より細かい)
inline constexpr int PAN = 1033;      ///< 定位 0〜127 (64 が中央)
inline constexpr int TUNE = 1034;     ///< 全体の音程 (セント)
inline constexpr int TONE = 1035;     ///< ドラムの音色 (明るさ、%)
inline constexpr int SNAP = 1036;     ///< ドラムのアタックの鋭さ (%)
inline constexpr int ARP = 1040;      ///< アルペジエーターの向き (0 = 切、1 = 上、2 = 下、3 = 上下、4 = 乱数)
inline constexpr int ARPRATE = 1041;  ///< アルペジエーターの 1 音の長さ (音長の値)
inline constexpr int ARPOCT = 1042;   ///< アルペジエーターが使うオクターブ数
inline constexpr int ARPGATE = 1043;  ///< アルペジエーターの 1 音の長さに対する鳴らす割合 (%)

/// @brief 範囲の外の値のマーカー。「直前の値から動かす」を表す
inline constexpr int FROM_CURRENT = std::numeric_limits<int>::min();
} // namespace modern_param

/// @brief パラメーター 1 つの定義
struct ModernParamInfo
{
	const char* name;    ///< MML での名前 (小文字)
	int id;              ///< setParam の id
	int defaultValue;    ///< 既定値 (ランプの起点が分からないときに使う)
	int minValue;
	int maxValue;
	bool logScale;       ///< ランプを対数で補間する (カットオフなど)
	const char* choices; ///< 名前で値を書ける項目の、コンマ区切りの名前 (先頭が 0)。無ければ nullptr
};

/// @brief パラメーターの表
[[nodiscard]] inline const std::array<ModernParamInfo, 38>& modernParamTable() noexcept
{
	using namespace modern_param;
	static const std::array<ModernParamInfo, 38> TABLE = {{
		{"cutoff", CUTOFF, 2000, 20, 20000, true, nullptr},
		{"res", RESONANCE, 10, 0, 100, false, nullptr},
		{"fenv", FENV, 0, -96, 96, false, nullptr},
		{"fkey", FKEY, 50, 0, 100, false, nullptr},
		{"ftype", FTYPE, 0, 0, 2, false, "lp,hp,bp"},
		{"detune", DETUNE, 0, -100, 100, false, nullptr},
		{"udetune", UDETUNE, 20, 0, 100, false, nullptr},
		{"spread", SPREAD, 80, 0, 100, false, nullptr},
		{"unison", UNISON, 1, 1, 7, false, nullptr},
		{"pw", PW, 50, 1, 99, false, nullptr},
		{"wave", WAVE, 0, 0, 4, false, "saw,square,tri,sine,noise"},
		{"wave2", WAVE2, 0, 0, 4, false, "saw,square,tri,sine,noise"},
		{"mix2", MIX2, 0, 0, 100, false, nullptr},
		{"sub", SUB, 0, 0, 100, false, nullptr},
		{"noise", NOISE, 0, 0, 100, false, nullptr},
		{"tune2", TUNE2, 0, -24, 24, false, nullptr},
		{"atk", ATK, 5, 0, 10000, false, nullptr},
		{"dec", DEC, 200, 0, 10000, false, nullptr},
		{"sus", SUS, 70, 0, 100, false, nullptr},
		{"rel", REL, 100, 0, 10000, false, nullptr},
		{"fatk", FATK, 0, 0, 10000, false, nullptr},
		{"fdec", FDEC, 200, 0, 10000, false, nullptr},
		{"fsus", FSUS, 0, 0, 100, false, nullptr},
		{"frel", FREL, 100, 0, 10000, false, nullptr},
		{"lfor", LFOR, 50, 0, 300, false, nullptr},
		{"lfowave", LFOWAVE, 0, 0, 4, false, "sine,tri,saw,square,sh"},
		{"lfop", LFOP, 0, 0, 1200, false, nullptr},
		{"lfof", LFOF, 0, 0, 96, false, nullptr},
		{"lfoa", LFOA, 0, 0, 100, false, nullptr},
		{"lfopw", LFOPW, 0, 0, 49, false, nullptr},
		{"lfodelay", LFODELAY, 0, 0, 5000, false, nullptr},
		{"gain", GAIN, 100, 0, 200, false, nullptr},
		{"vol", VOL, 127, 0, 127, false, nullptr},
		{"pan", PAN, 64, 0, 127, false, nullptr},
		{"tune", TUNE, 0, -2400, 2400, false, nullptr},
		{"tone", TONE, 50, 0, 100, false, nullptr},
		{"snap", SNAP, 50, 0, 100, false, nullptr},
		{"arp", ARP, 0, 0, 4, false, "off,up,down,updown,random"},
	}};
	return TABLE;
}

/// @brief アルペジエーターの追加分 (表の要素数を増やさないよう別に持つ)
[[nodiscard]] inline const std::array<ModernParamInfo, 3>& modernArpParamTable() noexcept
{
	using namespace modern_param;
	static const std::array<ModernParamInfo, 3> TABLE = {{
		{"arprate", ARPRATE, 16, 1, 64, false, nullptr},
		{"arpoct", ARPOCT, 1, 1, 4, false, nullptr},
		{"arpgate", ARPGATE, 70, 5, 100, false, nullptr},
	}};
	return TABLE;
}

/// @brief 名前からパラメーターを探す (無ければ nullptr)
[[nodiscard]] inline const ModernParamInfo* findModernParam(std::string_view name) noexcept
{
	for (const auto& p : modernParamTable())
	{
		if (name == p.name) return &p;
	}
	for (const auto& p : modernArpParamTable())
	{
		if (name == p.name) return &p;
	}
	return nullptr;
}

/// @brief id からパラメーターを探す (無ければ nullptr)
[[nodiscard]] inline const ModernParamInfo* findModernParam(int id) noexcept
{
	for (const auto& p : modernParamTable())
	{
		if (id == p.id) return &p;
	}
	for (const auto& p : modernArpParamTable())
	{
		if (id == p.id) return &p;
	}
	return nullptr;
}

/// @brief 拡張文法の読み取り (MmlParser から呼ばれる)
class MmlExtensionSyntax
{
public:
	/// @brief X コマンドを読む。pos は X の位置。必ず X のコマンド全体を読み進める
	/// @return コマンドが作れたら true。名前が不明、値が読めないときは false (警告して捨てる)
	/// @details 結果は type = Param、value = id、extra = 目標の値、extra2 = 起点の値 (FROM_CURRENT なら直前の値)、
	///          duration と dotted = ランプの音長 (duration が 0 なら即座に設定)
	[[nodiscard]] static bool parseParam(std::string_view s, std::size_t& pos, MmlCommand& out)
	{
		++pos; // X
		const std::size_t nameStart = pos;
		while (pos < s.size() && (std::isalnum(static_cast<unsigned char>(s[pos])) != 0))
		{
			++pos;
		}
		std::string name(s.substr(nameStart, pos - nameStart));
		for (char& ch : name)
		{
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}

		const ModernParamInfo* info = findModernParam(name);
		const bool hasEquals = (pos < s.size() && s[pos] == '=');
		if (hasEquals)
		{
			++pos;
		}
		int from = modern_param::FROM_CURRENT;
		int to = 0;
		int lenValue = 0;
		bool dotted = false;
		const bool ok = hasEquals && info != nullptr && readValue(s, pos, *info, from, to, lenValue, dotted);
		if (!ok)
		{
			// 値の残りを音符として読み直させないよう、値に使う文字を読み捨てる
			while (pos < s.size() && (std::isalnum(static_cast<unsigned char>(s[pos])) != 0
				|| s[pos] == '-' || s[pos] == '+' || s[pos] == '.' || s[pos] == '/' || s[pos] == '>'))
			{
				++pos;
			}
			warn(name);
			return false;
		}
		out = MmlCommand{};
		out.type = CommandType::Param;
		out.value = info->id;
		out.extra = to;
		out.extra2 = from;
		out.duration = lenValue;
		out.dotted = dotted;
		return true;
	}

	/// @brief 和音 'CEG' の音を読む。pos は開きの ' の位置。閉じの ' の次まで読み進める
	/// @param[out] rootOffset 最初の音の半音位置 (C = 0。シャープとフラットを含む)
	/// @param[out] toneMask 最初の音より何半音上かを、ビットの位置で表した集合 (1〜30)
	/// @return 音が 1 つ以上あれば true
	[[nodiscard]] static bool parseChord(std::string_view s, std::size_t& pos, int& rootOffset, int& toneMask)
	{
		++pos; // 開きの '
		bool haveRoot = false;
		int rootAbs = 0;
		int prevAbs = 0;
		int octaveShift = 0;
		unsigned mask = 0;
		while (pos < s.size() && s[pos] != '\'')
		{
			const char c = s[pos];
			++pos;
			if (c == '>')
			{
				octaveShift += 12;
				continue;
			}
			if (c == '<')
			{
				octaveShift -= 12;
				continue;
			}
			const int base = noteOffset(c);
			if (base < 0)
			{
				continue;
			}
			int n = base;
			if (pos < s.size() && (s[pos] == '+' || s[pos] == '#'))
			{
				++n;
				++pos;
			}
			else if (pos < s.size() && s[pos] == '-')
			{
				--n;
				++pos;
			}
			if (!haveRoot)
			{
				haveRoot = true;
				rootAbs = n;
				rootOffset = n;
				octaveShift = 0;
				continue;
			}
			// オクターブ記号を含めた位置。直前の音以下になるときは、近い上の音へ上げる ('CEG' は上へ並ぶ)
			int abs = n + octaveShift - rootAbs;
			while (abs <= prevAbs)
			{
				abs += 12;
			}
			if (abs <= 30)
			{
				mask |= (1u << abs);
				prevAbs = abs;
			}
		}
		if (pos < s.size())
		{
			++pos; // 閉じの '
		}
		toneMask = static_cast<int>(mask);
		return haveRoot;
	}

private:
	static int noteOffset(char c) noexcept
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

	static void warn(const std::string& name)
	{
		if (std::getenv("MITIRU_MML_QUIET") == nullptr)
		{
			std::fprintf(stderr, "mitiru_mml: parameter command 'X%s' is not understood and was skipped\n", name.c_str());
		}
	}

	static bool readInt(std::string_view s, std::size_t& pos, int& value)
	{
		std::size_t p = pos;
		bool negative = false;
		if (p < s.size() && (s[p] == '-' || s[p] == '+'))
		{
			negative = (s[p] == '-');
			++p;
		}
		if (p >= s.size() || std::isdigit(static_cast<unsigned char>(s[p])) == 0)
		{
			return false;
		}
		long long v = 0;
		while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p])) != 0)
		{
			v = v * 10 + (s[p] - '0');
			if (v > 1000000000LL) v = 1000000000LL;
			++p;
		}
		value = static_cast<int>(negative ? -v : v);
		pos = p;
		return true;
	}

	static bool readChoice(std::string_view s, std::size_t& pos, const char* choices, int& value)
	{
		if (choices == nullptr)
		{
			return false;
		}
		std::size_t p = pos;
		while (p < s.size() && (std::isalnum(static_cast<unsigned char>(s[p])) != 0))
		{
			++p;
		}
		std::string word(s.substr(pos, p - pos));
		for (char& ch : word)
		{
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		if (word.empty())
		{
			return false;
		}
		int index = 0;
		std::string_view rest(choices);
		while (!rest.empty())
		{
			const std::size_t comma = rest.find(',');
			const std::string_view item = rest.substr(0, comma);
			if (item == word)
			{
				value = index;
				pos = p;
				return true;
			}
			if (comma == std::string_view::npos) break;
			rest.remove_prefix(comma + 1);
			++index;
		}
		return false;
	}

	static bool readOperand(std::string_view s, std::size_t& pos, const ModernParamInfo& info, int& value)
	{
		return readInt(s, pos, value) || readChoice(s, pos, info.choices, value);
	}

	static bool readValue(std::string_view s, std::size_t& pos, const ModernParamInfo& info,
		int& from, int& to, int& lenValue, bool& dotted)
	{
		int first = 0;
		const bool hasFirst = readOperand(s, pos, info, first);
		bool isRamp = false;
		if (pos < s.size() && s[pos] == '>')
		{
			// '>' の次が値のときだけランプ。そうでなければオクターブ上げとして残す
			std::size_t p = pos + 1;
			int second = 0;
			if (readOperand(s, p, info, second))
			{
				pos = p;
				from = hasFirst ? first : modern_param::FROM_CURRENT;
				to = second;
				isRamp = true;
			}
		}
		if (!isRamp)
		{
			if (!hasFirst)
			{
				return false;
			}
			to = first;
			from = modern_param::FROM_CURRENT;
		}
		if (pos < s.size() && s[pos] == '/')
		{
			++pos;
			int len = 0;
			if (!readInt(s, pos, len) || len <= 0)
			{
				return false;
			}
			lenValue = len;
			if (pos < s.size() && s[pos] == '.')
			{
				dotted = true;
				++pos;
			}
		}
		else if (isRamp)
		{
			lenValue = 1; // 長さを書かないランプは全音符
		}
		return true;
	}
};

} // namespace mitiru_mml
