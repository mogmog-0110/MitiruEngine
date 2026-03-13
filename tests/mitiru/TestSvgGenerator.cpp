#include <catch2/catch_test_macros.hpp>

#include "mitiru/asset/SvgGenerator.hpp"
#include "mitiru/asset/GameAssetTemplates.hpp"
#include "mitiru/asset/AssetPipeline.hpp"

using namespace mitiru::asset;

// ============================================================
// SvgElement creation
// ============================================================

TEST_CASE("SvgElement default construction", "[mitiru][asset][svg]")
{
	SvgElement elem;
	REQUIRE(elem.shape == SvgShape::Rect);
	REQUIRE(elem.x == 0.0f);
	REQUIRE(elem.y == 0.0f);
	REQUIRE(elem.width == 0.0f);
	REQUIRE(elem.height == 0.0f);
	REQUIRE(elem.radius == 0.0f);
	REQUIRE(elem.style.fillColor == "none");
	REQUIRE(elem.style.opacity == 1.0f);
	REQUIRE(elem.children.empty());
}

// ============================================================
// SvgDocument basic structure
// ============================================================

TEST_CASE("SvgDocument default construction", "[mitiru][asset][svg]")
{
	SvgDocument doc;
	REQUIRE(doc.viewBoxW == 100.0f);
	REQUIRE(doc.viewBoxH == 100.0f);
	REQUIRE(doc.elements.empty());
	REQUIRE(doc.title.empty());
}

// ============================================================
// SvgGenerator::toSvg
// ============================================================

TEST_CASE("SvgGenerator toSvg produces valid XML header", "[mitiru][asset][svg]")
{
	SvgDocument doc;
	doc.viewBoxW = 200;
	doc.viewBoxH = 100;

	auto svg = SvgGenerator::toSvg(doc);
	REQUIRE(svg.find("<?xml version=\"1.0\"") != std::string::npos);
	REQUIRE(svg.find("<svg xmlns=") != std::string::npos);
	REQUIRE(svg.find("viewBox=\"0 0 200 100\"") != std::string::npos);
	REQUIRE(svg.find("</svg>") != std::string::npos);
}

TEST_CASE("SvgGenerator toSvg with circle element", "[mitiru][asset][svg]")
{
	SvgDocument doc;
	doc.viewBoxW = 100;
	doc.viewBoxH = 100;

	SvgElement circle;
	circle.shape = SvgShape::Circle;
	circle.x = 50;
	circle.y = 50;
	circle.radius = 30;
	circle.style.fillColor = "#00ffff";
	circle.style.strokeColor = "#00cccc";
	circle.style.strokeWidth = 2.0f;
	doc.elements.push_back(circle);

	auto svg = SvgGenerator::toSvg(doc);
	REQUIRE(svg.find("<circle") != std::string::npos);
	REQUIRE(svg.find("cx=\"50\"") != std::string::npos);
	REQUIRE(svg.find("cy=\"50\"") != std::string::npos);
	REQUIRE(svg.find("r=\"30\"") != std::string::npos);
	REQUIRE(svg.find("fill=\"#00ffff\"") != std::string::npos);
	REQUIRE(svg.find("stroke=\"#00cccc\"") != std::string::npos);
}

TEST_CASE("SvgGenerator toSvg with rect element", "[mitiru][asset][svg]")
{
	SvgDocument doc;
	SvgElement rect;
	rect.shape = SvgShape::Rect;
	rect.x = 10;
	rect.y = 20;
	rect.width = 80;
	rect.height = 40;
	rect.style.fillColor = "#ff8800";
	doc.elements.push_back(rect);

	auto svg = SvgGenerator::toSvg(doc);
	REQUIRE(svg.find("<rect") != std::string::npos);
	REQUIRE(svg.find("width=\"80\"") != std::string::npos);
	REQUIRE(svg.find("height=\"40\"") != std::string::npos);
}

TEST_CASE("SvgGenerator toSvg with path element", "[mitiru][asset][svg]")
{
	SvgDocument doc;
	SvgElement path;
	path.shape = SvgShape::Path;
	path.pathData = "M 10 10 L 90 90";
	path.style.fillColor = "none";
	path.style.strokeColor = "white";
	path.style.strokeWidth = 2.0f;
	doc.elements.push_back(path);

	auto svg = SvgGenerator::toSvg(doc);
	REQUIRE(svg.find("<path") != std::string::npos);
	REQUIRE(svg.find("d=\"M 10 10 L 90 90\"") != std::string::npos);
}

TEST_CASE("SvgGenerator toSvg with nested group", "[mitiru][asset][svg]")
{
	SvgDocument doc;
	SvgElement group;
	group.shape = SvgShape::Group;
	group.transform = "translate(10,10)";

	SvgElement child;
	child.shape = SvgShape::Circle;
	child.x = 20;
	child.y = 20;
	child.radius = 10;
	child.style.fillColor = "red";
	group.children.push_back(child);

	doc.elements.push_back(group);

	auto svg = SvgGenerator::toSvg(doc);
	REQUIRE(svg.find("<g") != std::string::npos);
	REQUIRE(svg.find("translate(10,10)") != std::string::npos);
	REQUIRE(svg.find("<circle") != std::string::npos);
	REQUIRE(svg.find("</g>") != std::string::npos);
}

TEST_CASE("SvgGenerator toSvg with glow filter", "[mitiru][asset][svg]")
{
	SvgDocument doc;
	SvgElement elem;
	elem.shape = SvgShape::Circle;
	elem.x = 50;
	elem.y = 50;
	elem.radius = 20;
	elem.style.fillColor = "#00ffff";
	elem.style.glowColor = "#00ffff";
	elem.style.glowRadius = 8.0f;
	doc.elements.push_back(elem);

	auto svg = SvgGenerator::toSvg(doc);
	REQUIRE(svg.find("<defs>") != std::string::npos);
	REQUIRE(svg.find("<filter id=\"glow0\"") != std::string::npos);
	REQUIRE(svg.find("feGaussianBlur") != std::string::npos);
	REQUIRE(svg.find("filter=\"url(#glow0)\"") != std::string::npos);
}

TEST_CASE("SvgGenerator toSvg with gradient", "[mitiru][asset][svg]")
{
	auto grad = SvgGenerator::generateGradient("bg", "#4A90D9", "#2C3E50", true);
	REQUIRE(grad.find("<linearGradient id=\"bg\"") != std::string::npos);
	REQUIRE(grad.find("x2=\"0\" y2=\"1\"") != std::string::npos);
	REQUIRE(grad.find("stop-color=\"#4A90D9\"") != std::string::npos);
	REQUIRE(grad.find("stop-color=\"#2C3E50\"") != std::string::npos);
}

TEST_CASE("SvgGenerator generateGlow produces filter", "[mitiru][asset][svg]")
{
	auto filter = SvgGenerator::generateGlow("#ff00ff", 6.0f);
	REQUIRE(filter.find("<filter id=\"glow\"") != std::string::npos);
	REQUIRE(filter.find("stdDeviation=\"6\"") != std::string::npos);
	REQUIRE(filter.find("flood-color=\"#ff00ff\"") != std::string::npos);
	REQUIRE(filter.find("feMerge") != std::string::npos);
}

// ============================================================
// SvgGenerator::fromJson roundtrip
// ============================================================

TEST_CASE("SvgGenerator fromJson roundtrip", "[mitiru][asset][svg]")
{
	std::string json = R"json({"shape":"circle","x":50,"y":50,"radius":30,
		"style":{"fill":"#00ffff","stroke":"#00cccc","strokeWidth":2,
		"glow":{"color":"#00ffff","radius":8}}})json";

	auto doc = SvgGenerator::fromJson(json);
	REQUIRE(doc.has_value());
	REQUIRE(doc->elements.size() == 1);
	REQUIRE(doc->elements[0].shape == SvgShape::Circle);
	REQUIRE(doc->elements[0].x == 50.0f);
	REQUIRE(doc->elements[0].radius == 30.0f);
	REQUIRE(doc->elements[0].style.fillColor == "#00ffff");
	REQUIRE(doc->elements[0].style.glowColor == "#00ffff");
	REQUIRE(doc->elements[0].style.glowRadius == 8.0f);

	auto svg = SvgGenerator::toSvg(*doc);
	REQUIRE(svg.find("<circle") != std::string::npos);
	REQUIRE(svg.find("filter=\"url(#glow0)\"") != std::string::npos);
}

TEST_CASE("SvgGenerator fromJson with invalid input returns nullopt", "[mitiru][asset][svg]")
{
	REQUIRE_FALSE(SvgGenerator::fromJson("").has_value());
	REQUIRE_FALSE(SvgGenerator::fromJson("not json").has_value());
	REQUIRE_FALSE(SvgGenerator::fromJson("[]").has_value());
}

// ============================================================
// GameAssetTemplates
// ============================================================

TEST_CASE("GameAssetTemplates player produces valid SVG", "[mitiru][asset][templates]")
{
	auto doc = GameAssetTemplates::player(20.0f, "#00ffff");
	REQUIRE(doc.title == "Player");
	REQUIRE_FALSE(doc.elements.empty());
	REQUIRE(doc.viewBoxW > 0);
	REQUIRE(doc.viewBoxH > 0);

	auto svg = SvgGenerator::toSvg(doc);
	REQUIRE(svg.find("<svg") != std::string::npos);
	REQUIRE(svg.find("<circle") != std::string::npos);
	REQUIRE(svg.find("</svg>") != std::string::npos);
}

TEST_CASE("GameAssetTemplates platform produces valid SVG", "[mitiru][asset][templates]")
{
	auto doc = GameAssetTemplates::platform(120.0f, 20.0f, "#00ff88");
	REQUIRE(doc.title == "Platform");
	REQUIRE_FALSE(doc.elements.empty());

	auto svg = SvgGenerator::toSvg(doc);
	REQUIRE(svg.find("<rect") != std::string::npos);
	REQUIRE(svg.find("</svg>") != std::string::npos);
}

TEST_CASE("GameAssetTemplates all templates produce non-empty SVG", "[mitiru][asset][templates]")
{
	// 全14テンプレートが有効なSVGを出力するか検証
	struct TemplateTest
	{
		std::string name;
		SvgDocument doc;
	};

	std::vector<TemplateTest> tests = {
		{"player", GameAssetTemplates::player()},
		{"platform", GameAssetTemplates::platform()},
		{"movingPlatform", GameAssetTemplates::movingPlatform()},
		{"crumblingPlatform", GameAssetTemplates::crumblingPlatform()},
		{"springPlatform", GameAssetTemplates::springPlatform()},
		{"spikeHazard", GameAssetTemplates::spikeHazard()},
		{"laserBarrier", GameAssetTemplates::laserBarrier()},
		{"collectible", GameAssetTemplates::collectible()},
		{"checkpoint", GameAssetTemplates::checkpoint()},
		{"npc", GameAssetTemplates::npc()},
		{"enemy", GameAssetTemplates::enemy()},
		{"gate", GameAssetTemplates::gate()},
		{"goal", GameAssetTemplates::goal()},
		{"formulaButton", GameAssetTemplates::formulaButton()},
	};

	for (const auto& t : tests)
	{
		INFO("Template: " + t.name);
		REQUIRE_FALSE(t.doc.elements.empty());
		REQUIRE(t.doc.viewBoxW > 0);
		REQUIRE(t.doc.viewBoxH > 0);

		auto svg = SvgGenerator::toSvg(t.doc);
		REQUIRE(svg.find("<?xml") != std::string::npos);
		REQUIRE(svg.find("<svg") != std::string::npos);
		REQUIRE(svg.find("</svg>") != std::string::npos);
		REQUIRE(svg.size() > 100);
	}
}

// ============================================================
// AssetPipeline
// ============================================================

TEST_CASE("AssetPipeline loadManifest parses entries", "[mitiru][asset][pipeline]")
{
	AssetPipeline pipeline;
	std::string json = R"json([
		{"name":"player1","templateType":"player","params":{"radius":"25","color":"#00ffff"}},
		{"name":"ground1","templateType":"platform","params":{"w":"200","h":"24","color":"#00ff88"}}
	])json";

	auto entries = pipeline.loadManifest(json);
	REQUIRE(entries.size() == 2);
	REQUIRE(entries[0].name == "player1");
	REQUIRE(entries[0].templateType == "player");
	REQUIRE(entries[0].params.at("radius") == "25");
	REQUIRE(entries[1].name == "ground1");
	REQUIRE(entries[1].templateType == "platform");
}

TEST_CASE("AssetPipeline generateAll produces all assets", "[mitiru][asset][pipeline]")
{
	AssetPipeline pipeline;
	std::string json = R"json([
		{"name":"p","templateType":"player","params":{"radius":"15","color":"#00ffff"}},
		{"name":"g","templateType":"platform","params":{"w":"100","h":"16","color":"#00ff88"}},
		{"name":"e","templateType":"enemy","params":{"size":"32","color":"#ff4444"}}
	])json";

	auto entries = pipeline.loadManifest(json);
	REQUIRE(entries.size() == 3);

	auto assets = pipeline.generateAll(entries);
	REQUIRE(assets.size() == 3);
	REQUIRE(assets.count("p") == 1);
	REQUIRE(assets.count("g") == 1);
	REQUIRE(assets.count("e") == 1);

	// 全てが有効なSVG
	for (const auto& [name, svg] : assets)
	{
		INFO("Asset: " + name);
		REQUIRE(svg.find("<svg") != std::string::npos);
		REQUIRE(svg.find("</svg>") != std::string::npos);
	}
}

TEST_CASE("AssetPipeline generateOne returns nullopt for unknown type", "[mitiru][asset][pipeline]")
{
	AssetPipeline pipeline;
	AssetManifest m;
	m.name = "unknown";
	m.templateType = "nonexistent_type";

	auto result = pipeline.generateOne(m);
	REQUIRE_FALSE(result.has_value());
}

TEST_CASE("AssetPipeline exportManifestTemplate is valid JSON array", "[mitiru][asset][pipeline]")
{
	AssetPipeline pipeline;
	auto tmpl = pipeline.exportManifestTemplate();

	// 基本的なJSON配列構造の検証
	REQUIRE_FALSE(tmpl.empty());

	auto trimmed = tmpl;
	auto start = trimmed.find_first_not_of(" \t\n\r");
	REQUIRE(start != std::string::npos);
	REQUIRE(trimmed[start] == '[');

	auto end = trimmed.find_last_not_of(" \t\n\r");
	REQUIRE(trimmed[end] == ']');

	// 全テンプレートタイプが含まれているか
	REQUIRE(tmpl.find("player") != std::string::npos);
	REQUIRE(tmpl.find("platform") != std::string::npos);
	REQUIRE(tmpl.find("movingPlatform") != std::string::npos);
	REQUIRE(tmpl.find("spikeHazard") != std::string::npos);
	REQUIRE(tmpl.find("formulaButton") != std::string::npos);

	// マニフェストとしてロード可能か
	auto entries = pipeline.loadManifest(tmpl);
	REQUIRE(entries.size() == 14);
}

TEST_CASE("AssetPipeline roundtrip from template manifest", "[mitiru][asset][pipeline]")
{
	AssetPipeline pipeline;
	auto tmpl = pipeline.exportManifestTemplate();
	auto entries = pipeline.loadManifest(tmpl);
	auto assets = pipeline.generateAll(entries);

	// 全14アセットが生成されること
	REQUIRE(assets.size() == 14);

	for (const auto& [name, svg] : assets)
	{
		INFO("Roundtrip asset: " + name);
		REQUIRE(svg.find("<svg") != std::string::npos);
		REQUIRE(svg.size() > 50);
	}
}
