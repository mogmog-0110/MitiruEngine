#pragma once

/// @file SvgGenerator.hpp
/// @brief JSON定義からSVGアセットを生成するジェネレーター
/// @details JSON形式のアセット定義を解析し、ネオングロー・グラデーション等の
///          SVGエフェクトを含む完全なSVG 1.1 XML文字列を出力する。
///
/// @code
/// // JSONからSVGドキュメントを構築
/// std::string json = R"json({"shape":"circle","x":50,"y":50,"radius":30,
///     "style":{"fill":"#00ffff","stroke":"#00cccc","strokeWidth":2,
///     "glow":{"color":"#00ffff","radius":8}}})json";
/// auto doc = mitiru::asset::SvgGenerator::fromJson(json);
/// if (doc)
/// {
///     std::string svg = mitiru::asset::SvgGenerator::toSvg(*doc);
/// }
/// @endcode

#include <cmath>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mitiru::asset
{

/// @brief SVGスタイル情報
struct SvgStyle
{
	std::string fillColor{"none"};     ///< 塗りつぶし色
	std::string strokeColor{"none"};   ///< ストローク色
	float strokeWidth{0.0f};           ///< ストローク幅
	float opacity{1.0f};               ///< 不透明度（0.0〜1.0）
	std::string glowColor;             ///< グローエフェクト色（空ならなし）
	float glowRadius{0.0f};            ///< グローの半径
};

/// @brief SVG図形の種類
enum class SvgShape
{
	Circle,    ///< 円
	Rect,      ///< 矩形
	Triangle,  ///< 三角形
	Polygon,   ///< 多角形
	Path,      ///< SVGパス
	Text,      ///< テキスト
	Group      ///< グループ（子要素を含む）
};

/// @brief SVG要素の定義
struct SvgElement
{
	SvgShape shape{SvgShape::Rect};          ///< 図形の種類
	float x{0.0f};                            ///< X座標
	float y{0.0f};                            ///< Y座標
	float width{0.0f};                        ///< 幅（矩形用）
	float height{0.0f};                       ///< 高さ（矩形用）
	float radius{0.0f};                       ///< 半径（円用）
	std::string pathData;                     ///< パスデータ（Path用）
	SvgStyle style;                           ///< スタイル情報
	std::vector<SvgElement> children;         ///< 子要素（Group用）
	std::string text;                         ///< テキスト内容（Text用）
	float fontSize{14.0f};                    ///< フォントサイズ（Text用）
	std::string transform;                    ///< transform属性
	std::vector<std::pair<float, float>> points; ///< ポリゴン頂点（Polygon/Triangle用）
};

/// @brief SVGドキュメント全体の定義
struct SvgDocument
{
	float viewBoxW{100.0f};                   ///< viewBoxの幅
	float viewBoxH{100.0f};                   ///< viewBoxの高さ
	std::vector<SvgElement> elements;         ///< 要素リスト
	std::string title;                        ///< ドキュメントタイトル
};

/// @brief JSON定義からSVGを生成するジェネレーター
/// @details 簡易JSONパーサーを内蔵し、アセット定義JSONを解析して
///          ネオングロー付きSVGを出力する。
class SvgGenerator
{
public:
	/// @brief JSON文字列からSvgDocumentを構築する
	/// @param json JSON形式のアセット定義
	/// @return 成功時はSvgDocument、失敗時はnullopt
	[[nodiscard]] static std::optional<SvgDocument> fromJson(const std::string& json)
	{
		auto trimmed = trim(json);
		if (trimmed.empty() || trimmed.front() != '{')
		{
			return std::nullopt;
		}

		SvgDocument doc;
		SvgElement elem;

		// viewBox解析
		doc.viewBoxW = extractFloat(trimmed, "viewBoxW", 100.0f);
		doc.viewBoxH = extractFloat(trimmed, "viewBoxH", 100.0f);
		doc.title = extractString(trimmed, "title");

		// 図形タイプ
		const auto shapeStr = extractString(trimmed, "shape");
		if (shapeStr == "circle")
		{
			elem.shape = SvgShape::Circle;
		}
		else if (shapeStr == "rect")
		{
			elem.shape = SvgShape::Rect;
		}
		else if (shapeStr == "triangle")
		{
			elem.shape = SvgShape::Triangle;
		}
		else if (shapeStr == "path")
		{
			elem.shape = SvgShape::Path;
		}
		else if (shapeStr == "text")
		{
			elem.shape = SvgShape::Text;
		}
		else if (shapeStr == "group")
		{
			elem.shape = SvgShape::Group;
		}

		// 座標・サイズ
		elem.x = extractFloat(trimmed, "x", 0.0f);
		elem.y = extractFloat(trimmed, "y", 0.0f);
		elem.width = extractFloat(trimmed, "width", 0.0f);
		elem.height = extractFloat(trimmed, "height", 0.0f);
		elem.radius = extractFloat(trimmed, "radius", 0.0f);
		elem.pathData = extractString(trimmed, "pathData");
		elem.text = extractString(trimmed, "text");
		elem.fontSize = extractFloat(trimmed, "fontSize", 14.0f);

		// スタイル解析
		auto styleBlock = extractObject(trimmed, "style");
		if (!styleBlock.empty())
		{
			elem.style.fillColor = extractString(styleBlock, "fill");
			if (elem.style.fillColor.empty())
			{
				elem.style.fillColor = "none";
			}
			elem.style.strokeColor = extractString(styleBlock, "stroke");
			if (elem.style.strokeColor.empty())
			{
				elem.style.strokeColor = "none";
			}
			elem.style.strokeWidth = extractFloat(styleBlock, "strokeWidth", 0.0f);
			elem.style.opacity = extractFloat(styleBlock, "opacity", 1.0f);

			auto glowBlock = extractObject(styleBlock, "glow");
			if (!glowBlock.empty())
			{
				elem.style.glowColor = extractString(glowBlock, "color");
				elem.style.glowRadius = extractFloat(glowBlock, "radius", 0.0f);
			}
		}

		doc.elements.push_back(elem);
		return doc;
	}

	/// @brief SvgDocumentをSVG XML文字列に変換する
	/// @param doc SVGドキュメント
	/// @return 完全なSVG 1.1 XML文字列
	[[nodiscard]] static std::string toSvg(const SvgDocument& doc)
	{
		std::ostringstream ss;
		ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		   << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
		   << "viewBox=\"0 0 " << doc.viewBoxW << " " << doc.viewBoxH << "\">\n";

		if (!doc.title.empty())
		{
			ss << "<title>" << escapeXml(doc.title) << "</title>\n";
		}

		// defs収集（グロー・グラデーション）
		std::vector<std::string> defs;
		int filterId = 0;
		std::map<int, std::string> elementFilterMap; ///< 要素index → フィルターID

		for (size_t i = 0; i < doc.elements.size(); ++i)
		{
			collectDefs(doc.elements[i], defs, filterId, elementFilterMap, static_cast<int>(i));
		}

		if (!defs.empty())
		{
			ss << "<defs>\n";
			for (const auto& def : defs)
			{
				ss << def << "\n";
			}
			ss << "</defs>\n";
		}

		// 要素出力
		for (size_t i = 0; i < doc.elements.size(); ++i)
		{
			auto it = elementFilterMap.find(static_cast<int>(i));
			const std::string filterRef = (it != elementFilterMap.end())
				? it->second : "";
			renderElement(ss, doc.elements[i], filterRef, "  ");
		}

		ss << "</svg>";
		return ss.str();
	}

	/// @brief ネオングローのSVGフィルター定義を生成する
	/// @param color グロー色
	/// @param radius グローの半径（stdDeviation値）
	/// @return SVGフィルター定義文字列（<filter>タグ）
	[[nodiscard]] static std::string generateGlow(const std::string& color, float radius)
	{
		std::ostringstream ss;
		ss << "<filter id=\"glow\" x=\"-50%\" y=\"-50%\" width=\"200%\" height=\"200%\">"
		   << "<feGaussianBlur in=\"SourceGraphic\" stdDeviation=\"" << radius << "\" result=\"blur\"/>"
		   << "<feFlood flood-color=\"" << color << "\" flood-opacity=\"0.8\" result=\"glowColor\"/>"
		   << "<feComposite in=\"glowColor\" in2=\"blur\" operator=\"in\" result=\"softGlow\"/>"
		   << "<feMerge>"
		   << "<feMergeNode in=\"softGlow\"/>"
		   << "<feMergeNode in=\"softGlow\"/>"
		   << "<feMergeNode in=\"SourceGraphic\"/>"
		   << "</feMerge>"
		   << "</filter>";
		return ss.str();
	}

	/// @brief 線形グラデーション定義を生成する
	/// @param id グラデーションのID
	/// @param color1 開始色
	/// @param color2 終了色
	/// @param vertical true=垂直方向、false=水平方向
	/// @return SVGグラデーション定義文字列（<linearGradient>タグ）
	[[nodiscard]] static std::string generateGradient(const std::string& id,
		const std::string& color1, const std::string& color2, bool vertical = false)
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
		return ss.str();
	}

private:
	/// @brief 要素からdefs（フィルター・グラデーション）を収集する
	static void collectDefs(const SvgElement& elem, std::vector<std::string>& defs,
		int& filterId, std::map<int, std::string>& filterMap, int elemIndex)
	{
		if (!elem.style.glowColor.empty() && elem.style.glowRadius > 0.0f)
		{
			const std::string fid = "glow" + std::to_string(filterId);
			std::ostringstream ss;
			ss << "<filter id=\"" << fid << "\" x=\"-50%\" y=\"-50%\" width=\"200%\" height=\"200%\">"
			   << "<feGaussianBlur in=\"SourceGraphic\" stdDeviation=\"" << elem.style.glowRadius << "\" result=\"blur\"/>"
			   << "<feFlood flood-color=\"" << elem.style.glowColor << "\" flood-opacity=\"0.8\" result=\"glowColor\"/>"
			   << "<feComposite in=\"glowColor\" in2=\"blur\" operator=\"in\" result=\"softGlow\"/>"
			   << "<feMerge>"
			   << "<feMergeNode in=\"softGlow\"/>"
			   << "<feMergeNode in=\"softGlow\"/>"
			   << "<feMergeNode in=\"SourceGraphic\"/>"
			   << "</feMerge>"
			   << "</filter>";
			defs.push_back(ss.str());
			filterMap[elemIndex] = fid;
			++filterId;
		}

		// 子要素の再帰処理
		for (size_t i = 0; i < elem.children.size(); ++i)
		{
			collectDefs(elem.children[i], defs, filterId, filterMap, -1);
		}
	}

	/// @brief 単一の要素をSVG XMLとして出力する
	static void renderElement(std::ostringstream& ss, const SvgElement& elem,
		const std::string& filterRef, const std::string& indent)
	{
		switch (elem.shape)
		{
		case SvgShape::Circle:
			ss << indent << "<circle cx=\"" << elem.x << "\" cy=\"" << elem.y
			   << "\" r=\"" << elem.radius << "\"";
			writeStyle(ss, elem.style);
			if (!filterRef.empty())
			{
				ss << " filter=\"url(#" << filterRef << ")\"";
			}
			ss << "/>\n";
			break;

		case SvgShape::Rect:
			ss << indent << "<rect x=\"" << elem.x << "\" y=\"" << elem.y
			   << "\" width=\"" << elem.width << "\" height=\"" << elem.height << "\"";
			writeStyle(ss, elem.style);
			if (!filterRef.empty())
			{
				ss << " filter=\"url(#" << filterRef << ")\"";
			}
			ss << "/>\n";
			break;

		case SvgShape::Triangle:
		case SvgShape::Polygon:
		{
			ss << indent << "<polygon points=\"";
			if (!elem.points.empty())
			{
				for (size_t i = 0; i < elem.points.size(); ++i)
				{
					if (i > 0)
					{
						ss << " ";
					}
					ss << elem.points[i].first << "," << elem.points[i].second;
				}
			}
			else if (elem.shape == SvgShape::Triangle)
			{
				// デフォルト三角形（上向き）
				const float cx = elem.x + elem.width * 0.5f;
				ss << cx << "," << elem.y << " "
				   << elem.x << "," << (elem.y + elem.height) << " "
				   << (elem.x + elem.width) << "," << (elem.y + elem.height);
			}
			ss << "\"";
			writeStyle(ss, elem.style);
			if (!filterRef.empty())
			{
				ss << " filter=\"url(#" << filterRef << ")\"";
			}
			ss << "/>\n";
			break;
		}

		case SvgShape::Path:
			ss << indent << "<path d=\"" << elem.pathData << "\"";
			writeStyle(ss, elem.style);
			if (!filterRef.empty())
			{
				ss << " filter=\"url(#" << filterRef << ")\"";
			}
			ss << "/>\n";
			break;

		case SvgShape::Text:
			ss << indent << "<text x=\"" << elem.x << "\" y=\"" << elem.y
			   << "\" font-size=\"" << elem.fontSize
			   << "\" text-anchor=\"middle\" dominant-baseline=\"central\""
			   << " font-family=\"monospace\"";
			writeStyle(ss, elem.style);
			if (!filterRef.empty())
			{
				ss << " filter=\"url(#" << filterRef << ")\"";
			}
			ss << ">" << escapeXml(elem.text) << "</text>\n";
			break;

		case SvgShape::Group:
			ss << indent << "<g";
			if (!elem.transform.empty())
			{
				ss << " transform=\"" << elem.transform << "\"";
			}
			if (!filterRef.empty())
			{
				ss << " filter=\"url(#" << filterRef << ")\"";
			}
			ss << ">\n";
			for (const auto& child : elem.children)
			{
				renderElement(ss, child, "", indent + "  ");
			}
			ss << indent << "</g>\n";
			break;
		}
	}

	/// @brief スタイル属性をストリームに書き出す
	static void writeStyle(std::ostringstream& ss, const SvgStyle& style)
	{
		if (style.fillColor != "none" && !style.fillColor.empty())
		{
			ss << " fill=\"" << style.fillColor << "\"";
		}
		else
		{
			ss << " fill=\"none\"";
		}
		if (style.strokeColor != "none" && !style.strokeColor.empty())
		{
			ss << " stroke=\"" << style.strokeColor << "\"";
			if (style.strokeWidth > 0.0f)
			{
				ss << " stroke-width=\"" << style.strokeWidth << "\"";
			}
		}
		if (style.opacity < 1.0f)
		{
			ss << " opacity=\"" << style.opacity << "\"";
		}
	}

	/// @brief XML特殊文字をエスケープする
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

	// ========== 簡易JSONパーサーユーティリティ ==========

	/// @brief 文字列の前後空白を除去する
	[[nodiscard]] static std::string trim(const std::string& s)
	{
		const auto start = s.find_first_not_of(" \t\n\r");
		if (start == std::string::npos)
		{
			return "";
		}
		const auto end = s.find_last_not_of(" \t\n\r");
		return s.substr(start, end - start + 1);
	}

	/// @brief JSONから文字列値を抽出する
	[[nodiscard]] static std::string extractString(const std::string& json, const std::string& key)
	{
		const std::string search = "\"" + key + "\"";
		auto pos = json.find(search);
		if (pos == std::string::npos)
		{
			return "";
		}
		pos = json.find(':', pos + search.size());
		if (pos == std::string::npos)
		{
			return "";
		}
		pos = json.find('"', pos + 1);
		if (pos == std::string::npos)
		{
			return "";
		}
		const auto end = json.find('"', pos + 1);
		if (end == std::string::npos)
		{
			return "";
		}
		return json.substr(pos + 1, end - pos - 1);
	}

	/// @brief JSONから数値を抽出する
	[[nodiscard]] static float extractFloat(const std::string& json, const std::string& key, float defaultVal)
	{
		const std::string search = "\"" + key + "\"";
		auto pos = json.find(search);
		if (pos == std::string::npos)
		{
			return defaultVal;
		}
		pos = json.find(':', pos + search.size());
		if (pos == std::string::npos)
		{
			return defaultVal;
		}
		++pos;
		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
		{
			++pos;
		}
		if (pos >= json.size())
		{
			return defaultVal;
		}
		try
		{
			return std::stof(json.substr(pos));
		}
		catch (...)
		{
			return defaultVal;
		}
	}

	/// @brief JSONからネストされたオブジェクトを抽出する
	[[nodiscard]] static std::string extractObject(const std::string& json, const std::string& key)
	{
		const std::string search = "\"" + key + "\"";
		auto pos = json.find(search);
		if (pos == std::string::npos)
		{
			return "";
		}
		pos = json.find('{', pos + search.size());
		if (pos == std::string::npos)
		{
			return "";
		}
		int depth = 1;
		size_t end = pos + 1;
		while (end < json.size() && depth > 0)
		{
			if (json[end] == '{')
			{
				++depth;
			}
			else if (json[end] == '}')
			{
				--depth;
			}
			++end;
		}
		return json.substr(pos, end - pos);
	}

	/// @brief JSONからbool値を抽出する
	[[nodiscard]] static bool extractBool(const std::string& json, const std::string& key, bool defaultVal)
	{
		const std::string search = "\"" + key + "\"";
		auto pos = json.find(search);
		if (pos == std::string::npos)
		{
			return defaultVal;
		}
		pos = json.find(':', pos + search.size());
		if (pos == std::string::npos)
		{
			return defaultVal;
		}
		const auto rest = json.substr(pos + 1);
		if (rest.find("true") < rest.find("false"))
		{
			return true;
		}
		if (rest.find("false") != std::string::npos)
		{
			return false;
		}
		return defaultVal;
	}
};

} // namespace mitiru::asset
