#pragma once

/// @file DynamicBGM.hpp
/// @brief ストーリーの感情に連動するダイナミックBGMシステム
/// @details シナリオの感情変化に応じてBGMをクロスフェード・レイヤーブレンドする。
///          キーワードベースの自動ムード検出、日本語感情語辞書、スクリプトコマンド連携を提供。
///
/// @code
/// mitiru::vn::DynamicBGMController bgm;
/// bgm.registerTrack(mitiru::vn::MoodType::Calm, "bgm_calm");
/// bgm.registerTrack(mitiru::vn::MoodType::Tense, "bgm_tense");
/// bgm.registerLayer(mitiru::vn::MoodType::Tense, "layer_drums", 0.8f);
///
/// bgm.setMood(mitiru::vn::MoodType::Calm, 2.0f);
/// // シナリオ進行中...
/// bgm.setMood(mitiru::vn::MoodType::Tense, 1.5f);
///
/// // 毎フレーム
/// bgm.update(dt);
/// auto state = bgm.currentState();
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  ムード定義
// ════════════════════════════════════════════════════════════════════

/// @brief BGMの感情種別
enum class MoodType : std::uint8_t
{
	Calm,        ///< 穏やか / 日常
	Happy,       ///< 嬉しい / 楽しい
	Sad,         ///< 悲しい / 切ない
	Tense,       ///< 緊張 / 不安
	Romantic,    ///< ロマンチック / 甘い
	Mysterious,  ///< 神秘的 / 不思議
	Action,      ///< アクション / 興奮
	Comedy,      ///< コメディ / おふざけ
};

/// @brief MoodType用ハッシュ関数
struct MoodTypeHash
{
	std::size_t operator()(MoodType m) const noexcept
	{
		return std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(m));
	}
};

/// @brief トランジション方式
enum class BGMTransitionType : std::uint8_t
{
	Crossfade,     ///< クロスフェード（同時に再生しながら切り替え）
	DuckAndSwap,   ///< 現在の曲を一旦下げてから切り替え
	LayerBlend,    ///< レイヤーベースのブレンド
};

// ════════════════════════════════════════════════════════════════════
//  トラック・レイヤー定義
// ════════════════════════════════════════════════════════════════════

/// @brief BGMトラック登録情報
struct BGMTrack
{
	MoodType mood = MoodType::Calm;
	std::string audioId;                 ///< オーディオリソースID
	float baseVolume = 1.0f;             ///< 基本音量（0.0〜1.0）
};

/// @brief BGMレイヤー登録情報
struct BGMLayer
{
	MoodType mood = MoodType::Calm;
	std::string audioId;                 ///< オーディオリソースID
	float volumeCurve = 1.0f;            ///< 音量カーブ係数（ムード強度に対する応答）
	float maxVolume = 1.0f;              ///< 最大音量
};

// ════════════════════════════════════════════════════════════════════
//  再生状態
// ════════════════════════════════════════════════════════════════════

/// @brief トラック再生状態
struct TrackPlayState
{
	std::string audioId;                 ///< 再生中のオーディオID
	float volume = 0.0f;                 ///< 現在の音量
	float targetVolume = 0.0f;           ///< 目標音量
	bool active = false;                 ///< 再生中か
};

/// @brief BGMコントローラーの全体状態（レンダリング/オーディオ層への出力）
struct BGMState
{
	MoodType currentMood = MoodType::Calm;
	MoodType previousMood = MoodType::Calm;
	float transitionProgress = 1.0f;     ///< トランジション進行度（0.0〜1.0、1.0で完了）
	std::vector<TrackPlayState> tracks;  ///< 全トラックの再生状態
	bool transitioning = false;          ///< トランジション中か
};

// ════════════════════════════════════════════════════════════════════
//  MoodDetector
// ════════════════════════════════════════════════════════════════════

/// @brief ムード検出結果
struct MoodDetection
{
	MoodType mood = MoodType::Calm;
	float confidence = 0.0f;             ///< 信頼度（0.0〜1.0）
};

/// @brief テキストからの感情キーワードベースムード検出
/// @details 日本語・英語の感情キーワード辞書を内蔵し、
///          対話テキストから最も適切なムードを推定する。
class MoodDetector
{
public:
	/// @brief コンストラクタ（デフォルト辞書を初期化）
	MoodDetector()
	{
		initDefaultKeywords();
	}

	/// @brief キーワード → ムードのマッピングを追加する
	/// @param keyword キーワード
	/// @param mood 対応するムード
	/// @param weight キーワードの重み（デフォルト1.0）
	void addKeyword(const std::string& keyword, MoodType mood, float weight = 1.0f)
	{
		m_keywords[keyword] = {mood, weight};
	}

	/// @brief キーワードを削除する
	void removeKeyword(const std::string& keyword)
	{
		m_keywords.erase(keyword);
	}

	/// @brief テキストからムードを検出する
	/// @param text 対話テキスト
	/// @return 最も信頼度の高いムード検出結果
	[[nodiscard]] MoodDetection detect(std::string_view text) const
	{
		// ムードごとのスコアを集計
		std::unordered_map<std::uint8_t, float> scores;

		for (const auto& [keyword, entry] : m_keywords)
		{
			if (containsKeyword(text, keyword))
			{
				auto moodKey = static_cast<std::uint8_t>(entry.mood);
				scores[moodKey] += entry.weight;
			}
		}

		if (scores.empty())
		{
			return {MoodType::Calm, 0.0f};
		}

		// 最高スコアのムードを選択
		std::uint8_t bestMood = 0;
		float bestScore = 0.0f;
		float totalScore = 0.0f;

		for (const auto& [mood, score] : scores)
		{
			totalScore += score;
			if (score > bestScore)
			{
				bestScore = score;
				bestMood = mood;
			}
		}

		float confidence = (totalScore > 0.0f) ? (bestScore / totalScore) : 0.0f;
		confidence = std::min(confidence, 1.0f);

		return {static_cast<MoodType>(bestMood), confidence};
	}

	/// @brief 全キーワードをクリアする
	void clearKeywords() { m_keywords.clear(); }

	/// @brief デフォルト辞書を再初期化する
	void resetToDefaults()
	{
		m_keywords.clear();
		initDefaultKeywords();
	}

private:
	struct KeywordEntry
	{
		MoodType mood;
		float weight;
	};

	/// @brief テキスト中にキーワードが含まれるか（部分一致）
	[[nodiscard]] static bool containsKeyword(std::string_view text, const std::string& keyword)
	{
		if (keyword.size() > text.size()) return false;
		return text.find(keyword) != std::string_view::npos;
	}

	/// @brief デフォルトの感情キーワード辞書を初期化する
	void initDefaultKeywords()
	{
		// ── 日本語キーワード ──

		// Happy
		addKeyword("\xe5\xac\x89\xe3\x81\x97", MoodType::Happy, 1.0f);         // 嬉し
		addKeyword("\xe6\xa5\xbd\xe3\x81\x97", MoodType::Happy, 1.0f);         // 楽し
		addKeyword("\xe3\x82\x84\xe3\x81\xa3\xe3\x81\x9f", MoodType::Happy, 0.8f); // やった
		addKeyword("\xe6\x9c\x80\xe9\xab\x98", MoodType::Happy, 0.8f);         // 最高
		addKeyword("\xe5\xb9\xb8\xe3\x81\x9b", MoodType::Happy, 1.0f);         // 幸せ
		addKeyword("\xe3\x82\x8f\xe3\x83\xbc\xe3\x81\x84", MoodType::Happy, 0.6f); // わーい

		// Sad
		addKeyword("\xe6\x82\xb2\xe3\x81\x97", MoodType::Sad, 1.0f);           // 悲し
		addKeyword("\xe5\x88\x87\xe3\x81\xaa", MoodType::Sad, 0.9f);           // 切な
		addKeyword("\xe6\xb3\xa3", MoodType::Sad, 0.8f);                       // 泣
		addKeyword("\xe6\xb6\x99", MoodType::Sad, 0.8f);                       // 涙
		addKeyword("\xe5\xaf\x82\xe3\x81\x97", MoodType::Sad, 0.9f);           // 寂し
		addKeyword("\xe3\x81\x94\xe3\x82\x81\xe3\x82\x93", MoodType::Sad, 0.7f); // ごめん
		addKeyword("\xe3\x81\x95\xe3\x82\x88\xe3\x81\xaa\xe3\x82\x89", MoodType::Sad, 0.9f); // さよなら

		// Tense
		addKeyword("\xe6\x80\x96", MoodType::Tense, 1.0f);                     // 怖
		addKeyword("\xe5\x8d\xb1\xe9\x99\xba", MoodType::Tense, 1.0f);         // 危険
		addKeyword("\xe9\x80\x83\xe3\x81\x92", MoodType::Tense, 0.9f);         // 逃げ
		addKeyword("\xe6\xae\xba", MoodType::Tense, 1.0f);                     // 殺
		addKeyword("\xe6\xad\xbb", MoodType::Tense, 0.9f);                     // 死
		addKeyword("\xe8\xa1\x80", MoodType::Tense, 0.8f);                     // 血
		addKeyword("\xe4\xb8\x8d\xe5\xae\x89", MoodType::Tense, 0.8f);         // 不安
		addKeyword("\xe7\xb7\x8a\xe5\xbc\xb5", MoodType::Tense, 0.7f);         // 緊張

		// Romantic
		addKeyword("\xe5\xa5\xbd\xe3\x81\x8d", MoodType::Romantic, 1.0f);       // 好き
		addKeyword("\xe6\x84\x9b\xe3\x81\x97\xe3\x81\xa6", MoodType::Romantic, 1.0f); // 愛して
		addKeyword("\xe3\x82\xad\xe3\x82\xb9", MoodType::Romantic, 0.9f);       // キス
		addKeyword("\xe3\x83\x89\xe3\x82\xad\xe3\x83\x89\xe3\x82\xad", MoodType::Romantic, 0.8f); // ドキドキ
		addKeyword("\xe6\x81\x8b", MoodType::Romantic, 0.9f);                   // 恋
		addKeyword("\xe6\x8a\xb1\xe3\x81\x8d\xe3\x81\x97\xe3\x82\x81", MoodType::Romantic, 0.9f); // 抱きしめ

		// Mysterious
		addKeyword("\xe4\xb8\x8d\xe6\x80\x9d\xe8\xad\xb0", MoodType::Mysterious, 1.0f); // 不思議
		addKeyword("\xe8\xac\x8e", MoodType::Mysterious, 1.0f);                 // 謎
		addKeyword("\xe7\xa7\x98\xe5\xaf\x86", MoodType::Mysterious, 0.9f);     // 秘密
		addKeyword("\xe5\xa5\x87\xe5\xa6\x99", MoodType::Mysterious, 0.8f);     // 奇妙
		addKeyword("\xe9\xad\x94\xe6\xb3\x95", MoodType::Mysterious, 0.8f);     // 魔法

		// Action
		addKeyword("\xe6\x88\xa6", MoodType::Action, 0.9f);                     // 戦
		addKeyword("\xe6\x94\xbb\xe6\x92\x83", MoodType::Action, 1.0f);         // 攻撃
		addKeyword("\xe5\x89\xa3", MoodType::Action, 0.7f);                     // 剣
		addKeyword("\xe7\x88\x86\xe7\x99\xba", MoodType::Action, 1.0f);         // 爆発
		addKeyword("\xe8\xb5\xb0\xe3\x82\x8c", MoodType::Action, 0.8f);         // 走れ

		// Comedy
		addKeyword("\xe3\x82\xa2\xe3\x83\x8f\xe3\x83\x8f", MoodType::Comedy, 0.8f); // アハハ
		addKeyword("\xe3\x83\x90\xe3\x82\xab", MoodType::Comedy, 0.7f);         // バカ
		addKeyword("\xe7\xac\x91", MoodType::Comedy, 0.6f);                     // 笑
		addKeyword("\xe3\x81\xb5\xe3\x81\x96\xe3\x81\x91", MoodType::Comedy, 0.7f); // ふざけ
		addKeyword("\xe3\x82\xb3\xe3\x83\xa9", MoodType::Comedy, 0.6f);         // コラ

		// Calm
		addKeyword("\xe5\xb9\xb3\xe5\x92\x8c", MoodType::Calm, 0.8f);           // 平和
		addKeyword("\xe3\x81\xae\xe3\x82\x93\xe3\x81\xb3\xe3\x82\x8a", MoodType::Calm, 0.8f); // のんびり
		addKeyword("\xe9\x9d\x99\xe3\x81\x8b", MoodType::Calm, 0.7f);           // 静か

		// ── 英語キーワード ──

		addKeyword("happy", MoodType::Happy, 0.9f);
		addKeyword("glad", MoodType::Happy, 0.8f);
		addKeyword("wonderful", MoodType::Happy, 0.8f);
		addKeyword("great", MoodType::Happy, 0.6f);

		addKeyword("sad", MoodType::Sad, 0.9f);
		addKeyword("sorry", MoodType::Sad, 0.7f);
		addKeyword("cry", MoodType::Sad, 0.8f);
		addKeyword("tears", MoodType::Sad, 0.8f);
		addKeyword("goodbye", MoodType::Sad, 0.8f);
		addKeyword("farewell", MoodType::Sad, 0.9f);

		addKeyword("scared", MoodType::Tense, 0.9f);
		addKeyword("danger", MoodType::Tense, 1.0f);
		addKeyword("run", MoodType::Tense, 0.6f);
		addKeyword("kill", MoodType::Tense, 1.0f);
		addKeyword("blood", MoodType::Tense, 0.8f);

		addKeyword("love", MoodType::Romantic, 1.0f);
		addKeyword("kiss", MoodType::Romantic, 0.9f);
		addKeyword("heart", MoodType::Romantic, 0.7f);
		addKeyword("darling", MoodType::Romantic, 0.9f);

		addKeyword("mystery", MoodType::Mysterious, 1.0f);
		addKeyword("strange", MoodType::Mysterious, 0.8f);
		addKeyword("secret", MoodType::Mysterious, 0.9f);
		addKeyword("magic", MoodType::Mysterious, 0.8f);

		addKeyword("fight", MoodType::Action, 0.9f);
		addKeyword("attack", MoodType::Action, 1.0f);
		addKeyword("sword", MoodType::Action, 0.7f);
		addKeyword("explosion", MoodType::Action, 1.0f);

		addKeyword("haha", MoodType::Comedy, 0.8f);
		addKeyword("funny", MoodType::Comedy, 0.8f);
		addKeyword("laugh", MoodType::Comedy, 0.7f);
		addKeyword("joke", MoodType::Comedy, 0.8f);

		addKeyword("peaceful", MoodType::Calm, 0.8f);
		addKeyword("quiet", MoodType::Calm, 0.7f);
		addKeyword("gentle", MoodType::Calm, 0.6f);
	}

	std::unordered_map<std::string, KeywordEntry> m_keywords;
};

// ════════════════════════════════════════════════════════════════════
//  DynamicBGMController
// ════════════════════════════════════════════════════════════════════

/// @brief ダイナミックBGMコントローラー
/// @details ムードに紐付いたBGMトラック・レイヤーを管理し、
///          ムード変更時にスムーズなトランジションを行う。
class DynamicBGMController
{
public:
	/// @brief オーディオ再生要求コールバック型
	/// @details (audioId, volume, fadeInDuration) を受け取り再生を開始する
	using PlayCallback = std::function<void(const std::string& audioId, float volume, float fadeDuration)>;

	/// @brief オーディオ停止要求コールバック型
	/// @details (audioId, fadeOutDuration) を受け取り停止する
	using StopCallback = std::function<void(const std::string& audioId, float fadeDuration)>;

	/// @brief オーディオ音量変更コールバック型
	using VolumeCallback = std::function<void(const std::string& audioId, float volume)>;

	/// @brief コンストラクタ
	DynamicBGMController() = default;

	// ── トラック/レイヤー登録 ──────────────────────────────────

	/// @brief ムードにBGMトラックを登録する
	/// @param mood ムード種別
	/// @param audioId オーディオリソースID
	/// @param baseVolume 基本音量（0.0〜1.0）
	void registerTrack(MoodType mood, const std::string& audioId, float baseVolume = 1.0f)
	{
		BGMTrack track;
		track.mood = mood;
		track.audioId = audioId;
		track.baseVolume = std::clamp(baseVolume, 0.0f, 1.0f);
		m_tracks[mood] = track;
	}

	/// @brief ムードにレイヤーを登録する（追加楽器等）
	/// @param mood ムード種別
	/// @param audioId オーディオリソースID
	/// @param volumeCurve 音量カーブ係数
	/// @param maxVolume 最大音量
	void registerLayer(MoodType mood, const std::string& audioId,
	                    float volumeCurve = 1.0f, float maxVolume = 1.0f)
	{
		BGMLayer layer;
		layer.mood = mood;
		layer.audioId = audioId;
		layer.volumeCurve = volumeCurve;
		layer.maxVolume = std::clamp(maxVolume, 0.0f, 1.0f);
		m_layers[mood].push_back(layer);
	}

	/// @brief 登録済みトラック/レイヤーをクリアする
	void clearRegistrations()
	{
		m_tracks.clear();
		m_layers.clear();
	}

	// ── コールバック設定 ────────────────────────────────────────

	/// @brief 再生要求コールバックを設定する
	void setPlayCallback(PlayCallback cb) { m_onPlay = std::move(cb); }

	/// @brief 停止要求コールバックを設定する
	void setStopCallback(StopCallback cb) { m_onStop = std::move(cb); }

	/// @brief 音量変更コールバックを設定する
	void setVolumeCallback(VolumeCallback cb) { m_onVolume = std::move(cb); }

	// ── ムード制御 ──────────────────────────────────────────

	/// @brief ムードを変更する
	/// @param mood 新しいムード
	/// @param transitionDuration トランジション時間（秒）
	/// @param transitionType トランジション方式
	void setMood(MoodType mood, float transitionDuration = 1.0f,
	             BGMTransitionType transitionType = BGMTransitionType::Crossfade)
	{
		if (mood == m_currentMood && !m_transitioning) return;

		m_previousMood = m_currentMood;
		m_currentMood = mood;
		m_transitionType = transitionType;
		m_transitionDuration = std::max(0.01f, transitionDuration);
		m_transitionElapsed = 0.0f;
		m_transitioning = true;

		// 新しいトラックの再生を開始
		startMoodTrack(mood, transitionDuration);
	}

	/// @brief 現在のムードを取得する
	[[nodiscard]] MoodType currentMood() const noexcept { return m_currentMood; }

	/// @brief トランジション中かどうか
	[[nodiscard]] bool isTransitioning() const noexcept { return m_transitioning; }

	/// @brief トランジション進行度（0.0〜1.0）
	[[nodiscard]] float transitionProgress() const noexcept
	{
		if (!m_transitioning) return 1.0f;
		return std::clamp(m_transitionElapsed / m_transitionDuration, 0.0f, 1.0f);
	}

	// ── テキストからの自動ムード設定 ──────────────────────────

	/// @brief テキストから自動的にムードを検出して設定する
	/// @param dialogueText 対話テキスト
	/// @param transitionDuration トランジション時間
	/// @param minConfidence 最小信頼度閾値（これ以下なら変更しない）
	/// @return 検出されたムード（設定された場合）
	[[nodiscard]] MoodDetection detectAndSetMood(
		std::string_view dialogueText,
		float transitionDuration = 2.0f,
		float minConfidence = 0.5f)
	{
		auto detection = m_detector.detect(dialogueText);
		if (detection.confidence >= minConfidence)
		{
			setMood(detection.mood, transitionDuration);
		}
		return detection;
	}

	/// @brief ムード検出器への参照を取得する（キーワードカスタマイズ用）
	[[nodiscard]] MoodDetector& moodDetector() noexcept { return m_detector; }
	[[nodiscard]] const MoodDetector& moodDetector() const noexcept { return m_detector; }

	// ── スクリプトコマンド連携 ──────────────────────────────────

	/// @brief スクリプトコマンド文字列からムードを設定する
	/// @details "@mood happy 2.0" 形式のコマンドを解析する
	/// @param commandArgs "@mood" 以降のコマンド引数
	/// @return 成功ならtrue
	bool parseMoodCommand(std::string_view commandArgs)
	{
		// 先頭空白をスキップ
		while (!commandArgs.empty() && commandArgs.front() == ' ')
		{
			commandArgs.remove_prefix(1);
		}

		if (commandArgs.empty()) return false;

		// ムード名を取得
		auto spacePos = commandArgs.find(' ');
		std::string_view moodName = (spacePos != std::string_view::npos)
			? commandArgs.substr(0, spacePos)
			: commandArgs;

		auto mood = parseMoodName(moodName);
		if (!mood.has_value()) return false;

		// トランジション時間を取得（省略時は1.0秒）
		float duration = 1.0f;
		if (spacePos != std::string_view::npos)
		{
			auto durationStr = commandArgs.substr(spacePos + 1);
			while (!durationStr.empty() && durationStr.front() == ' ')
			{
				durationStr.remove_prefix(1);
			}
			if (!durationStr.empty())
			{
				duration = parseFloat(durationStr, 1.0f);
			}
		}

		setMood(mood.value(), duration);
		return true;
	}

	// ── 更新処理 ──────────────────────────────────────────────

	/// @brief 毎フレーム更新
	/// @param deltaTime フレーム時間（秒）
	void update(float deltaTime)
	{
		if (!m_transitioning) return;

		m_transitionElapsed += deltaTime;
		float t = std::clamp(m_transitionElapsed / m_transitionDuration, 0.0f, 1.0f);

		// トランジション方式に応じた音量計算
		switch (m_transitionType)
		{
		case BGMTransitionType::Crossfade:
			updateCrossfade(t);
			break;
		case BGMTransitionType::DuckAndSwap:
			updateDuckAndSwap(t);
			break;
		case BGMTransitionType::LayerBlend:
			updateLayerBlend(t);
			break;
		}

		// トランジション完了
		if (t >= 1.0f)
		{
			m_transitioning = false;
			stopMoodTrack(m_previousMood, 0.0f);
		}
	}

	// ── 状態取得 ──────────────────────────────────────────────

	/// @brief 現在の全体状態を取得する
	[[nodiscard]] BGMState currentState() const
	{
		BGMState state;
		state.currentMood = m_currentMood;
		state.previousMood = m_previousMood;
		state.transitioning = m_transitioning;
		state.transitionProgress = transitionProgress();

		for (const auto& [audioId, volume] : m_trackVolumes)
		{
			TrackPlayState tps;
			tps.audioId = audioId;
			tps.volume = volume;
			tps.active = (volume > 0.001f);
			state.tracks.push_back(tps);
		}

		return state;
	}

	/// @brief マスターボリュームを設定する
	/// @param volume マスターボリューム（0.0〜1.0）
	void setMasterVolume(float volume) noexcept
	{
		m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
	}

	/// @brief マスターボリュームを取得する
	[[nodiscard]] float masterVolume() const noexcept { return m_masterVolume; }

private:
	/// @brief ムード名文字列からMoodTypeを解析する
	[[nodiscard]] static std::optional<MoodType> parseMoodName(std::string_view name) noexcept
	{
		if (name == "calm")       return MoodType::Calm;
		if (name == "happy")      return MoodType::Happy;
		if (name == "sad")        return MoodType::Sad;
		if (name == "tense")      return MoodType::Tense;
		if (name == "romantic")   return MoodType::Romantic;
		if (name == "mysterious") return MoodType::Mysterious;
		if (name == "action")     return MoodType::Action;
		if (name == "comedy")     return MoodType::Comedy;
		return std::nullopt;
	}

	/// @brief 浮動小数点数を解析する
	[[nodiscard]] static float parseFloat(std::string_view sv, float defaultValue)
	{
		if (sv.empty()) return defaultValue;
		try
		{
			return std::stof(std::string(sv));
		}
		catch (...)
		{
			return defaultValue;
		}
	}

	/// @brief ムード用トラックの再生を開始する
	void startMoodTrack(MoodType mood, float fadeDuration)
	{
		auto it = m_tracks.find(mood);
		if (it == m_tracks.end()) return;

		const auto& track = it->second;
		float volume = track.baseVolume * m_masterVolume;

		if (m_onPlay)
		{
			m_onPlay(track.audioId, volume, fadeDuration);
		}
		m_trackVolumes[track.audioId] = volume;

		// レイヤーも開始
		auto layerIt = m_layers.find(mood);
		if (layerIt != m_layers.end())
		{
			for (const auto& layer : layerIt->second)
			{
				float layerVol = layer.maxVolume * layer.volumeCurve * m_masterVolume;
				if (m_onPlay)
				{
					m_onPlay(layer.audioId, layerVol, fadeDuration);
				}
				m_trackVolumes[layer.audioId] = layerVol;
			}
		}
	}

	/// @brief ムード用トラックの再生を停止する
	void stopMoodTrack(MoodType mood, float fadeDuration)
	{
		auto it = m_tracks.find(mood);
		if (it != m_tracks.end())
		{
			if (m_onStop)
			{
				m_onStop(it->second.audioId, fadeDuration);
			}
			m_trackVolumes.erase(it->second.audioId);
		}

		auto layerIt = m_layers.find(mood);
		if (layerIt != m_layers.end())
		{
			for (const auto& layer : layerIt->second)
			{
				if (m_onStop)
				{
					m_onStop(layer.audioId, fadeDuration);
				}
				m_trackVolumes.erase(layer.audioId);
			}
		}
	}

	/// @brief クロスフェード更新
	void updateCrossfade(float t)
	{
		// 旧トラックをフェードアウト
		auto oldIt = m_tracks.find(m_previousMood);
		if (oldIt != m_tracks.end())
		{
			float oldVol = oldIt->second.baseVolume * (1.0f - t) * m_masterVolume;
			if (m_onVolume)
			{
				m_onVolume(oldIt->second.audioId, oldVol);
			}
			m_trackVolumes[oldIt->second.audioId] = oldVol;
		}

		// 新トラックをフェードイン
		auto newIt = m_tracks.find(m_currentMood);
		if (newIt != m_tracks.end())
		{
			float newVol = newIt->second.baseVolume * t * m_masterVolume;
			if (m_onVolume)
			{
				m_onVolume(newIt->second.audioId, newVol);
			}
			m_trackVolumes[newIt->second.audioId] = newVol;
		}
	}

	/// @brief ダック&スワップ更新
	void updateDuckAndSwap(float t)
	{
		if (t < 0.5f)
		{
			// 前半: 旧トラックをダック
			float duckT = t * 2.0f;
			auto oldIt = m_tracks.find(m_previousMood);
			if (oldIt != m_tracks.end())
			{
				float vol = oldIt->second.baseVolume * (1.0f - duckT) * m_masterVolume;
				if (m_onVolume)
				{
					m_onVolume(oldIt->second.audioId, vol);
				}
				m_trackVolumes[oldIt->second.audioId] = vol;
			}
		}
		else
		{
			// 後半: 新トラックをフェードイン
			float fadeT = (t - 0.5f) * 2.0f;
			auto newIt = m_tracks.find(m_currentMood);
			if (newIt != m_tracks.end())
			{
				float vol = newIt->second.baseVolume * fadeT * m_masterVolume;
				if (m_onVolume)
				{
					m_onVolume(newIt->second.audioId, vol);
				}
				m_trackVolumes[newIt->second.audioId] = vol;
			}
		}
	}

	/// @brief レイヤーブレンド更新
	void updateLayerBlend(float t)
	{
		// メイントラックはクロスフェード
		updateCrossfade(t);

		// 旧レイヤーをフェードアウト
		auto oldLayerIt = m_layers.find(m_previousMood);
		if (oldLayerIt != m_layers.end())
		{
			for (const auto& layer : oldLayerIt->second)
			{
				float vol = layer.maxVolume * layer.volumeCurve * (1.0f - t) * m_masterVolume;
				if (m_onVolume)
				{
					m_onVolume(layer.audioId, vol);
				}
				m_trackVolumes[layer.audioId] = vol;
			}
		}

		// 新レイヤーをフェードイン
		auto newLayerIt = m_layers.find(m_currentMood);
		if (newLayerIt != m_layers.end())
		{
			for (const auto& layer : newLayerIt->second)
			{
				float vol = layer.maxVolume * layer.volumeCurve * t * m_masterVolume;
				if (m_onVolume)
				{
					m_onVolume(layer.audioId, vol);
				}
				m_trackVolumes[layer.audioId] = vol;
			}
		}
	}

	// ── メンバー ─────────────────────────────────────────────────

	// トラック/レイヤー登録
	std::unordered_map<MoodType, BGMTrack, MoodTypeHash> m_tracks;
	std::unordered_map<MoodType, std::vector<BGMLayer>, MoodTypeHash> m_layers;

	// ムード検出器
	MoodDetector m_detector;

	// トランジション状態
	MoodType m_currentMood = MoodType::Calm;
	MoodType m_previousMood = MoodType::Calm;
	BGMTransitionType m_transitionType = BGMTransitionType::Crossfade;
	float m_transitionDuration = 1.0f;
	float m_transitionElapsed = 0.0f;
	bool m_transitioning = false;

	// 音量管理
	float m_masterVolume = 1.0f;
	std::unordered_map<std::string, float> m_trackVolumes;

	// コールバック
	PlayCallback m_onPlay;
	StopCallback m_onStop;
	VolumeCallback m_onVolume;
};

} // namespace mitiru::vn
