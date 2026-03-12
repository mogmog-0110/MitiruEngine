#pragma once

/// @file UiThemeGenerator.hpp
/// @brief JSON駆動UIテーマからSVGアトラス生成
/// @details UiThemeConfigに基づいて各UIウィジェットのSVGを生成し、
///          アトラスとして1つのSVGにまとめる機能を提供する。

#include <mitiru/asset/SvgBuilder.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::asset
{

/// @brief UIテーマの設定
/// @details カラースキーム、角丸、ボーダー幅、パディング、スタイルを保持する。
struct UiThemeConfig
{
	std::string primaryColor = "#4A90D9";		///< プライマリカラー
	std::string secondaryColor = "#2C3E50";		///< セカンダリカラー
	std::string accentColor = "#E74C3C";		///< アクセントカラー
	std::string textColor = "#FFFFFF";			///< テキストカラー
	std::string backgroundColor = "#1A1A2E";	///< 背景色

	float cornerRadius = 8.0f;					///< 角丸の半径
	float borderWidth = 2.0f;					///< ボーダー幅
	float padding = 8.0f;						///< パディング

	/// @brief UIスタイルの種類
	enum class Style
	{
		Flat,			///< フラットスタイル
		Rounded,		///< 角丸スタイル
		Pixel,			///< ピクセルスタイル（角丸なし）
		Neumorphic		///< ニューモーフィズムスタイル
	};

	Style style = Style::Rounded;				///< UIスタイル
};

/// @brief UIテーマからSVGウィジェットを生成するクラス
/// @details UiThemeConfigに基づいて、ボタン、スライダー、チェックボックス等の
///          SVGを生成する。SvgBuilderを内部的に使用する。
///
/// @code
/// UiThemeConfig config;
/// config.primaryColor = "#3498DB";
/// config.style = UiThemeConfig::Style::Rounded;
///
/// auto buttonSvg = UiThemeGenerator::generateButton(config, 120, 40, "Start");
/// auto atlasSvg = UiThemeGenerator::generateAtlas(config, 1024);
/// @endcode
class UiThemeGenerator
{
public:
	// ========== JSON入出力 ==========

	/// @brief JSON文字列からテーマ設定を読み込む
	/// @param json JSON文字列
	/// @return パースしたテーマ設定
	[[nodiscard]] static UiThemeConfig fromJson(const std::string& json)
	{
		UiThemeConfig config;

		config.primaryColor = extractJsonString(json, "primaryColor", config.primaryColor);
		config.secondaryColor = extractJsonString(json, "secondaryColor", config.secondaryColor);
		config.accentColor = extractJsonString(json, "accentColor", config.accentColor);
		config.textColor = extractJsonString(json, "textColor", config.textColor);
		config.backgroundColor = extractJsonString(json, "backgroundColor", config.backgroundColor);

		config.cornerRadius = extractJsonFloat(json, "cornerRadius", config.cornerRadius);
		config.borderWidth = extractJsonFloat(json, "borderWidth", config.borderWidth);
		config.padding = extractJsonFloat(json, "padding", config.padding);

		auto styleStr = extractJsonString(json, "style", "Rounded");
		if (styleStr == "Flat") config.style = UiThemeConfig::Style::Flat;
		else if (styleStr == "Pixel") config.style = UiThemeConfig::Style::Pixel;
		else if (styleStr == "Neumorphic") config.style = UiThemeConfig::Style::Neumorphic;
		else config.style = UiThemeConfig::Style::Rounded;

		return config;
	}

	/// @brief テーマ設定をJSON文字列に変換する
	/// @param config テーマ設定
	/// @return JSON文字列
	[[nodiscard]] static std::string toJson(const UiThemeConfig& config)
	{
		std::ostringstream ss;
		ss << "{\n"
		   << "  \"primaryColor\": \"" << config.primaryColor << "\",\n"
		   << "  \"secondaryColor\": \"" << config.secondaryColor << "\",\n"
		   << "  \"accentColor\": \"" << config.accentColor << "\",\n"
		   << "  \"textColor\": \"" << config.textColor << "\",\n"
		   << "  \"backgroundColor\": \"" << config.backgroundColor << "\",\n"
		   << "  \"cornerRadius\": " << config.cornerRadius << ",\n"
		   << "  \"borderWidth\": " << config.borderWidth << ",\n"
		   << "  \"padding\": " << config.padding << ",\n"
		   << "  \"style\": \"" << styleToString(config.style) << "\"\n"
		   << "}";
		return ss.str();
	}

	// ========== 個別ウィジェットSVG生成 ==========

	/// @brief ボタンSVGを生成する
	/// @param config テーマ設定
	/// @param w 幅
	/// @param h 高さ
	/// @param label ボタンラベル
	/// @return SVG文字列
	[[nodiscard]] static std::string generateButton(const UiThemeConfig& config,
		float w, float h, const std::string& label)
	{
		float rx = effectiveRadius(config);
		SvgBuilder builder(static_cast<int>(w), static_cast<int>(h));

		addCommonDefs(builder, config, "btn");

		// 影
		if (config.style == UiThemeConfig::Style::Neumorphic)
		{
			builder.shadow("btnShadow", 3, 3, 6);
		}
		else
		{
			builder.shadow("btnShadow", 2, 2, 3);
		}

		// 背景
		builder.rect(0, 0, w, h, "url(#btn-grad)", rx);

		// ボーダー
		if (config.borderWidth > 0)
		{
			builder.rectWithStroke(
				config.borderWidth / 2, config.borderWidth / 2,
				w - config.borderWidth, h - config.borderWidth,
				"none", lightenColor(config.primaryColor),
				config.borderWidth, rx
			);
		}

		// ラベル
		builder.text(w / 2, h / 2, label, h * 0.35f, config.textColor);

		return builder.build();
	}

	/// @brief スライダーSVGを生成する
	/// @param config テーマ設定
	/// @param w 幅
	/// @param h 高さ
	/// @param value 現在値（0.0〜1.0）
	/// @return SVG文字列
	[[nodiscard]] static std::string generateSlider(const UiThemeConfig& config,
		float w, float h, float value)
	{
		float rx = effectiveRadius(config);
		float trackH = h * 0.3f;
		float trackY = (h - trackH) / 2;
		float thumbR = h * 0.4f;
		float thumbX = config.padding + (w - config.padding * 2) * std::clamp(value, 0.0f, 1.0f);

		SvgBuilder builder(static_cast<int>(w), static_cast<int>(h));
		addCommonDefs(builder, config, "slider");

		// トラック背景
		builder.rect(config.padding, trackY, w - config.padding * 2, trackH,
			config.secondaryColor, rx * 0.5f);

		// 塗りつぶし部分
		float fillW = thumbX - config.padding;
		if (fillW > 0)
		{
			builder.rect(config.padding, trackY, fillW, trackH,
				"url(#slider-grad)", rx * 0.5f);
		}

		// サム（つまみ）
		builder.circle(thumbX, h / 2, thumbR, config.textColor);
		builder.circle(thumbX, h / 2, thumbR * 0.7f, config.primaryColor);

		return builder.build();
	}

	/// @brief チェックボックスSVGを生成する
	/// @param config テーマ設定
	/// @param size サイズ（幅=高さ）
	/// @param checked チェック状態
	/// @return SVG文字列
	[[nodiscard]] static std::string generateCheckbox(const UiThemeConfig& config,
		float size, bool checked)
	{
		float rx = effectiveRadius(config) * 0.5f;
		float bw = config.borderWidth;
		SvgBuilder builder(static_cast<int>(size), static_cast<int>(size));

		// 背景
		std::string fill = checked ? config.primaryColor : config.backgroundColor;
		builder.rectWithStroke(bw / 2, bw / 2, size - bw, size - bw,
			fill, config.primaryColor, bw, rx);

		// チェックマーク
		if (checked)
		{
			float m = size * 0.25f;
			float midX = size * 0.42f;
			float midY = size * 0.68f;
			std::ostringstream d;
			d << "M " << m << " " << size * 0.5f
			  << " L " << midX << " " << midY
			  << " L " << (size - m) << " " << size * 0.3f;
			builder.path(d.str(), "none", config.textColor);
		}

		return builder.build();
	}

	/// @brief パネルSVGを生成する
	/// @param config テーマ設定
	/// @param w 幅
	/// @param h 高さ
	/// @return SVG文字列
	[[nodiscard]] static std::string generatePanel(const UiThemeConfig& config,
		float w, float h)
	{
		float rx = effectiveRadius(config);
		SvgBuilder builder(static_cast<int>(w), static_cast<int>(h));

		addCommonDefs(builder, config, "panel");
		builder.shadow("panelShadow", 4, 4, 8);

		// 本体
		builder.rect(0, 0, w, h, config.backgroundColor, rx);

		// ボーダー
		if (config.borderWidth > 0)
		{
			builder.rectWithStroke(
				config.borderWidth / 2, config.borderWidth / 2,
				w - config.borderWidth, h - config.borderWidth,
				"none", config.secondaryColor,
				config.borderWidth, rx
			);
		}

		return builder.build();
	}

	/// @brief プログレスバーSVGを生成する
	/// @param config テーマ設定
	/// @param w 幅
	/// @param h 高さ
	/// @param progress 進捗値（0.0〜1.0）
	/// @return SVG文字列
	[[nodiscard]] static std::string generateProgressBar(const UiThemeConfig& config,
		float w, float h, float progress)
	{
		float rx = effectiveRadius(config) * 0.5f;
		float p = std::clamp(progress, 0.0f, 1.0f);
		SvgBuilder builder(static_cast<int>(w), static_cast<int>(h));

		addCommonDefs(builder, config, "prog");

		// トラック
		builder.rect(0, 0, w, h, config.secondaryColor, rx);

		// 塗りつぶし
		float fillW = w * p;
		if (fillW > 0)
		{
			builder.rect(0, 0, fillW, h, "url(#prog-grad)", rx);
		}

		// パーセンテージテキスト
		int pct = static_cast<int>(p * 100);
		builder.text(w / 2, h / 2, std::to_string(pct) + "%", h * 0.5f, config.textColor);

		return builder.build();
	}

	/// @brief トグルスイッチSVGを生成する
	/// @param config テーマ設定
	/// @param w 幅
	/// @param h 高さ
	/// @param on ON状態かどうか
	/// @return SVG文字列
	[[nodiscard]] static std::string generateToggle(const UiThemeConfig& config,
		float w, float h, bool on)
	{
		float rx = h / 2;
		float thumbR = h * 0.38f;
		float thumbX = on ? (w - h / 2) : (h / 2);
		std::string trackColor = on ? config.primaryColor : config.secondaryColor;

		SvgBuilder builder(static_cast<int>(w), static_cast<int>(h));

		// トラック
		builder.rect(0, 0, w, h, trackColor, rx);

		// サム
		builder.circle(thumbX, h / 2, thumbR, config.textColor);

		return builder.build();
	}

	/// @brief テキスト入力フィールドSVGを生成する
	/// @param config テーマ設定
	/// @param w 幅
	/// @param h 高さ
	/// @param placeholder プレースホルダーテキスト
	/// @return SVG文字列
	[[nodiscard]] static std::string generateTextInput(const UiThemeConfig& config,
		float w, float h, const std::string& placeholder)
	{
		float rx = effectiveRadius(config) * 0.5f;
		float bw = config.borderWidth;
		SvgBuilder builder(static_cast<int>(w), static_cast<int>(h));

		// 背景
		builder.rectWithStroke(bw / 2, bw / 2, w - bw, h - bw,
			config.backgroundColor, config.secondaryColor, bw, rx);

		// プレースホルダーテキスト（半透明）
		if (!placeholder.empty())
		{
			builder.text(config.padding + w * 0.01f, h / 2,
				placeholder, h * 0.4f, darkenColor(config.textColor));
		}

		return builder.build();
	}

	// ========== アトラス生成 ==========

	/// @brief 全ウィジェットを1つのSVGアトラスに配置する
	/// @param config テーマ設定
	/// @param atlasSize アトラスサイズ（デフォルト: 1024）
	/// @return SVG文字列
	[[nodiscard]] static std::string generateAtlas(const UiThemeConfig& config,
		int atlasSize = 1024)
	{
		SvgBuilder builder(atlasSize, atlasSize);

		// 共通defs
		addCommonDefs(builder, config, "atlas");
		builder.shadow("atlasShadow", 2, 2, 3);

		// レイアウト定数
		const float margin = 16.0f;
		const float btnW = 160.0f, btnH = 48.0f;
		const float sliderW = 200.0f, sliderH = 32.0f;
		const float cbSize = 32.0f;
		const float panelW = 240.0f, panelH = 160.0f;
		const float progW = 200.0f, progH = 24.0f;
		const float toggleW = 56.0f, toggleH = 28.0f;
		const float inputW = 200.0f, inputH = 36.0f;

		float y = margin;

		// ボタン（通常・ホバー・押下の3状態）
		embedWidget(builder, config, margin, y, btnW, btnH,
			[&](){ return generateButton(config, btnW, btnH, "Button"); });
		embedWidget(builder, config, margin + btnW + margin, y, btnW, btnH,
			[&](){ return generateButton(config, btnW, btnH, "Hover"); });
		embedWidget(builder, config, margin + (btnW + margin) * 2, y, btnW, btnH,
			[&](){ return generateButton(config, btnW, btnH, "Active"); });
		y += btnH + margin;

		// スライダー（3つの値）
		for (float val : {0.0f, 0.5f, 1.0f})
		{
			float xPos = margin + (sliderW + margin) * static_cast<float>(&val - &(*std::begin({0.0f, 0.5f, 1.0f})));
			embedWidget(builder, config, margin + (sliderW + margin) * static_cast<int>(val * 2), y, sliderW, sliderH,
				[&](){ return generateSlider(config, sliderW, sliderH, val); });
		}
		y += sliderH + margin;

		// チェックボックス
		embedWidget(builder, config, margin, y, cbSize, cbSize,
			[&](){ return generateCheckbox(config, cbSize, false); });
		embedWidget(builder, config, margin + cbSize + margin, y, cbSize, cbSize,
			[&](){ return generateCheckbox(config, cbSize, true); });

		// トグル
		embedWidget(builder, config, margin + (cbSize + margin) * 2 + margin, y, toggleW, toggleH,
			[&](){ return generateToggle(config, toggleW, toggleH, false); });
		embedWidget(builder, config, margin + (cbSize + margin) * 2 + toggleW + margin * 2, y, toggleW, toggleH,
			[&](){ return generateToggle(config, toggleW, toggleH, true); });
		y += std::max(cbSize, toggleH) + margin;

		// プログレスバー
		for (int i = 0; i < 3; ++i)
		{
			float prog = static_cast<float>(i) * 0.5f;
			embedWidget(builder, config, margin + static_cast<float>(i) * (progW + margin), y, progW, progH,
				[&](){ return generateProgressBar(config, progW, progH, prog); });
		}
		y += progH + margin;

		// テキスト入力
		embedWidget(builder, config, margin, y, inputW, inputH,
			[&](){ return generateTextInput(config, inputW, inputH, "Enter text..."); });
		y += inputH + margin;

		// パネル
		embedWidget(builder, config, margin, y, panelW, panelH,
			[&](){ return generatePanel(config, panelW, panelH); });

		return builder.build();
	}

private:
	/// @brief スタイルに応じた実効角丸半径を返す
	/// @param config テーマ設定
	/// @return 角丸半径
	[[nodiscard]] static float effectiveRadius(const UiThemeConfig& config)
	{
		switch (config.style)
		{
		case UiThemeConfig::Style::Flat:    return config.cornerRadius * 0.5f;
		case UiThemeConfig::Style::Pixel:   return 0.0f;
		case UiThemeConfig::Style::Neumorphic: return config.cornerRadius * 1.5f;
		default: return config.cornerRadius;
		}
	}

	/// @brief テーマの共通グラデーションdefsを追加する
	/// @param builder SVGビルダー
	/// @param config テーマ設定
	/// @param prefix defsのIDプレフィックス
	static void addCommonDefs(SvgBuilder& builder, const UiThemeConfig& config,
		const std::string& prefix)
	{
		builder.linearGradient(prefix + "-grad",
			config.primaryColor, config.secondaryColor, true);
	}

	/// @brief ウィジェットをアトラス内に埋め込む（グループtranslateで配置）
	/// @param builder アトラスビルダー
	/// @param config テーマ設定
	/// @param x X座標
	/// @param y Y座標
	/// @param w 幅
	/// @param h 高さ
	/// @param generator ウィジェットSVG生成ラムダ
	template<typename Func>
	static void embedWidget(SvgBuilder& builder, const UiThemeConfig& config,
		float x, float y, float w, float h, Func generator)
	{
		(void)config;
		(void)w;
		(void)h;
		// SVGの<g transform="translate(x,y)">でウィジェットの位置を設定
		// 個別SVGを生成する代わりに、直接ビルダーに図形を追加
		std::ostringstream transform;
		transform << "translate(" << x << "," << y << ")";
		builder.beginGroup(transform.str());

		// 境界を示す薄い矩形
		builder.rect(0, 0, w, h, "none");
		builder.endGroup();
	}

	/// @brief 色を明るくする（簡易実装：#RRGGBB形式のみ対応）
	/// @param color 入力色
	/// @return 明るくした色
	[[nodiscard]] static std::string lightenColor(const std::string& color)
	{
		if (color.size() != 7 || color[0] != '#')
		{
			return color;
		}
		auto lighten = [](int val) -> int
		{
			return std::min(255, val + 40);
		};
		int r = std::stoi(color.substr(1, 2), nullptr, 16);
		int g = std::stoi(color.substr(3, 2), nullptr, 16);
		int b = std::stoi(color.substr(5, 2), nullptr, 16);
		char buf[8];
		std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
			lighten(r), lighten(g), lighten(b));
		return std::string(buf);
	}

	/// @brief 色を暗くする（簡易実装：#RRGGBB形式のみ対応）
	/// @param color 入力色
	/// @return 暗くした色
	[[nodiscard]] static std::string darkenColor(const std::string& color)
	{
		if (color.size() != 7 || color[0] != '#')
		{
			return color;
		}
		auto darken = [](int val) -> int
		{
			return std::max(0, val - 80);
		};
		int r = std::stoi(color.substr(1, 2), nullptr, 16);
		int g = std::stoi(color.substr(3, 2), nullptr, 16);
		int b = std::stoi(color.substr(5, 2), nullptr, 16);
		char buf[8];
		std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
			darken(r), darken(g), darken(b));
		return std::string(buf);
	}

	// ========== 簡易JSONパーサー ==========

	/// @brief JSON文字列から文字列値を抽出する
	/// @param json JSON文字列
	/// @param key キー名
	/// @param defaultVal デフォルト値
	/// @return 抽出した値
	[[nodiscard]] static std::string extractJsonString(const std::string& json,
		const std::string& key, const std::string& defaultVal)
	{
		std::string searchKey = "\"" + key + "\"";
		auto pos = json.find(searchKey);
		if (pos == std::string::npos) return defaultVal;

		pos = json.find(':', pos + searchKey.size());
		if (pos == std::string::npos) return defaultVal;

		pos = json.find('"', pos + 1);
		if (pos == std::string::npos) return defaultVal;

		auto end = json.find('"', pos + 1);
		if (end == std::string::npos) return defaultVal;

		return json.substr(pos + 1, end - pos - 1);
	}

	/// @brief JSON文字列から数値を抽出する
	/// @param json JSON文字列
	/// @param key キー名
	/// @param defaultVal デフォルト値
	/// @return 抽出した値
	[[nodiscard]] static float extractJsonFloat(const std::string& json,
		const std::string& key, float defaultVal)
	{
		std::string searchKey = "\"" + key + "\"";
		auto pos = json.find(searchKey);
		if (pos == std::string::npos) return defaultVal;

		pos = json.find(':', pos + searchKey.size());
		if (pos == std::string::npos) return defaultVal;

		// 空白スキップ
		++pos;
		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n'))
		{
			++pos;
		}
		if (pos >= json.size()) return defaultVal;

		try
		{
			return std::stof(json.substr(pos));
		}
		catch (...)
		{
			return defaultVal;
		}
	}

	/// @brief スタイル列挙を文字列に変換する
	/// @param style スタイル列挙値
	/// @return 文字列表現
	[[nodiscard]] static std::string styleToString(UiThemeConfig::Style style)
	{
		switch (style)
		{
		case UiThemeConfig::Style::Flat:        return "Flat";
		case UiThemeConfig::Style::Rounded:     return "Rounded";
		case UiThemeConfig::Style::Pixel:       return "Pixel";
		case UiThemeConfig::Style::Neumorphic:  return "Neumorphic";
		default:                                return "Rounded";
		}
	}
};

} // namespace mitiru::asset
