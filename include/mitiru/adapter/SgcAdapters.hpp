#pragma once

/// @file SgcAdapters.hpp
/// @brief sgcアダプター層アンブレラヘッダー
/// @details mitiru::Screen/InputState → sgc::IRenderer/ITextRenderer/ITextMeasure/IInputProvider
///          の全アダプターを一括インクルードする。
///
/// これらのアダプターにより、sgcの全機能（38種UIウィジェット、ActionMap、
/// StateMachine、Transition、ParticleSystem、DialogueSystem等）が
/// mitiruのScreen/InputState上で動作する。
///
/// @code
/// mitiru::Screen screen(800, 600);
/// mitiru::InputState input;
///
/// // sgcアダプター生成
/// mitiru::adapter::ScreenRenderer renderer(screen);
/// mitiru::adapter::ScreenTextRenderer textRenderer(screen);
/// mitiru::adapter::InputAdapter inputAdapter(input);
///
/// // sgcのActionMapが使える
/// sgc::ActionMap actionMap;
/// actionMap.bind("jump", 0x20); // Space
/// actionMap.update(inputAdapter);
/// if (actionMap.isActionJustPressed("jump")) { /* ... */ }
///
/// // sgcのUIウィジェットが使える
/// sgc::ui::evaluateButton(buttonState, rect, inputAdapter);
/// sgc::ui::drawButton(renderer, textRenderer, buttonState, rect, "Click me", theme);
/// @endcode

#include <mitiru/adapter/ScreenRenderer.hpp>
#include <mitiru/adapter/ScreenTextRenderer.hpp>
#include <mitiru/adapter/InputAdapter.hpp>

// sgcの主要機能ヘッダー（便利インクルード）
#include <sgc/input/ActionMap.hpp>
#include <sgc/patterns/StateMachine.hpp>
#include <sgc/scene/Transition.hpp>
#include <sgc/ui/Button.hpp>
#include <sgc/ui/Theme.hpp>
#include <sgc/effects/ParticleSystem.hpp>
#include <sgc/animation/Tween.hpp>
#include <sgc/save/SaveSystem.hpp>
