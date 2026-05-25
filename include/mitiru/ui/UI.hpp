#pragma once

/// @file UI.hpp
/// @brief MitiruEngine 汎用 UI システムのマスター include
/// @details 全 UI ヘッダを依存順にまとめて include する利便性ヘッダ。
///          個別ヘッダを単独で include すればコンパイル時間を最小化できる。

// ── 基盤 ─────────────────────────────────────────────────────────
#include <mitiru/ui/UINode.hpp>

// ── イベントとインタラクション ───────────────────────────────────
#include <mitiru/ui/UIEvent.hpp>
#include <mitiru/ui/UIHitTest.hpp>
#include <mitiru/ui/UIFocus.hpp>

// ── ステートとスタイリング ───────────────────────────────────────
#include <mitiru/ui/UIState.hpp>
#include <mitiru/ui/UIStyle.hpp>
#include <mitiru/ui/UITheme.hpp>

// ── 描画プリミティブ ─────────────────────────────────────────────
#include <mitiru/ui/UINineSlice.hpp>
#include <mitiru/ui/UIAnimation.hpp>

// ── レイアウト ───────────────────────────────────────────────────
#include <mitiru/ui/LayoutEngine.hpp>
#include <mitiru/ui/UITemplate.hpp>

// ── ブリッジモジュール（VN→UI）───────────────────────────────────
#include <mitiru/ui/Easing.hpp>
#include <mitiru/ui/ScrollView.hpp>
#include <mitiru/ui/Modal.hpp>
#include <mitiru/ui/Accessibility.hpp>

// ── 描画 ─────────────────────────────────────────────────────────
#include <mitiru/ui/UIRenderer.hpp>

// ── 観察・デバッグ ───────────────────────────────────────────────
#include <mitiru/ui/UIEventLog.hpp>
#include <mitiru/ui/UISnapshot.hpp>

// ── ウィジェット ─────────────────────────────────────────────────
#include <mitiru/ui/widgets/UIAccordion.hpp>
#include <mitiru/ui/widgets/UIAvatar.hpp>
#include <mitiru/ui/widgets/UIBadge.hpp>
#include <mitiru/ui/widgets/UIButton.hpp>
#include <mitiru/ui/widgets/UICard.hpp>
#include <mitiru/ui/widgets/UICarousel.hpp>
#include <mitiru/ui/widgets/UIChatWindow.hpp>
#include <mitiru/ui/widgets/UIColorPicker.hpp>
#include <mitiru/ui/widgets/UIContextMenu.hpp>
#include <mitiru/ui/widgets/UIDraggableWindow.hpp>
#include <mitiru/ui/widgets/UIDropdown.hpp>
#include <mitiru/ui/widgets/UIFloatingText.hpp>
#include <mitiru/ui/widgets/UIForm.hpp>
#include <mitiru/ui/widgets/UIHotbar.hpp>
#include <mitiru/ui/widgets/UIInventoryGrid.hpp>
#include <mitiru/ui/widgets/UIListView.hpp>
#include <mitiru/ui/widgets/UIMenuBar.hpp>
#include <mitiru/ui/widgets/UINumberSpinner.hpp>
#include <mitiru/ui/widgets/UIProgressBar.hpp>
#include <mitiru/ui/widgets/UIRadialMenu.hpp>
#include <mitiru/ui/widgets/UIRadioGroup.hpp>
#include <mitiru/ui/widgets/UISlider.hpp>
#include <mitiru/ui/widgets/UISplitter.hpp>
#include <mitiru/ui/widgets/UITabBar.hpp>
#include <mitiru/ui/widgets/UITable.hpp>
#include <mitiru/ui/widgets/UITextInput.hpp>
#include <mitiru/ui/widgets/UIToast.hpp>
#include <mitiru/ui/widgets/UIToggle.hpp>
#include <mitiru/ui/widgets/UITooltip.hpp>
#include <mitiru/ui/widgets/UITreeView.hpp>
