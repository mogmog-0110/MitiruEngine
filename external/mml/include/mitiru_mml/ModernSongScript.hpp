#pragma once
/// @file ModernSongScript.hpp
/// @brief 現代的な曲を、# で始まる指示の行と MML で書く文書から ModernSong を作る
/// @details 読み取りの前半 (#track の行と、続く MML) は SongText と同じで、ChipSong と同じテキストの形になる。
///          指示の行は MML の外に置くので、MML の文法とは衝突しない。# の行は 1 行で 1 つの指示。
///          # のない行は、直前の #track の MML になる (; で始まる行はコメント)。
///
/// 指示の一覧
///   #tempo 128                           曲のテンポ (MML の T が優先)
///   #swing 0.62 grid=8                   スウィング。表拍が周期に占める割合 (0.5 でなし、0.667 で 3 連符の跳ね)。grid は 4、8、16
///   #limiter ceiling=-1 release=80 lookahead=3    マスターのリミッター。#limiter off で切る
///   #master gain=-1 width=1.1            マスターのゲイン (dB) とステレオ幅
///   #tail 2.0                            最後のイベントのあとに描く秒数
///   #loop 2                              曲を流す回数
///   #dither on                           16bit への変換でディザを掛ける (既定は on)
///   #rate 48000                          サンプルレート
///   #sf2 path/to/file.sf2                SF2 の読み込み
///   #group bass gain=-2                  グループのゲイン (dB)。トラックが書いたグループは自動で作られる
///   #duck target=bass,pad by=kick depth=-9 attack=4 release=180   ダッキング (kick の発音で bass と pad を下げる)
///   #duck target=pad by=drums mode=signal threshold=-30 ratio=8 lowpass=150 release=200   ドラムの音を検出に使うサイドチェーン
///   #track [名前] chip=... voice=... ch=... pan=... vol=... group=... drum=... send=reverb:30,delay:10   トラック (SongText.hpp に書き方の全部)
///   chip は opna、opm、opl、opl2、opl3、sn76489、ay8910、nes、synth、drums、sampler。
///   drums の drum は kick snare hat tom cymbal rim clap openhat kit。rhythm は B S H F Y I のリズム文法。
///   opna は part=ssg で SSG、rhythm でドラム。

#include <mitiru_mml/ModernSong.hpp>
#include <mitiru_mml/SongText.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief 文書から ModernSong を作る
class ModernSongScript
{
public:
	/// @brief トラックの send= の宣言 (ModernSong::TrackSpec::sends にも入り、ミックスに効く)
	struct TrackSend
	{
		std::string track;
		std::string effect;     ///< reverb、delay、chorus
		float percent = 0.0f;   ///< 0〜100
	};

	/// @brief 読み取りの結果
	struct ParseResult
	{
		ModernSong song;
		std::vector<TrackSend> sends;                  ///< send= で書かれた送りの一覧 (song のトラックにも入っている)
		std::vector<std::string> errors;               ///< 読み取れなかった行 (行番号つき)
		std::vector<std::string> unknownDirectives;    ///< この読み取りが知らない # の行 (ほかの読み取りに渡すために残す)
		[[nodiscard]] bool ok() const noexcept
		{
			return errors.empty();
		}
	};

	/// @param text 文書
	/// @param soundFont 先に読み込んだ SF2 (文書の #sf2 が優先)
	[[nodiscard]] static ParseResult parse(std::string_view text, std::shared_ptr<const Sf2File> soundFont = nullptr)
	{
		ParseResult result;
		if (soundFont)
		{
			result.song.setSoundFont(std::move(soundFont));
		}

		SongTextResult read = SongText::parse(text);
		result.errors = std::move(read.errors);
		for (const auto& directive : read.directives)
		{
			handleDirective(result, directive.text, directive.line);
		}
		for (const auto& decl : read.tracks)
		{
			result.song.addTrack(toTrackSpec(decl));
			for (const auto& send : decl.sends)
			{
				result.sends.push_back({decl.name, send.effect, send.percent});
			}
		}
		return result;
	}

private:
	using Args = std::map<std::string, std::string>;

	static std::string where(int lineNo)
	{
		return SongText::prefix(lineNo);
	}

	static std::string trim(const std::string& s)
	{
		std::size_t a = 0;
		std::size_t b = s.size();
		while (a < b && std::isspace(static_cast<unsigned char>(s[a])) != 0) ++a;
		while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])) != 0) --b;
		return s.substr(a, b - a);
	}

	static std::string lower(std::string s)
	{
		for (char& c : s)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return s;
	}

	static std::vector<std::string> splitWords(const std::string& s)
	{
		std::vector<std::string> words;
		std::istringstream in(s);
		std::string w;
		while (in >> w)
		{
			words.push_back(w);
		}
		return words;
	}

	static std::vector<std::string> splitComma(const std::string& s)
	{
		std::vector<std::string> parts;
		std::string cur;
		for (const char c : s)
		{
			if (c == ',')
			{
				if (!cur.empty()) parts.push_back(cur);
				cur.clear();
			}
			else
			{
				cur += c;
			}
		}
		if (!cur.empty()) parts.push_back(cur);
		return parts;
	}

	static bool toFloat(const std::string& s, float& out)
	{
		char* end = nullptr;
		const float v = std::strtof(s.c_str(), &end);
		if (end == s.c_str() || *end != '\0')
		{
			return false;
		}
		out = v;
		return true;
	}

	static bool toInt(const std::string& s, int& out)
	{
		char* end = nullptr;
		const long v = std::strtol(s.c_str(), &end, 10);
		if (end == s.c_str() || *end != '\0')
		{
			return false;
		}
		out = static_cast<int>(v);
		return true;
	}

	/// @brief key=value の語を map にし、= の無い語は positional に入れる
	static Args parseArgs(const std::vector<std::string>& words, std::size_t from, std::vector<std::string>& positional)
	{
		Args args;
		for (std::size_t i = from; i < words.size(); ++i)
		{
			const auto eq = words[i].find('=');
			if (eq == std::string::npos)
			{
				positional.push_back(words[i]);
			}
			else
			{
				args[lower(words[i].substr(0, eq))] = words[i].substr(eq + 1);
			}
		}
		return args;
	}

	/// @brief 数値の引数を読む。無ければ何もしない。読めなければ errors に積む
	static void number(const Args& args, const char* key, float& target, ParseResult& r, int lineNo)
	{
		const auto it = args.find(key);
		if (it == args.end())
		{
			return;
		}
		if (!toFloat(it->second, target))
		{
			r.errors.push_back(where(lineNo) + "'" + key + "' は数で書きます ('" + it->second + "' は読めません)");
		}
	}

	/// @brief SongText の宣言を ModernSong のトラックにする
	static ModernSong::TrackSpec toTrackSpec(const ChipTrackDecl& decl)
	{
		ModernSong::TrackSpec spec;
		spec.name = decl.name;
		spec.group = decl.group;
		spec.rhythm = decl.rhythm;
		spec.swing = decl.swing;
		spec.mml = decl.mml;
		spec.channel = decl.channel;
		spec.pan = decl.pan;
		spec.volume = decl.volume;
		for (const EffectSend& send : decl.sends)
		{
			const std::size_t effect = send.effect == "reverb" ? SendBus::REVERB : send.effect == "delay" ? SendBus::DELAY : SendBus::CHORUS;
			spec.sends[effect] = send.percent / 100.0f;
		}
		switch (decl.kind)
		{
		case ChipKind::Synth:
			spec.chip = ModernSong::ChipKind::Synth;
			spec.voice = decl.voiceGiven ? decl.voice : 0;
			break;
		case ChipKind::Drums:
			spec.chip = ModernSong::ChipKind::Drums;
			spec.drum = decl.drum;
			break;
		case ChipKind::Sampler:
			spec.chip = ModernSong::ChipKind::Sampler;
			spec.voice = decl.voiceGiven ? decl.voice : -1;
			spec.drum = decl.percussion ? 0 : -1;
			break;
		case ChipKind::OpnaFm:
			spec.chip = ModernSong::ChipKind::Opna;
			spec.voice = decl.voiceGiven ? decl.voice : 0;
			break;
		case ChipKind::OpnaSsg:
			spec.chip = ModernSong::ChipKind::Opna;
			spec.ssg = true;
			spec.voice = decl.voiceGiven ? decl.voice : -1;
			break;
		case ChipKind::OpnaRhythm:
			spec.chip = ModernSong::ChipKind::Opna;
			spec.voice = 0;
			break;
		case ChipKind::Opm: spec.chip = ModernSong::ChipKind::Opm; spec.voice = decl.voice; break;
		case ChipKind::Opl: spec.chip = ModernSong::ChipKind::Opl; spec.voice = decl.voice; break;
		case ChipKind::Opl2: spec.chip = ModernSong::ChipKind::Opl2; spec.voice = decl.voice; break;
		case ChipKind::Opl3: spec.chip = ModernSong::ChipKind::Opl3; spec.voice = decl.voice; break;
		case ChipKind::Sn76489: spec.chip = ModernSong::ChipKind::Sn76489; spec.voice = decl.voice; break;
		case ChipKind::Ay8910: spec.chip = ModernSong::ChipKind::Ay8910; spec.voice = decl.voice; break;
		case ChipKind::Nes: spec.chip = ModernSong::ChipKind::Nes; spec.voice = decl.voice; break;
		}
		return spec;
	}

	static void handleDirective(ParseResult& r, const std::string& line, int lineNo)
	{
		const auto words = splitWords(line.substr(1));
		if (words.empty())
		{
			r.errors.push_back(where(lineNo) + "# のあとに指示の名前が要ります");
			return;
		}
		const std::string name = lower(words[0]);
		std::vector<std::string> positional;
		const Args args = parseArgs(words, 1, positional);
		ModernSong::Options& opt = r.song.options();

		const auto firstNumber = [&](float& target)
		{
			if (positional.empty() || !toFloat(positional[0], target))
			{
				r.errors.push_back(where(lineNo) + "#" + name + " には数が要ります");
			}
		};

		if (name == "tempo")
		{
			float v = 0.0f;
			firstNumber(v);
			opt.tempo = static_cast<int>(v);
		}
		else if (name == "swing")
		{
			float v = 0.5f;
			firstNumber(v);
			opt.swingRatio = (v > 1.0f) ? v / 100.0f : v;
			float grid = 8.0f;
			number(args, "grid", grid, r, lineNo);
			opt.swingGrid = static_cast<int>(grid);
		}
		else if (name == "limiter")
		{
			if (!positional.empty() && lower(positional[0]) == "off")
			{
				opt.limiter = false;
				return;
			}
			opt.limiter = true;
			number(args, "ceiling", opt.limit.ceilingDb, r, lineNo);
			number(args, "release", opt.limit.releaseMs, r, lineNo);
			number(args, "lookahead", opt.limit.lookaheadMs, r, lineNo);
		}
		else if (name == "master")
		{
			number(args, "gain", opt.masterGainDb, r, lineNo);
			number(args, "width", opt.stereoWidth, r, lineNo);
		}
		else if (name == "tail")
		{
			float v = 0.0f;
			firstNumber(v);
			opt.tailSeconds = static_cast<double>(v);
		}
		else if (name == "loop")
		{
			float v = 1.0f;
			firstNumber(v);
			opt.loopCount = std::max(1, static_cast<int>(v));
		}
		else if (name == "dither")
		{
			opt.dither = positional.empty() || lower(positional[0]) != "off";
		}
		else if (name == "rate")
		{
			float v = 44100.0f;
			firstNumber(v);
			opt.sampleRate = static_cast<std::uint32_t>(std::max(8000.0f, v));
		}
		else if (name == "sf2")
		{
			const std::string path = trim(line.substr(line.find(words[0]) + words[0].size()));
			auto sf = std::make_shared<Sf2File>();
			if (sf->loadFile(path))
			{
				r.song.setSoundFont(std::move(sf));
			}
			else
			{
				r.errors.push_back(where(lineNo) + "SF2 '" + path + "' を読めません (" + sf->error() + ")");
			}
		}
		else if (name == "group")
		{
			ModernSong::GroupSpec g;
			if (positional.empty())
			{
				r.errors.push_back(where(lineNo) + "#group には名前が要ります");
				return;
			}
			g.name = positional[0];
			number(args, "gain", g.gainDb, r, lineNo);
			r.song.addGroup(g);
		}
		else if (name == "duck")
		{
			handleDuck(r, args, lineNo);
		}
		else
		{
			r.unknownDirectives.push_back(line);
		}
	}

	static void handleDuck(ParseResult& r, const Args& args, int lineNo)
	{
		ModernSong::DuckRule rule;
		const auto target = args.find("target");
		const auto by = args.find("by");
		if (target == args.end() || by == args.end())
		{
			r.errors.push_back(where(lineNo) + "#duck には target= と by= が要ります");
			return;
		}
		rule.targets = splitComma(target->second);
		rule.by = by->second;
		number(args, "depth", rule.duck.depthDb, r, lineNo);
		number(args, "attack", rule.duck.attackMs, r, lineNo);
		number(args, "hold", rule.duck.holdMs, r, lineNo);
		number(args, "release", rule.duck.releaseMs, r, lineNo);
		const auto mode = args.find("mode");
		rule.signal = (mode != args.end() && lower(mode->second) == "signal");
		number(args, "threshold", rule.sidechain.thresholdDb, r, lineNo);
		number(args, "ratio", rule.sidechain.ratio, r, lineNo);
		number(args, "lowpass", rule.sidechain.keyLowPassHz, r, lineNo);
		rule.sidechain.attackMs = rule.duck.attackMs;
		rule.sidechain.releaseMs = rule.duck.releaseMs;
		r.song.addDuck(rule);
	}
};

} // namespace mitiru_mml
