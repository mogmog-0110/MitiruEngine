#pragma once

/// @file ConfigScreen.hpp
/// @brief ビジュアルノベル用設定画面
/// @details テキスト速度、オート速度、各種音量、ウィンドウ透明度などの
///          設定UIとJSON永続化を提供する。

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <mitiru/audio/AudioMixer.hpp>
#include <mitiru/data/Json.hpp>
#include <mitiru/ui/UITheme.hpp>

namespace mitiru::vn
{

/// @brief ビジュアルノベルの設定データ
/// @details すべての設定値を保持する値型。JSONへのシリアライズ/デシリアライズをサポート。
struct VNConfig
{
	float textSpeed     = 0.5f;   ///< テキスト表示速度 [0.0=最遅, 1.0=瞬間表示]
	float autoSpeed     = 0.5f;   ///< オートモード速度 [0.0=最遅, 1.0=最速]
	float bgmVolume     = 0.8f;   ///< BGMボリューム [0.0, 1.0]
	float seVolume      = 0.8f;   ///< SEボリューム [0.0, 1.0]
	float voiceVolume   = 1.0f;   ///< ボイスボリューム [0.0, 1.0]
	float masterVolume  = 1.0f;   ///< マスターボリューム [0.0, 1.0]
	float windowAlpha   = 0.8f;   ///< メッセージウィンドウ透明度 [0.0, 1.0]
	bool  fullscreen    = false;  ///< フルスクリーン
	bool  skipUnread    = false;  ///< 未読テキストのスキップを許可
	int   textSize      = 24;     ///< テキストフォントサイズ
	std::string language = "ja";  ///< 言語コード

	/// @brief テキスト速度を1秒あたりの文字数に変換する
	/// @return 文字/秒（1.0の場合は非常に大きい値=瞬間表示）
	[[nodiscard]] float textCharsPerSecond() const noexcept
	{
		if (textSpeed >= 1.0f)
		{
			return 9999.0f;
		}
		/// 0.0 → 5文字/秒, 0.5 → 30文字/秒, 0.99 → 200文字/秒
		return 5.0f + textSpeed * 195.0f;
	}

	/// @brief オート速度をスケール値に変換する
	/// @return スケール値（小さいほど待ち時間が短い）
	[[nodiscard]] float autoDelayScale() const noexcept
	{
		/// 0.0 → 2.0倍遅い, 0.5 → 1.0倍（標準）, 1.0 → 0.2倍速い
		return 2.0f - autoSpeed * 1.8f;
	}

	/// @brief デフォルト設定を返す
	[[nodiscard]] static VNConfig defaults() noexcept { return VNConfig{}; }

	/// @brief JSON文字列にシリアライズする
	[[nodiscard]] std::string toJson() const
	{
		mitiru::data::Json j;
		j["textSpeed"]    = textSpeed;
		j["autoSpeed"]    = autoSpeed;
		j["bgmVolume"]    = bgmVolume;
		j["seVolume"]     = seVolume;
		j["voiceVolume"]  = voiceVolume;
		j["masterVolume"] = masterVolume;
		j["windowAlpha"]  = windowAlpha;
		j["fullscreen"]   = fullscreen;
		j["skipUnread"]   = skipUnread;
		j["textSize"]     = textSize;
		j["language"]     = language;
		return j.dump();
	}

	/// @brief JSON文字列からデシリアライズする
	/// @param json JSON文字列
	/// @return パースした設定（パース失敗時はデフォルト値が残る）
	[[nodiscard]] static VNConfig fromJson(std::string_view json)
	{
		VNConfig config;
		auto j = mitiru::data::Json::parse(std::string(json), nullptr, false);
		if (j.is_discarded()) return config;

		if (j.contains("textSpeed")    && j["textSpeed"].is_number())    config.textSpeed    = j["textSpeed"].get<float>();
		if (j.contains("autoSpeed")    && j["autoSpeed"].is_number())    config.autoSpeed    = j["autoSpeed"].get<float>();
		if (j.contains("bgmVolume")    && j["bgmVolume"].is_number())    config.bgmVolume    = j["bgmVolume"].get<float>();
		if (j.contains("seVolume")     && j["seVolume"].is_number())     config.seVolume     = j["seVolume"].get<float>();
		if (j.contains("voiceVolume")  && j["voiceVolume"].is_number())  config.voiceVolume  = j["voiceVolume"].get<float>();
		if (j.contains("masterVolume") && j["masterVolume"].is_number()) config.masterVolume = j["masterVolume"].get<float>();
		if (j.contains("windowAlpha")  && j["windowAlpha"].is_number())  config.windowAlpha  = j["windowAlpha"].get<float>();
		if (j.contains("fullscreen")   && j["fullscreen"].is_boolean())  config.fullscreen   = j["fullscreen"].get<bool>();
		if (j.contains("skipUnread")   && j["skipUnread"].is_boolean())  config.skipUnread   = j["skipUnread"].get<bool>();
		if (j.contains("textSize")     && j["textSize"].is_number())     config.textSize     = j["textSize"].get<int>();
		if (j.contains("language")     && j["language"].is_string())     config.language     = j["language"].get<std::string>();
		return config;
	}
};

/// @brief 設定画面のウィジェット配置パラメータ
/// @details レンダラーが参照するレイアウト定数。setLayout()で上書き可能。
struct ConfigScreenLayout
{
	float startY        = 80.0f;   ///< 最初のウィジェットのY座標
	float itemSpacing   = 50.0f;   ///< ウィジェット間の縦間隔
	float labelX        = 50.0f;   ///< ラベルのX座標
	float sliderX       = 250.0f;  ///< スライダーのX座標
	float sliderWidth   = 300.0f;  ///< スライダーの幅
	float sliderHeight  = 8.0f;    ///< スライダーのトラック高さ
	float handleWidth   = 8.0f;    ///< スライダーハンドルの幅
	float handleHeight  = 16.0f;   ///< スライダーハンドルの高さ
	float buttonSpacing = 20.0f;   ///< ボタン間の横間隔
	float buttonWidth   = 120.0f;  ///< ボタンの幅
	float buttonHeight  = 36.0f;   ///< ボタンの高さ
	float fontSize      = 16.0f;   ///< ラベルフォントサイズ
	float valueFontSize = 14.0f;   ///< 値表示フォントサイズ
};

/// @brief スライダーウィジェットの状態
struct SliderWidget
{
	std::string label;          ///< ラベルテキスト
	float minValue   = 0.0f;   ///< 最小値
	float maxValue   = 1.0f;   ///< 最大値
	float current    = 0.5f;   ///< 現在値
	float step       = 0.05f;  ///< ステップ幅
	bool  dragging   = false;  ///< ドラッグ中フラグ
	bool  focused    = false;  ///< フォーカス状態

	/// @brief 値を正規化して取得する [0.0, 1.0]
	[[nodiscard]] float normalized() const noexcept
	{
		const float range = maxValue - minValue;
		if (range <= 0.0f)
		{
			return 0.0f;
		}
		return std::clamp((current - minValue) / range, 0.0f, 1.0f);
	}

	/// @brief 正規化値から実値を設定する
	/// @param norm 正規化値 [0.0, 1.0]
	void setFromNormalized(float norm) noexcept
	{
		current = std::clamp(minValue + norm * (maxValue - minValue),
		                     minValue, maxValue);
	}

	/// @brief ステップ分増加する
	void increment() noexcept
	{
		current = std::min(current + step, maxValue);
	}

	/// @brief ステップ分減少する
	void decrement() noexcept
	{
		current = std::max(current - step, minValue);
	}

	/// @brief ドラッグ開始
	void beginDrag() noexcept { dragging = true; }

	/// @brief ドラッグ終了
	void endDrag() noexcept { dragging = false; }

	/// @brief ドラッグ中に正規化位置で更新する
	/// @param normalizedPos ドラッグ位置 [0.0, 1.0]
	void dragTo(float normalizedPos) noexcept
	{
		if (dragging)
		{
			setFromNormalized(normalizedPos);
		}
	}

	/// @brief 表示用テキストを取得する
	[[nodiscard]] std::string displayText() const
	{
		const int percent = static_cast<int>(normalized() * 100.0f + 0.5f);
		return label + ": " + std::to_string(percent) + "%";
	}
};

/// @brief トグルウィジェットの状態
struct ToggleWidget
{
	std::string label;          ///< ラベルテキスト
	bool value   = false;       ///< 現在値
	bool focused = false;       ///< フォーカス状態

	/// @brief トグルする
	void toggle() noexcept { value = !value; }

	/// @brief 表示用テキストを取得する
	[[nodiscard]] std::string displayText() const
	{
		return label + ": " + (value ? "ON" : "OFF");
	}
};

/// @brief 設定画面のボタン種別
enum class ConfigAction : std::uint8_t
{
	None    = 0,  ///< 何もしない
	Apply   = 1,  ///< 設定を適用する
	Cancel  = 2,  ///< 変更をキャンセルする
	Reset   = 3,  ///< デフォルトに戻す
};

/// @brief ビジュアルノベル設定画面
/// @details スライダー/トグルウィジェットで設定を調整し、
///          AudioMixerへのリアルタイムプレビュー、JSON永続化を提供する。
///
/// @code
/// mitiru::vn::ConfigScreen screen;
/// screen.open(currentConfig);
///
/// // 毎フレーム
/// auto action = screen.update(dt);
/// if (action == mitiru::vn::ConfigAction::Apply) {
///     auto newConfig = screen.editingConfig();
///     saveToFile(newConfig.toJson());
/// }
/// @endcode
class ConfigScreen
{
public:
	/// @brief コンストラクタ
	ConfigScreen() noexcept = default;

	/// @brief テーマ指定付きコンストラクタ
	/// @param theme UIテーマ
	explicit ConfigScreen(const ui::UITheme& theme) noexcept
		: m_theme(theme)
	{
	}

	// ── 画面制御 ──────────────────────────────────────────

	/// @brief 設定画面を開く
	/// @param config 現在の設定（編集開始点）
	void open(const VNConfig& config)
	{
		m_originalConfig = config;
		m_editingConfig = config;
		m_visible = true;
		m_focusIndex = 0;
		rebuildWidgets();
	}

	/// @brief 設定画面を閉じる
	void close() noexcept
	{
		m_visible = false;
	}

	/// @brief 設定画面が表示中かどうか
	[[nodiscard]] bool isVisible() const noexcept { return m_visible; }

	// ── 入力処理 ──────────────────────────────────────────

	/// @brief フォーカスを上に移動する
	void onFocusUp() noexcept
	{
		if (totalWidgetCount() == 0)
		{
			return;
		}
		m_focusIndex = (m_focusIndex - 1 + totalWidgetCount()) % totalWidgetCount();
		updateFocusState();
	}

	/// @brief フォーカスを下に移動する
	void onFocusDown() noexcept
	{
		if (totalWidgetCount() == 0)
		{
			return;
		}
		m_focusIndex = (m_focusIndex + 1) % totalWidgetCount();
		updateFocusState();
	}

	/// @brief 左キー押下（スライダー減少 / トグル切替）
	void onLeft() noexcept
	{
		if (isSliderFocused())
		{
			auto& slider = focusedSlider();
			slider.decrement();
			applySliderToConfig(m_focusIndex);
			previewVolumes();
		}
		else if (isToggleFocused())
		{
			auto& toggle = focusedToggle();
			toggle.toggle();
			applyToggleToConfig(m_focusIndex);
		}
	}

	/// @brief 右キー押下（スライダー増加 / トグル切替）
	void onRight() noexcept
	{
		if (isSliderFocused())
		{
			auto& slider = focusedSlider();
			slider.increment();
			applySliderToConfig(m_focusIndex);
			previewVolumes();
		}
		else if (isToggleFocused())
		{
			auto& toggle = focusedToggle();
			toggle.toggle();
			applyToggleToConfig(m_focusIndex);
		}
	}

	/// @brief 決定キー押下（トグル切替 / ボタン押下）
	/// @return 実行されたアクション
	[[nodiscard]] ConfigAction onConfirm() noexcept
	{
		if (isToggleFocused())
		{
			auto& toggle = focusedToggle();
			toggle.toggle();
			applyToggleToConfig(m_focusIndex);
			return ConfigAction::None;
		}

		/// ボタン領域のフォーカス判定
		const int buttonBase = static_cast<int>(m_sliders.size() + m_toggles.size());
		const int buttonIndex = m_focusIndex - buttonBase;
		if (buttonIndex >= 0 && buttonIndex < 3)
		{
			switch (buttonIndex)
			{
			case 0: return applyAction();
			case 1: return cancelAction();
			case 2: return resetAction();
			}
		}

		return ConfigAction::None;
	}

	/// @brief スライダーのドラッグ更新
	/// @param sliderIndex スライダーインデックス
	/// @param normalizedPos ドラッグ位置 [0.0, 1.0]
	void onSliderDrag(int sliderIndex, float normalizedPos)
	{
		if (sliderIndex < 0 || sliderIndex >= static_cast<int>(m_sliders.size()))
		{
			return;
		}
		m_sliders[static_cast<std::size_t>(sliderIndex)].dragTo(normalizedPos);
		applySliderToConfig(sliderIndex);
		previewVolumes();
	}

	// ── ボタンアクション ──────────────────────────────────

	/// @brief 適用ボタン
	[[nodiscard]] ConfigAction applyAction() noexcept
	{
		m_originalConfig = m_editingConfig;
		close();
		return ConfigAction::Apply;
	}

	/// @brief キャンセルボタン
	[[nodiscard]] ConfigAction cancelAction() noexcept
	{
		m_editingConfig = m_originalConfig;
		restoreVolumes();
		close();
		return ConfigAction::Cancel;
	}

	/// @brief デフォルトに戻すボタン
	[[nodiscard]] ConfigAction resetAction()
	{
		m_editingConfig = VNConfig::defaults();
		rebuildWidgets();
		previewVolumes();
		return ConfigAction::Reset;
	}

	// ── 更新 ────────────────────────────────────────────────

	/// @brief 毎フレーム更新
	/// @param deltaTime 前フレームからの経過時間（秒）
	/// @return このフレームで発生したアクション
	[[nodiscard]] ConfigAction update([[maybe_unused]] float deltaTime) noexcept
	{
		/// 設定画面自体には時間ベースの動作はないが、
		/// 将来のアニメーション用に引数を保持する。
		return ConfigAction::None;
	}

	// ── AudioMixer連携 ──────────────────────────────────────

	/// @brief プレビュー用AudioMixerを設定する
	/// @param mixer AudioMixerへのポインタ（nullptr可）
	void setAudioMixer(audio::AudioMixer* mixer) noexcept
	{
		m_mixer = mixer;
	}

	// ── 状態アクセス ──────────────────────────────────────

	/// @brief 編集中の設定を取得する
	[[nodiscard]] const VNConfig& editingConfig() const noexcept
	{
		return m_editingConfig;
	}

	/// @brief 元の設定（画面を開いた時点の設定）を取得する
	[[nodiscard]] const VNConfig& originalConfig() const noexcept
	{
		return m_originalConfig;
	}

	/// @brief スライダー一覧を取得する
	[[nodiscard]] const std::vector<SliderWidget>& sliders() const noexcept
	{
		return m_sliders;
	}

	/// @brief トグル一覧を取得する
	[[nodiscard]] const std::vector<ToggleWidget>& toggles() const noexcept
	{
		return m_toggles;
	}

	/// @brief フォーカスインデックスを取得する
	[[nodiscard]] int focusIndex() const noexcept { return m_focusIndex; }

	/// @brief テーマを取得する
	[[nodiscard]] const ui::UITheme& theme() const noexcept { return m_theme; }

	/// @brief テーマを設定する
	void setTheme(const ui::UITheme& theme) noexcept { m_theme = theme; }

	/// @brief レイアウトを取得する
	[[nodiscard]] const ConfigScreenLayout& layout() const noexcept { return m_layout; }

	/// @brief レイアウトを設定する
	/// @param layout ウィジェット配置パラメータ
	void setLayout(const ConfigScreenLayout& layout) noexcept { m_layout = layout; }

	/// @brief 設定が変更されているかどうか
	[[nodiscard]] bool isDirty() const noexcept
	{
		return m_editingConfig.textSpeed    != m_originalConfig.textSpeed
		    || m_editingConfig.autoSpeed    != m_originalConfig.autoSpeed
		    || m_editingConfig.bgmVolume    != m_originalConfig.bgmVolume
		    || m_editingConfig.seVolume     != m_originalConfig.seVolume
		    || m_editingConfig.voiceVolume  != m_originalConfig.voiceVolume
		    || m_editingConfig.masterVolume != m_originalConfig.masterVolume
		    || m_editingConfig.windowAlpha  != m_originalConfig.windowAlpha
		    || m_editingConfig.fullscreen   != m_originalConfig.fullscreen
		    || m_editingConfig.skipUnread   != m_originalConfig.skipUnread
		    || m_editingConfig.textSize     != m_originalConfig.textSize
		    || m_editingConfig.language     != m_originalConfig.language;
	}

private:
	/// @brief ウィジェットを構築する
	void rebuildWidgets()
	{
		m_sliders.clear();
		m_toggles.clear();

		/// スライダー群（順番固定: text, auto, master, bgm, se, voice, windowAlpha）
		m_sliders.push_back(SliderWidget{"Text Speed",    0.0f, 1.0f, m_editingConfig.textSpeed,    0.05f});
		m_sliders.push_back(SliderWidget{"Auto Speed",    0.0f, 1.0f, m_editingConfig.autoSpeed,    0.05f});
		m_sliders.push_back(SliderWidget{"Master Volume", 0.0f, 1.0f, m_editingConfig.masterVolume, 0.05f});
		m_sliders.push_back(SliderWidget{"BGM Volume",    0.0f, 1.0f, m_editingConfig.bgmVolume,    0.05f});
		m_sliders.push_back(SliderWidget{"SE Volume",     0.0f, 1.0f, m_editingConfig.seVolume,     0.05f});
		m_sliders.push_back(SliderWidget{"Voice Volume",  0.0f, 1.0f, m_editingConfig.voiceVolume,  0.05f});
		m_sliders.push_back(SliderWidget{"Window Alpha",  0.0f, 1.0f, m_editingConfig.windowAlpha,  0.05f});

		/// トグル群
		m_toggles.push_back(ToggleWidget{"Fullscreen", m_editingConfig.fullscreen});
		m_toggles.push_back(ToggleWidget{"Skip Unread", m_editingConfig.skipUnread});

		updateFocusState();
	}

	/// @brief ウィジェット総数を取得する（スライダー + トグル + ボタン3つ）
	[[nodiscard]] int totalWidgetCount() const noexcept
	{
		return static_cast<int>(m_sliders.size() + m_toggles.size()) + 3;
	}

	/// @brief フォーカス状態を全ウィジェットに反映する
	void updateFocusState() noexcept
	{
		for (auto& s : m_sliders) { s.focused = false; }
		for (auto& t : m_toggles) { t.focused = false; }

		if (m_focusIndex < static_cast<int>(m_sliders.size()))
		{
			m_sliders[static_cast<std::size_t>(m_focusIndex)].focused = true;
		}
		else
		{
			const int toggleIdx = m_focusIndex - static_cast<int>(m_sliders.size());
			if (toggleIdx >= 0 && toggleIdx < static_cast<int>(m_toggles.size()))
			{
				m_toggles[static_cast<std::size_t>(toggleIdx)].focused = true;
			}
		}
	}

	/// @brief フォーカスがスライダーにあるかどうか
	[[nodiscard]] bool isSliderFocused() const noexcept
	{
		return m_focusIndex >= 0 && m_focusIndex < static_cast<int>(m_sliders.size());
	}

	/// @brief フォーカスがトグルにあるかどうか
	[[nodiscard]] bool isToggleFocused() const noexcept
	{
		const int toggleIdx = m_focusIndex - static_cast<int>(m_sliders.size());
		return toggleIdx >= 0 && toggleIdx < static_cast<int>(m_toggles.size());
	}

	/// @brief フォーカス中のスライダーを取得する
	[[nodiscard]] SliderWidget& focusedSlider() noexcept
	{
		return m_sliders[static_cast<std::size_t>(m_focusIndex)];
	}

	/// @brief フォーカス中のトグルを取得する
	[[nodiscard]] ToggleWidget& focusedToggle() noexcept
	{
		const int idx = m_focusIndex - static_cast<int>(m_sliders.size());
		return m_toggles[static_cast<std::size_t>(idx)];
	}

	/// @brief スライダー値を編集中設定に反映する
	/// @param sliderIndex スライダーインデックス
	void applySliderToConfig(int sliderIndex) noexcept
	{
		if (sliderIndex < 0 || sliderIndex >= static_cast<int>(m_sliders.size()))
		{
			return;
		}
		const float val = m_sliders[static_cast<std::size_t>(sliderIndex)].current;

		switch (sliderIndex)
		{
		case 0: m_editingConfig.textSpeed    = val; break;
		case 1: m_editingConfig.autoSpeed    = val; break;
		case 2: m_editingConfig.masterVolume = val; break;
		case 3: m_editingConfig.bgmVolume    = val; break;
		case 4: m_editingConfig.seVolume     = val; break;
		case 5: m_editingConfig.voiceVolume  = val; break;
		case 6: m_editingConfig.windowAlpha  = val; break;
		default: break;
		}
	}

	/// @brief トグル値を編集中設定に反映する
	/// @param widgetIndex ウィジェットインデックス（全体）
	void applyToggleToConfig(int widgetIndex) noexcept
	{
		const int toggleIdx = widgetIndex - static_cast<int>(m_sliders.size());
		if (toggleIdx < 0 || toggleIdx >= static_cast<int>(m_toggles.size()))
		{
			return;
		}
		const bool val = m_toggles[static_cast<std::size_t>(toggleIdx)].value;

		switch (toggleIdx)
		{
		case 0: m_editingConfig.fullscreen = val; break;
		case 1: m_editingConfig.skipUnread = val; break;
		default: break;
		}
	}

	/// @brief 音量プレビューをAudioMixerに反映する
	void previewVolumes() noexcept
	{
		if (!m_mixer)
		{
			return;
		}
		m_mixer->setMasterVolume(m_editingConfig.masterVolume);
		m_mixer->setCategoryVolume(audio::SoundCategory::Bgm,   m_editingConfig.bgmVolume);
		m_mixer->setCategoryVolume(audio::SoundCategory::Se,    m_editingConfig.seVolume);
		m_mixer->setCategoryVolume(audio::SoundCategory::Voice, m_editingConfig.voiceVolume);
	}

	/// @brief 元の音量設定をAudioMixerに復元する
	void restoreVolumes() noexcept
	{
		if (!m_mixer)
		{
			return;
		}
		m_mixer->setMasterVolume(m_originalConfig.masterVolume);
		m_mixer->setCategoryVolume(audio::SoundCategory::Bgm,   m_originalConfig.bgmVolume);
		m_mixer->setCategoryVolume(audio::SoundCategory::Se,    m_originalConfig.seVolume);
		m_mixer->setCategoryVolume(audio::SoundCategory::Voice, m_originalConfig.voiceVolume);
	}

	VNConfig m_editingConfig;                 ///< 編集中の設定
	VNConfig m_originalConfig;                ///< 元の設定
	bool m_visible = false;                   ///< 表示中フラグ
	int m_focusIndex = 0;                     ///< フォーカスインデックス

	std::vector<SliderWidget> m_sliders;      ///< スライダーウィジェット群
	std::vector<ToggleWidget> m_toggles;      ///< トグルウィジェット群

	audio::AudioMixer* m_mixer = nullptr;     ///< プレビュー用AudioMixer（非所有）
	ui::UITheme m_theme;                      ///< UIテーマ
	ConfigScreenLayout m_layout;              ///< ウィジェット配置パラメータ
};

} // namespace mitiru::vn
