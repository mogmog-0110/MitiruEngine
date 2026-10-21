#pragma once

/// @file RenderNeedsResolve.hpp
/// @brief DLL の export か EngineConfig が持つ宣言の文字列と環境変数から、旧体制か新体制か、何を宣言したかを決める (ADR 0090 の 3.2)。
/// @details 副作用が無い関数にして、GPU も窓も無い試験で体制の切り替えを確かめられるようにする。

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/render/RenderFeatures.hpp>

namespace mitiru::render::features
{

struct ResolvedNeeds
{
	RenderNeeds needs;                        ///< declaredMode が false なら旧体制
	std::vector<std::string> unknown;         ///< 表に無い名前 (綴りの誤り)
	std::vector<std::string> notDeclarable;   ///< 基本の行の名前。宣言しなくても使える
};

/// @param configured DLL の export か EngineConfig::renderNeeds の文字列。nullopt は export が無い DLL
/// @param envValue 環境変数 MITIRU_RENDER_NEEDS。"legacy" は export があっても旧体制へ戻す (画素の突き合わせと切り分け用)。
///        "none" は何も宣言しない新体制。それ以外の空でない文字列は、export の代わりにその宣言を使う (census と効果の測定を、ゲームを組み直さずに回すため)
[[nodiscard]] inline ResolvedNeeds resolveRenderNeeds(const std::optional<std::string>& configured, std::string_view envValue)
{
	ResolvedNeeds out;
	if (envValue == "legacy") { return out; }
	std::string_view text;
	if (envValue == "none") { text = {}; }
	else if (!envValue.empty()) { text = envValue; }
	else if (configured) { text = *configured; }
	else { return out; }
	const NeedsParse parsed = parseNeeds(text);
	out.needs = makeRenderNeeds(parsed);
	out.unknown = parsed.unknown;
	out.notDeclarable = parsed.notDeclarable;
	return out;
}

}  // namespace mitiru::render::features
