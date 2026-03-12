#pragma once

/// @file SvgBuilder.hpp
/// @brief プログラム的なSVG生成ビルダー
/// @details メソッドチェーンで基本図形・グラデーション・フィルター等を
///          組み立てて、有効なSVG 1.1 XML文字列を出力する。

#include <sstream>
#include <string>
#include <vector>

namespace mitiru::asset
{

/// @brief SVGをプログラム的に構築するビルダークラス
/// @details 基本図形、テキスト、パス、グラデーション、フィルターを
///          メソッドチェーンで追加し、build()で完全なSVG XML文字列を得る。
///
/// @code
/// auto svg = SvgBuilder(200, 100)
///     .linearGradient("bg", "#4A90D9", "#2C3E50")
///     .rect(0, 0, 200, 100, "url(#bg)", 8)
///     .text(100, 55, "Hello", 16, "white")
///     .build();
/// @endcode
class SvgBuilder
{
public:
	/// @brief コンストラクタ
	/// @param width SVGの幅（ピクセル）
	/// @param height SVGの高さ（ピクセル）
	SvgBuilder(int width, int height)
		: m_width(width)
		, m_height(height)
	{
	}

	// ========== 基本図形 ==========

	/// @brief 矩形を追加する
	/// @param x X座標
	/// @param y Y座標
	/// @param w 幅
	/// @param h 高さ
	/// @param fill 塗りつぶし色（CSS色値またはurl(#id)）
	/// @param rx 角丸の半径（デフォルト: 0）
	/// @return 自身の参照（メソッドチェーン用）
	SvgBuilder& rect(float x, float y, float w, float h, const std::string& fill, float rx = 0)
	{
		std::ostringstream ss;
		ss << "<rect x=\"" << x << "\" y=\"" << y
		   << "\" width=\"" << w << "\" height=\"" << h
		   << "\" fill=\"" << fill << "\"";
		if (rx > 0)
		{
			ss << " rx=\"" << rx << "\" ry=\"" << rx << "\"";
		}
		ss << "/>";
		m_elements.push_back(ss.str());
		return *this;
	}

	/// @brief 矩形を追加する（ストローク付き）
	/// @param x X座標
	/// @param y Y座標
	/// @param w 幅
	/// @param h 高さ
	/// @param fill 塗りつぶし色
	/// @param stroke ストローク色
	/// @param strokeWidth ストローク幅
	/// @param rx 角丸の半径
	/// @return 自身の参照
	SvgBuilder& rectWithStroke(float x, float y, float w, float h,
		const std::string& fill, const std::string& stroke,
		float strokeWidth = 1, float rx = 0)
	{
		std::ostringstream ss;
		ss << "<rect x=\"" << x << "\" y=\"" << y
		   << "\" width=\"" << w << "\" height=\"" << h
		   << "\" fill=\"" << fill << "\""
		   << " stroke=\"" << stroke << "\""
		   << " stroke-width=\"" << strokeWidth << "\"";
		if (rx > 0)
		{
			ss << " rx=\"" << rx << "\" ry=\"" << rx << "\"";
		}
		ss << "/>";
		m_elements.push_back(ss.str());
		return *this;
	}

	/// @brief 円を追加する
	/// @param cx 中心X座標
	/// @param cy 中心Y座標
	/// @param r 半径
	/// @param fill 塗りつぶし色
	/// @return 自身の参照
	SvgBuilder& circle(float cx, float cy, float r, const std::string& fill)
	{
		std::ostringstream ss;
		ss << "<circle cx=\"" << cx << "\" cy=\"" << cy
		   << "\" r=\"" << r << "\" fill=\"" << fill << "\"/>";
		m_elements.push_back(ss.str());
		return *this;
	}

	/// @brief 線を追加する
	/// @param x1 始点X
	/// @param y1 始点Y
	/// @param x2 終点X
	/// @param y2 終点Y
	/// @param stroke ストローク色
	/// @param strokeWidth ストローク幅（デフォルト: 1）
	/// @return 自身の参照
	SvgBuilder& line(float x1, float y1, float x2, float y2,
		const std::string& stroke, float strokeWidth = 1)
	{
		std::ostringstream ss;
		ss << "<line x1=\"" << x1 << "\" y1=\"" << y1
		   << "\" x2=\"" << x2 << "\" y2=\"" << y2
		   << "\" stroke=\"" << stroke
		   << "\" stroke-width=\"" << strokeWidth << "\"/>";
		m_elements.push_back(ss.str());
		return *this;
	}

	/// @brief テキストを追加する
	/// @param x X座標
	/// @param y Y座標
	/// @param content テキスト内容
	/// @param fontSize フォントサイズ
	/// @param fill 文字色（デフォルト: "white"）
	/// @return 自身の参照
	SvgBuilder& text(float x, float y, const std::string& content,
		float fontSize, const std::string& fill = "white")
	{
		std::ostringstream ss;
		ss << "<text x=\"" << x << "\" y=\"" << y
		   << "\" font-size=\"" << fontSize
		   << "\" fill=\"" << fill
		   << "\" text-anchor=\"middle\""
		   << " dominant-baseline=\"central\""
		   << " font-family=\"sans-serif\">"
		   << escapeXml(content)
		   << "</text>";
		m_elements.push_back(ss.str());
		return *this;
	}

	/// @brief SVGパスを追加する
	/// @param d パスデータ文字列
	/// @param fill 塗りつぶし色
	/// @param stroke ストローク色（デフォルト: "none"）
	/// @return 自身の参照
	SvgBuilder& path(const std::string& d, const std::string& fill,
		const std::string& stroke = "none")
	{
		std::ostringstream ss;
		ss << "<path d=\"" << d << "\" fill=\"" << fill
		   << "\" stroke=\"" << stroke << "\"/>";
		m_elements.push_back(ss.str());
		return *this;
	}

	// ========== グループ ==========

	/// @brief グループ開始タグを追加する
	/// @param transform SVG transform属性（デフォルト: ""）
	/// @return 自身の参照
	SvgBuilder& beginGroup(const std::string& transform = "")
	{
		std::ostringstream ss;
		ss << "<g";
		if (!transform.empty())
		{
			ss << " transform=\"" << transform << "\"";
		}
		ss << ">";
		m_elements.push_back(ss.str());
		return *this;
	}

	/// @brief グループ終了タグを追加する
	/// @return 自身の参照
	SvgBuilder& endGroup()
	{
		m_elements.push_back("</g>");
		return *this;
	}

	// ========== フィルタータグ付き要素 ==========

	/// @brief 要素にフィルター属性を適用する（次に追加する要素用にフィルターIDを記録）
	/// @param filterId フィルターID
	/// @return 自身の参照
	SvgBuilder& applyFilter(const std::string& filterId)
	{
		m_pendingFilter = filterId;
		return *this;
	}

	// ========== スタイル（defs内） ==========

	/// @brief 線形グラデーションを定義する
	/// @param id グラデーションID
	/// @param color1 開始色
	/// @param color2 終了色
	/// @param vertical 垂直方向かどうか（デフォルト: false = 水平）
	/// @return 自身の参照
	SvgBuilder& linearGradient(const std::string& id,
		const std::string& color1, const std::string& color2,
		bool vertical = false)
	{
		std::ostringstream ss;
		ss << "<linearGradient id=\"" << id << "\"";
		if (vertical)
		{
			ss << " x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"";
		}
		else
		{
			ss << " x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\"";
		}
		ss << ">"
		   << "<stop offset=\"0%\" stop-color=\"" << color1 << "\"/>"
		   << "<stop offset=\"100%\" stop-color=\"" << color2 << "\"/>"
		   << "</linearGradient>";
		m_defs.push_back(ss.str());
		return *this;
	}

	/// @brief ドロップシャドウフィルターを定義する
	/// @param filterId フィルターID
	/// @param dx X方向のオフセット（デフォルト: 2）
	/// @param dy Y方向のオフセット（デフォルト: 2）
	/// @param blur ぼかし量（デフォルト: 4）
	/// @return 自身の参照
	SvgBuilder& shadow(const std::string& filterId, float dx = 2, float dy = 2, float blur = 4)
	{
		std::ostringstream ss;
		ss << "<filter id=\"" << filterId << "\" x=\"-20%\" y=\"-20%\" width=\"140%\" height=\"140%\">"
		   << "<feDropShadow dx=\"" << dx << "\" dy=\"" << dy
		   << "\" stdDeviation=\"" << blur
		   << "\" flood-color=\"rgba(0,0,0,0.5)\"/>"
		   << "</filter>";
		m_defs.push_back(ss.str());
		return *this;
	}

	// ========== 出力 ==========

	/// @brief 完全なSVG XML文字列を構築して返す
	/// @return SVG 1.1準拠のXML文字列
	[[nodiscard]] std::string build() const
	{
		std::ostringstream ss;
		ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		   << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
		   << "width=\"" << m_width << "\" height=\"" << m_height
		   << "\" viewBox=\"0 0 " << m_width << " " << m_height << "\">\n";

		// defs セクション
		if (!m_defs.empty())
		{
			ss << "<defs>\n";
			for (const auto& def : m_defs)
			{
				ss << "  " << def << "\n";
			}
			ss << "</defs>\n";
		}

		// 要素
		for (const auto& elem : m_elements)
		{
			ss << "  " << elem << "\n";
		}

		ss << "</svg>";
		return ss.str();
	}

private:
	int m_width;								///< SVG幅
	int m_height;								///< SVG高さ
	std::vector<std::string> m_defs;			///< defs内の定義（グラデーション、フィルター等）
	std::vector<std::string> m_elements;		///< SVG要素群
	std::string m_pendingFilter;				///< 次の要素に適用するフィルターID

	/// @brief XML特殊文字をエスケープする
	/// @param s 入力文字列
	/// @return エスケープ済み文字列
	[[nodiscard]] static std::string escapeXml(const std::string& s)
	{
		std::string result;
		result.reserve(s.size());
		for (char c : s)
		{
			switch (c)
			{
			case '&':  result += "&amp;"; break;
			case '<':  result += "&lt;"; break;
			case '>':  result += "&gt;"; break;
			case '"':  result += "&quot;"; break;
			case '\'': result += "&apos;"; break;
			default:   result += c; break;
			}
		}
		return result;
	}
};

} // namespace mitiru::asset
