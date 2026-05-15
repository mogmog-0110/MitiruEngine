#pragma once

/// @file UI.hpp
/// @brief Master include for the MitiruEngine general-purpose UI system.
/// @details Includes all UI headers in dependency order for convenience.
///          Individual headers can still be included separately for
///          minimal compile-time overhead.

// ── Foundation ───────────────────────────────────────────────────
#include <mitiru/ui/UINode.hpp>

// ── Events and interaction ───────────────────────────────────────
#include <mitiru/ui/UIEvent.hpp>
#include <mitiru/ui/UIHitTest.hpp>
#include <mitiru/ui/UIFocus.hpp>

// ── State and styling ────────────────────────────────────────────
#include <mitiru/ui/UIState.hpp>
#include <mitiru/ui/UIStyle.hpp>
#include <mitiru/ui/UITheme.hpp>

// ── Rendering primitives ─────────────────────────────────────────
#include <mitiru/ui/UINineSlice.hpp>
#include <mitiru/ui/UIAnimation.hpp>

// ── Layout ───────────────────────────────────────────────────────
#include <mitiru/ui/LayoutEngine.hpp>
#include <mitiru/ui/UITemplate.hpp>

// ── Bridge modules (VN-to-UI) ────────────────────────────────────
#include <mitiru/ui/Easing.hpp>
#include <mitiru/ui/ScrollView.hpp>
#include <mitiru/ui/Modal.hpp>
#include <mitiru/ui/Accessibility.hpp>

// ── Rendering ────────────────────────────────────────────────────
#include <mitiru/ui/UIRenderer.hpp>

// ── Observation / debugging ──────────────────────────────────────
#include <mitiru/ui/UIEventLog.hpp>
#include <mitiru/ui/UISnapshot.hpp>

// ── Widgets ──────────────────────────────────────────────────────
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
