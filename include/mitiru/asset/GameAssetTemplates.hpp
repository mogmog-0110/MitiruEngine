#pragma once

/// @file GameAssetTemplates.hpp
/// @brief GraphWalker用ゲームオブジェクトのSVGテンプレート集
/// @details サイバーパンク風ネオングロー付きのゲームアセットを
///          プログラム的に生成する静的メソッド群。
///          各カテゴリは個別ヘッダーに分割されている。
///
/// @code
/// auto playerSvg = mitiru::asset::GameAssetTemplates::player(20.0f, "#00ffff");
/// auto svgStr = mitiru::asset::SvgGenerator::toSvg(playerSvg);
/// @endcode

#include "GameAssetUtil.hpp"
#include "GameAssetCharacters.hpp"
#include "GameAssetEnvironment.hpp"
#include "GameAssetUI.hpp"
#include "GameAssetEffects.hpp"

namespace mitiru::asset
{

/// @brief GraphWalkerゲームオブジェクトのSVGテンプレート群
/// @details 各メソッドはネオングロー付きのSvgDocumentを返す。
///          カラーパレットはGraphWalkerのゾーン配色に準拠。
///          カテゴリ別ヘッダーから全メソッドを継承する。
class GameAssetTemplates
	: public GameAssetCharacters
	, public GameAssetEnvironment
	, public GameAssetUI
	, public GameAssetEffects
{
public:
	// すべての静的メソッドは基底クラスから継承される:
	//
	// GameAssetCharacters: player(), npc(), enemy()
	// GameAssetEnvironment: platform(), movingPlatform(), crumblingPlatform(),
	//                       springPlatform(), checkpoint(), gate(), goal()
	// GameAssetUI: collectible(), formulaButton()
	// GameAssetEffects: spikeHazard(), laserBarrier()

	using GameAssetCharacters::player;
	using GameAssetCharacters::npc;
	using GameAssetCharacters::enemy;

	using GameAssetEnvironment::platform;
	using GameAssetEnvironment::movingPlatform;
	using GameAssetEnvironment::crumblingPlatform;
	using GameAssetEnvironment::springPlatform;
	using GameAssetEnvironment::checkpoint;
	using GameAssetEnvironment::gate;
	using GameAssetEnvironment::goal;

	using GameAssetUI::collectible;
	using GameAssetUI::formulaButton;

	using GameAssetEffects::spikeHazard;
	using GameAssetEffects::laserBarrier;
};

} // namespace mitiru::asset
