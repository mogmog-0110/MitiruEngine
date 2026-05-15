#pragma once

/// @file UISkinLoader.hpp
/// @brief VN用UIスキン定義とローダー
/// @details カスタムスキンでVNの全UI要素（メッセージウィンドウ、選択肢ボタン、
///          セーブスロット、スライダー等）の外観を差し替えるシステム。
///          JSONファイルからスキンを読み込み、デフォルトスキンとマージし、
///          ホットリロードをサポートする。
///
/// @code
/// mitiru::vn::UISkinLoader loader;
/// auto skin = loader.loadFromJson(jsonString);
/// // デフォルトにないフィールドは自動補完される
///
/// const auto& msgWin = skin.messageWindow;
/// const auto& choice = skin.choiceButton;
/// @endcode

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru::vn
{

// ── カラーユーティリティ ────────────────────────────────────────

/// @brief 16進カラーコードからColorfに変換する
/// @param hex "#RRGGBB" または "#RRGGBBAA" 形式
/// @return 変換された色（不正な場合は白）
[[nodiscard]] inline sgc::Colorf colorFromHex(std::string_view hex) noexcept
{
	if (hex.empty() || hex[0] != '#') { return {1.0f, 1.0f, 1.0f, 1.0f}; }
	hex.remove_prefix(1);

	auto parseComponent = [](std::string_view s) -> float {
		unsigned int val = 0;
		for (char c : s)
		{
			val <<= 4;
			if (c >= '0' && c <= '9') { val += static_cast<unsigned int>(c - '0'); }
			else if (c >= 'a' && c <= 'f') { val += static_cast<unsigned int>(c - 'a' + 10); }
			else if (c >= 'A' && c <= 'F') { val += static_cast<unsigned int>(c - 'A' + 10); }
		}
		return static_cast<float>(val) / 255.0f;
	};

	if (hex.size() == 6)
	{
		return {parseComponent(hex.substr(0, 2)),
		        parseComponent(hex.substr(2, 2)),
		        parseComponent(hex.substr(4, 2)),
		        1.0f};
	}
	if (hex.size() == 8)
	{
		return {parseComponent(hex.substr(0, 2)),
		        parseComponent(hex.substr(2, 2)),
		        parseComponent(hex.substr(4, 2)),
		        parseComponent(hex.substr(6, 2))};
	}
	return {1.0f, 1.0f, 1.0f, 1.0f};
}

// ── パディング ──────────────────────────────────────────────────

/// @brief 上下左右のパディング値
struct Padding
{
	float top = 20.0f;
	float bottom = 20.0f;
	float left = 30.0f;
	float right = 30.0f;
};

// ── メッセージウィンドウスキン ───────────────────────────────────

/// @brief メッセージウィンドウの背景タイプ
enum class WindowBackgroundType : std::uint8_t
{
	Solid,      ///< 単色塗りつぶし
	NineSlice,  ///< 9スライス画像
	Image,      ///< 画像全体
};

/// @brief メッセージウィンドウのスキン定義
struct MessageWindowSkin
{
	WindowBackgroundType backgroundType = WindowBackgroundType::Solid;
	std::string backgroundImage;     ///< 背景画像パス（NineSlice/Image時）
	float cornerSize = 16.0f;        ///< コーナーサイズ（NineSlice時）
	sgc::Colorf backgroundColor{0.0f, 0.0f, 0.1f, 0.85f}; ///< 背景色（Solid時）
	float alpha = 0.85f;             ///< 全体の不透明度
	Padding padding;                 ///< テキスト領域のパディング

	/// @brief 名前プレート設定
	struct NamePlate
	{
		WindowBackgroundType type = WindowBackgroundType::Solid;
		std::string image;
		sgc::Colorf backgroundColor{0.13f, 0.27f, 0.67f, 1.0f};
		sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};
		Padding padding{8.0f, 8.0f, 16.0f, 16.0f};
	} namePlate;

	/// @brief 待機アイコン設定
	struct WaitIcon
	{
		std::string image;             ///< アイコン画像パス
		float size = 16.0f;            ///< アイコンサイズ
		float blinkSpeed = 2.0f;       ///< 点滅速度（Hz）
		float offsetX = -24.0f;        ///< 右下からのXオフセット
		float offsetY = -20.0f;        ///< 右下からのYオフセット
	} waitIcon;
};

// ── 選択肢ボタンスキン ──────────────────────────────────────────

/// @brief UI要素の状態別スタイル
struct ElementState
{
	sgc::Colorf backgroundColor{0.2f, 0.2f, 0.3f, 0.9f};
	sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};
	sgc::Colorf borderColor{0.4f, 0.4f, 0.6f, 1.0f};
	float borderWidth = 1.0f;
	std::string backgroundImage;
};

/// @brief 選択肢ボタンのスキン定義
struct ChoiceButtonSkin
{
	ElementState normal;
	ElementState hover{
		{0.3f, 0.3f, 0.5f, 0.95f},
		{1.0f, 1.0f, 0.8f, 1.0f},
		{0.6f, 0.6f, 1.0f, 1.0f},
		2.0f,
		""
	};
	ElementState selected{
		{0.1f, 0.3f, 0.6f, 0.95f},
		{1.0f, 1.0f, 1.0f, 1.0f},
		{0.4f, 0.6f, 1.0f, 1.0f},
		2.0f,
		""
	};
	ElementState disabled{
		{0.15f, 0.15f, 0.15f, 0.6f},
		{0.5f, 0.5f, 0.5f, 0.8f},
		{0.3f, 0.3f, 0.3f, 0.6f},
		1.0f,
		""
	};
	Padding padding{12.0f, 12.0f, 24.0f, 24.0f};
	float height = 48.0f;
	float spacing = 8.0f;       ///< 選択肢間のスペース
};

// ── セーブスロットスキン ────────────────────────────────────────

/// @brief セーブスロットのスキン定義
struct SaveSlotSkin
{
	ElementState normal{
		{0.1f, 0.1f, 0.15f, 0.9f},
		{0.9f, 0.9f, 0.9f, 1.0f},
		{0.3f, 0.3f, 0.4f, 1.0f},
		1.0f,
		""
	};
	ElementState hover{
		{0.15f, 0.15f, 0.25f, 0.95f},
		{1.0f, 1.0f, 1.0f, 1.0f},
		{0.5f, 0.5f, 0.7f, 1.0f},
		1.5f,
		""
	};
	float thumbnailWidth = 160.0f;
	float thumbnailHeight = 90.0f;
	Padding textPadding{8.0f, 8.0f, 12.0f, 12.0f};
	float slotHeight = 120.0f;
	float spacing = 4.0f;
};

// ── スライダースキン ────────────────────────────────────────────

/// @brief コンフィグスライダーのスキン定義
struct ConfigSliderSkin
{
	sgc::Colorf trackColor{0.2f, 0.2f, 0.3f, 0.8f};
	sgc::Colorf fillColor{0.3f, 0.5f, 0.9f, 1.0f};
	sgc::Colorf handleColor{0.8f, 0.8f, 0.9f, 1.0f};
	std::string trackImage;
	std::string fillImage;
	std::string handleImage;
	float trackHeight = 6.0f;
	float handleSize = 20.0f;
};

// ── スクロールバースキン ────────────────────────────────────────

/// @brief スクロールバーのスキン定義
struct ScrollBarSkin
{
	sgc::Colorf trackColor{0.1f, 0.1f, 0.15f, 0.5f};
	sgc::Colorf thumbColor{0.4f, 0.4f, 0.5f, 0.8f};
	sgc::Colorf thumbHoverColor{0.5f, 0.5f, 0.7f, 0.9f};
	std::string trackImage;
	std::string thumbImage;
	float width = 12.0f;
	float thumbMinHeight = 24.0f;
};

// ── フォント設定 ────────────────────────────────────────────────

/// @brief フォント設定
struct FontConfig
{
	std::string mainFont;            ///< メインフォントパス
	std::string nameFont;            ///< 名前表示フォントパス
	std::string choiceFont;          ///< 選択肢フォントパス
	std::string systemFont;          ///< システムフォントパス
	float mainSize = 24.0f;          ///< メインフォントサイズ
	float nameSize = 20.0f;          ///< 名前フォントサイズ
	float choiceSize = 22.0f;        ///< 選択肢フォントサイズ
	float systemSize = 18.0f;        ///< システムフォントサイズ
};

// ── テキストカラー設定 ──────────────────────────────────────────

/// @brief テキスト色設定
struct TextColorConfig
{
	sgc::Colorf main{1.0f, 1.0f, 1.0f, 1.0f};
	sgc::Colorf shadow{0.0f, 0.0f, 0.0f, 0.5f};
	sgc::Colorf outline{0.0f, 0.0f, 0.0f, 0.8f};
	sgc::Colorf name{1.0f, 1.0f, 1.0f, 1.0f};
	sgc::Colorf choice{1.0f, 1.0f, 1.0f, 1.0f};
	sgc::Colorf system{0.8f, 0.8f, 0.8f, 1.0f};
};

// ── UIスキン ────────────────────────────────────────────────────

/// @brief VN UIの完全なスキン定義
struct UISkin
{
	std::string name = "Default";         ///< スキン名
	MessageWindowSkin messageWindow;      ///< メッセージウィンドウ
	ChoiceButtonSkin choiceButton;        ///< 選択肢ボタン
	SaveSlotSkin saveSlot;                ///< セーブスロット
	ConfigSliderSkin configSlider;        ///< コンフィグスライダー
	ScrollBarSkin scrollBar;              ///< スクロールバー
	FontConfig fonts;                     ///< フォント設定
	TextColorConfig textColors;           ///< テキスト色設定

	/// @brief デフォルトスキンを生成する
	[[nodiscard]] static UISkin defaultSkin() noexcept
	{
		return UISkin{};
	}
};

// ── バリデーション結果 ──────────────────────────────────────────

/// @brief スキンバリデーションの結果
struct SkinValidationResult
{
	bool isValid = true;
	std::vector<std::string> warnings;
	std::vector<std::string> errors;
};

// ── スキンローダー ──────────────────────────────────────────────

/// @brief UIスキンのJSONローダー
/// @details JSONからスキンを読み込み、デフォルトスキンとマージする。
///          指定されていないフィールドはデフォルト値が使われる。
class UISkinLoader
{
	UISkin m_currentSkin;
	std::string m_lastLoadedPath;
	std::function<void(const UISkin&)> m_onReloadCallback;

public:
	/// @brief デフォルトスキンで初期化する
	UISkinLoader() noexcept
		: m_currentSkin(UISkin::defaultSkin())
	{
	}

	/// @brief 現在のスキンを取得する
	[[nodiscard]] const UISkin& skin() const noexcept { return m_currentSkin; }

	/// @brief JSON文字列からスキンを読み込む
	/// @param json JSON文字列
	/// @return 読み込まれたスキン
	[[nodiscard]] UISkin loadFromJson(std::string_view json)
	{
		UISkin skin = UISkin::defaultSkin();
		parseJson(json, skin);
		m_currentSkin = skin;
		return skin;
	}

	/// @brief スキンをJSON文字列からマージ読み込みする
	/// @param json JSON文字列
	/// @param baseSkin ベーススキン（未指定フィールドはここから継承）
	/// @return マージされたスキン
	[[nodiscard]] UISkin loadFromJson(std::string_view json, const UISkin& baseSkin)
	{
		UISkin skin = baseSkin;
		parseJson(json, skin);
		m_currentSkin = skin;
		return skin;
	}

	/// @brief スキンの完全性を検証する
	/// @param skin 検証対象スキン
	/// @return バリデーション結果
	[[nodiscard]] static SkinValidationResult validate(const UISkin& skin)
	{
		SkinValidationResult result;

		if (skin.name.empty())
		{
			result.warnings.push_back("Skin name is empty");
		}

		// メッセージウィンドウの画像チェック
		if (skin.messageWindow.backgroundType != WindowBackgroundType::Solid &&
		    skin.messageWindow.backgroundImage.empty())
		{
			result.errors.push_back("MessageWindow requires background image for non-solid type");
			result.isValid = false;
		}

		// フォントパスチェック
		if (skin.fonts.mainFont.empty())
		{
			result.warnings.push_back("Main font path not specified; will use engine default");
		}

		// パディング妥当性
		const auto& p = skin.messageWindow.padding;
		if (p.top < 0.0f || p.bottom < 0.0f || p.left < 0.0f || p.right < 0.0f)
		{
			result.errors.push_back("MessageWindow padding must be non-negative");
			result.isValid = false;
		}

		// スライダーハンドルサイズ
		if (skin.configSlider.handleSize <= 0.0f)
		{
			result.warnings.push_back("Slider handle size is zero or negative");
		}

		return result;
	}

	/// @brief ホットリロード用コールバックを設定する
	/// @param callback スキン更新時に呼ばれるコールバック
	void setReloadCallback(std::function<void(const UISkin&)> callback)
	{
		m_onReloadCallback = std::move(callback);
	}

	/// @brief 最後に読み込んだパスを記録する（ホットリロード用）
	/// @param path ファイルパス
	void setLoadedPath(const std::string& path)
	{
		m_lastLoadedPath = path;
	}

	/// @brief 最後に読み込んだパスを取得する
	[[nodiscard]] const std::string& lastLoadedPath() const noexcept
	{
		return m_lastLoadedPath;
	}

	/// @brief リロードをトリガーする（外部からのファイル変更検知後に呼ぶ）
	/// @param json 新しいJSON文字列
	void reload(std::string_view json)
	{
		m_currentSkin = loadFromJson(json);
		if (m_onReloadCallback)
		{
			m_onReloadCallback(m_currentSkin);
		}
	}

	/// @brief スキンをJSON文字列にエクスポートする
	/// @param skin エクスポート対象スキン
	/// @return JSON文字列
	[[nodiscard]] static std::string toJson(const UISkin& skin)
	{
		nlohmann::json j;
		j["name"] = skin.name;

		// messageWindow
		{
			nlohmann::json mw;
			mw["type"] = backgroundTypeToString(skin.messageWindow.backgroundType);
			if (!skin.messageWindow.backgroundImage.empty())
			{
				mw["image"] = skin.messageWindow.backgroundImage;
			}
			mw["cornerSize"] = skin.messageWindow.cornerSize;
			mw["alpha"] = skin.messageWindow.alpha;
			mw["backgroundColor"] = colorToHex(skin.messageWindow.backgroundColor);
			mw["padding"] = paddingToJsonObj(skin.messageWindow.padding);
			j["messageWindow"] = mw;
		}

		// nameplate
		{
			nlohmann::json np;
			np["type"] = backgroundTypeToString(skin.messageWindow.namePlate.type);
			np["color"] = colorToHex(skin.messageWindow.namePlate.backgroundColor);
			np["textColor"] = colorToHex(skin.messageWindow.namePlate.textColor);
			j["nameplate"] = np;
		}

		// fonts
		{
			nlohmann::json fn;
			if (!skin.fonts.mainFont.empty()) { fn["main"] = skin.fonts.mainFont; }
			fn["mainSize"] = skin.fonts.mainSize;
			fn["nameSize"] = skin.fonts.nameSize;
			fn["choiceSize"] = skin.fonts.choiceSize;
			fn["systemSize"] = skin.fonts.systemSize;
			j["fonts"] = fn;
		}

		// textColors
		{
			nlohmann::json tc;
			tc["main"] = colorToHex(skin.textColors.main);
			tc["shadow"] = colorToHex(skin.textColors.shadow);
			tc["name"] = colorToHex(skin.textColors.name);
			j["textColors"] = tc;
		}

		return j.dump(2);
	}

private:
	// ── nlohmann::json ベースのパーサー ──────────────────────

	static void parseJson(std::string_view json, UISkin& skin)
	{
		nlohmann::json j;
		try
		{
			j = nlohmann::json::parse(json);
		}
		catch (...)
		{
			return; // パース失敗時はデフォルトスキンを維持
		}

		if (!j.is_object()) { return; }

		// name
		if (j.contains("name") && j["name"].is_string())
		{
			skin.name = j["name"].get<std::string>();
		}

		// messageWindow セクション
		if (j.contains("messageWindow") && j["messageWindow"].is_object())
		{
			const auto& mw = j["messageWindow"];

			if (mw.contains("type") && mw["type"].is_string())
			{
				skin.messageWindow.backgroundType = backgroundTypeFromString(mw["type"].get<std::string>());
			}
			if (mw.contains("image") && mw["image"].is_string())
			{
				skin.messageWindow.backgroundImage = mw["image"].get<std::string>();
			}
			if (mw.contains("cornerSize") && mw["cornerSize"].is_number())
			{
				skin.messageWindow.cornerSize = mw["cornerSize"].get<float>();
			}
			if (mw.contains("alpha") && mw["alpha"].is_number())
			{
				skin.messageWindow.alpha = mw["alpha"].get<float>();
			}
			if (mw.contains("backgroundColor") && mw["backgroundColor"].is_string())
			{
				skin.messageWindow.backgroundColor = colorFromHex(mw["backgroundColor"].get<std::string>());
			}
			if (mw.contains("padding") && mw["padding"].is_object())
			{
				parsePadding(mw["padding"], skin.messageWindow.padding);
			}
		}

		// nameplate セクション
		if (j.contains("nameplate") && j["nameplate"].is_object())
		{
			const auto& np = j["nameplate"];

			if (np.contains("type") && np["type"].is_string())
			{
				skin.messageWindow.namePlate.type = backgroundTypeFromString(np["type"].get<std::string>());
			}
			if (np.contains("color") && np["color"].is_string())
			{
				skin.messageWindow.namePlate.backgroundColor = colorFromHex(np["color"].get<std::string>());
			}
			if (np.contains("textColor") && np["textColor"].is_string())
			{
				skin.messageWindow.namePlate.textColor = colorFromHex(np["textColor"].get<std::string>());
			}
			if (np.contains("image") && np["image"].is_string())
			{
				skin.messageWindow.namePlate.image = np["image"].get<std::string>();
			}
		}

		// fonts セクション
		if (j.contains("fonts") && j["fonts"].is_object())
		{
			const auto& fn = j["fonts"];

			if (fn.contains("main") && fn["main"].is_string())
			{
				skin.fonts.mainFont = fn["main"].get<std::string>();
			}
			if (fn.contains("name") && fn["name"].is_string())
			{
				skin.fonts.nameFont = fn["name"].get<std::string>();
			}
			if (fn.contains("choice") && fn["choice"].is_string())
			{
				skin.fonts.choiceFont = fn["choice"].get<std::string>();
			}
			if (fn.contains("system") && fn["system"].is_string())
			{
				skin.fonts.systemFont = fn["system"].get<std::string>();
			}
			if (fn.contains("mainSize") && fn["mainSize"].is_number())
			{
				skin.fonts.mainSize = fn["mainSize"].get<float>();
			}
			if (fn.contains("nameSize") && fn["nameSize"].is_number())
			{
				skin.fonts.nameSize = fn["nameSize"].get<float>();
			}
			if (fn.contains("choiceSize") && fn["choiceSize"].is_number())
			{
				skin.fonts.choiceSize = fn["choiceSize"].get<float>();
			}
			if (fn.contains("systemSize") && fn["systemSize"].is_number())
			{
				skin.fonts.systemSize = fn["systemSize"].get<float>();
			}
		}

		// textColors セクション
		if (j.contains("textColors") && j["textColors"].is_object())
		{
			const auto& tc = j["textColors"];

			if (tc.contains("main") && tc["main"].is_string())
			{
				skin.textColors.main = colorFromHex(tc["main"].get<std::string>());
			}
			if (tc.contains("shadow") && tc["shadow"].is_string())
			{
				skin.textColors.shadow = colorFromHex(tc["shadow"].get<std::string>());
			}
			if (tc.contains("outline") && tc["outline"].is_string())
			{
				skin.textColors.outline = colorFromHex(tc["outline"].get<std::string>());
			}
			if (tc.contains("name") && tc["name"].is_string())
			{
				skin.textColors.name = colorFromHex(tc["name"].get<std::string>());
			}
			if (tc.contains("choice") && tc["choice"].is_string())
			{
				skin.textColors.choice = colorFromHex(tc["choice"].get<std::string>());
			}
			if (tc.contains("system") && tc["system"].is_string())
			{
				skin.textColors.system = colorFromHex(tc["system"].get<std::string>());
			}
		}

		// choiceButton セクション
		if (j.contains("choiceButton") && j["choiceButton"].is_object())
		{
			const auto& cb = j["choiceButton"];

			if (cb.contains("height") && cb["height"].is_number())
			{
				skin.choiceButton.height = cb["height"].get<float>();
			}
			if (cb.contains("spacing") && cb["spacing"].is_number())
			{
				skin.choiceButton.spacing = cb["spacing"].get<float>();
			}

			parseElementState(cb, "normal", skin.choiceButton.normal);
			parseElementState(cb, "hover", skin.choiceButton.hover);
			parseElementState(cb, "selected", skin.choiceButton.selected);
			parseElementState(cb, "disabled", skin.choiceButton.disabled);
		}

		// configSlider セクション
		if (j.contains("configSlider") && j["configSlider"].is_object())
		{
			const auto& sl = j["configSlider"];

			if (sl.contains("trackColor") && sl["trackColor"].is_string())
			{
				skin.configSlider.trackColor = colorFromHex(sl["trackColor"].get<std::string>());
			}
			if (sl.contains("fillColor") && sl["fillColor"].is_string())
			{
				skin.configSlider.fillColor = colorFromHex(sl["fillColor"].get<std::string>());
			}
			if (sl.contains("handleColor") && sl["handleColor"].is_string())
			{
				skin.configSlider.handleColor = colorFromHex(sl["handleColor"].get<std::string>());
			}
			if (sl.contains("trackHeight") && sl["trackHeight"].is_number())
			{
				skin.configSlider.trackHeight = sl["trackHeight"].get<float>();
			}
			if (sl.contains("handleSize") && sl["handleSize"].is_number())
			{
				skin.configSlider.handleSize = sl["handleSize"].get<float>();
			}
		}
	}

	static void parseElementState(const nlohmann::json& parent, const std::string& key, ElementState& state)
	{
		if (!parent.contains(key) || !parent[key].is_object()) { return; }
		const auto& section = parent[key];

		if (section.contains("backgroundColor") && section["backgroundColor"].is_string())
		{
			state.backgroundColor = colorFromHex(section["backgroundColor"].get<std::string>());
		}
		if (section.contains("textColor") && section["textColor"].is_string())
		{
			state.textColor = colorFromHex(section["textColor"].get<std::string>());
		}
		if (section.contains("borderColor") && section["borderColor"].is_string())
		{
			state.borderColor = colorFromHex(section["borderColor"].get<std::string>());
		}
		if (section.contains("borderWidth") && section["borderWidth"].is_number())
		{
			state.borderWidth = section["borderWidth"].get<float>();
		}
		if (section.contains("backgroundImage") && section["backgroundImage"].is_string())
		{
			state.backgroundImage = section["backgroundImage"].get<std::string>();
		}
	}

	static void parsePadding(const nlohmann::json& j, Padding& padding)
	{
		if (j.contains("top") && j["top"].is_number()) { padding.top = j["top"].get<float>(); }
		if (j.contains("bottom") && j["bottom"].is_number()) { padding.bottom = j["bottom"].get<float>(); }
		if (j.contains("left") && j["left"].is_number()) { padding.left = j["left"].get<float>(); }
		if (j.contains("right") && j["right"].is_number()) { padding.right = j["right"].get<float>(); }
	}

	// ── 文字列変換ヘルパー ────────────────────────────────────

	[[nodiscard]] static WindowBackgroundType backgroundTypeFromString(std::string_view s) noexcept
	{
		if (s == "nineSlice" || s == "nine_slice") { return WindowBackgroundType::NineSlice; }
		if (s == "image") { return WindowBackgroundType::Image; }
		return WindowBackgroundType::Solid;
	}

	[[nodiscard]] static std::string backgroundTypeToString(WindowBackgroundType type)
	{
		switch (type)
		{
		case WindowBackgroundType::NineSlice: return "nineSlice";
		case WindowBackgroundType::Image:     return "image";
		case WindowBackgroundType::Solid:
		default:                              return "solid";
		}
	}

	[[nodiscard]] static std::string colorToHex(const sgc::Colorf& c)
	{
		auto toHex = [](float v) -> std::string {
			int val = std::clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255);
			char buf[3];
			std::snprintf(buf, sizeof(buf), "%02X", val);
			return std::string(buf, 2);
		};
		return "#" + toHex(c.r) + toHex(c.g) + toHex(c.b);
	}

	[[nodiscard]] static nlohmann::json paddingToJsonObj(const Padding& p)
	{
		return nlohmann::json{
			{"top", p.top},
			{"bottom", p.bottom},
			{"left", p.left},
			{"right", p.right}
		};
	}
};

} // namespace mitiru::vn
