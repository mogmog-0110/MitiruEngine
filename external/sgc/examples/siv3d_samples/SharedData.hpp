#pragma once
/// @file SharedData.hpp
/// @brief サンプルギャラリー共有データ

#include "sgc/graphics/IRenderer.hpp"
#include "sgc/graphics/ITextMeasure.hpp"
#include "sgc/graphics/ITextRenderer.hpp"
#include "sgc/input/IInputProvider.hpp"

/// @brief シーン間共有データ
struct SharedData
{
	sgc::IRenderer* renderer = nullptr;
	sgc::ITextRenderer* textRenderer = nullptr;
	sgc::ITextMeasure* textMeasure = nullptr;
	sgc::IInputProvider* inputProvider = nullptr;
	float screenWidth = 800.0f;
	float screenHeight = 600.0f;
};
