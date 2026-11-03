#pragma once

/// @file AccessKitBridge.hpp
/// @brief UiAccessTree (RML の木) を AccessKit の木に写して Windows の UI Automation に見せ、読み上げソフトからの操作を受ける。
/// @details MITIRU_HAS_ACCESSKIT が無いビルドでは存在しない (cmake/MitiruAccessKit.cmake、ADR 0117)。
///          accesskit.dll は遅延読み込みで、無い PC では dllAvailable() が false を返し、host は段階 1 (UiaRoot) のまま動く。
///          WM_GETOBJECT は窓のスレッド、操作の頼みは UIA のスレッドから届く。操作は鍵で守ったキューに積み、
///          フレームの中で host が UI へ渡す (UI は状態を持たない)。

#ifdef MITIRU_HAS_ACCESSKIT

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <accesskit.h>

#include <mitiru/ui_rml/UiAccessTree.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mitiru::platform
{

namespace detail
{

[[nodiscard]] inline accesskit_role accessKitRole(ui_rml::AccessRole r) noexcept
{
	using R = ui_rml::AccessRole;
	switch (r)
	{
	case R::Window: return ACCESSKIT_ROLE_WINDOW;
	case R::Group: return ACCESSKIT_ROLE_GROUP;
	case R::Label: return ACCESSKIT_ROLE_LABEL;
	case R::Heading: return ACCESSKIT_ROLE_HEADING;
	case R::Button: return ACCESSKIT_ROLE_BUTTON;
	case R::Link: return ACCESSKIT_ROLE_LINK;
	case R::CheckBox: return ACCESSKIT_ROLE_CHECK_BOX;
	case R::RadioButton: return ACCESSKIT_ROLE_RADIO_BUTTON;
	case R::Slider: return ACCESSKIT_ROLE_SLIDER;
	case R::ProgressBar: return ACCESSKIT_ROLE_PROGRESS_INDICATOR;
	case R::ComboBox: return ACCESSKIT_ROLE_COMBO_BOX;
	case R::TextInput: return ACCESSKIT_ROLE_TEXT_INPUT;
	case R::PasswordInput: return ACCESSKIT_ROLE_PASSWORD_INPUT;
	case R::TabList: return ACCESSKIT_ROLE_TAB_LIST;
	case R::Tab: return ACCESSKIT_ROLE_TAB;
	case R::TabPanel: return ACCESSKIT_ROLE_TAB_PANEL;
	case R::Image: return ACCESSKIT_ROLE_IMAGE;
	case R::List: return ACCESSKIT_ROLE_LIST;
	case R::ListItem: return ACCESSKIT_ROLE_LIST_ITEM;
	case R::Dialog: return ACCESSKIT_ROLE_DIALOG;
	}
	return ACCESSKIT_ROLE_GROUP;
}

/// 窓の画素に合わせる倍率。UI の座標は論理解像度なので、窓の客領域の大きさで伸ばす。
struct BoundsScale
{
	double x = 1.0;
	double y = 1.0;
};

[[nodiscard]] inline accesskit_node* accessKitNode(const ui_rml::AccessNode& n, BoundsScale scale)
{
	using namespace ui_rml;
	accesskit_node* node = accesskit_node_new(accessKitRole(n.role));
	// 文字そのものを表す Label は、AccessKit では名前でなく value に文字を持つ (label だと UIA の Name が空になる)
	if (n.role == AccessRole::Label) { accesskit_node_set_value_with_length(node, n.name.data(), n.name.size()); }
	else if (!n.name.empty()) { accesskit_node_set_label_with_length(node, n.name.data(), n.name.size()); }
	if (!n.value.empty() && n.role != AccessRole::Label) { accesskit_node_set_value_with_length(node, n.value.data(), n.value.size()); }
	if (!n.children.empty()) { accesskit_node_set_children(node, n.children.size(), n.children.data()); }
	if (n.bounds.w > 0.0f && n.bounds.h > 0.0f)
	{
		const double left = n.bounds.x * scale.x;
		const double top = n.bounds.y * scale.y;
		accesskit_node_set_bounds(node, accesskit_rect{ left, top, left + n.bounds.w * scale.x, top + n.bounds.h * scale.y });
	}
	if ((n.actions & access_action::kFocus) != 0) { accesskit_node_add_action(node, ACCESSKIT_ACTION_FOCUS); }
	if ((n.actions & access_action::kClick) != 0) { accesskit_node_add_action(node, ACCESSKIT_ACTION_CLICK); }
	if ((n.actions & access_action::kSetValue) != 0) { accesskit_node_add_action(node, ACCESSKIT_ACTION_SET_VALUE); }
	if ((n.actions & access_action::kIncrement) != 0) { accesskit_node_add_action(node, ACCESSKIT_ACTION_INCREMENT); }
	if ((n.actions & access_action::kDecrement) != 0) { accesskit_node_add_action(node, ACCESSKIT_ACTION_DECREMENT); }
	if (n.disabled) { accesskit_node_set_disabled(node); }
	if (n.modal) { accesskit_node_set_modal(node); }
	if (n.toggle != AccessToggle::None)
	{
		accesskit_node_set_toggled(node, n.toggle == AccessToggle::On ? ACCESSKIT_TOGGLED_TRUE : ACCESSKIT_TOGGLED_FALSE);
	}
	if (n.role == AccessRole::Tab) { accesskit_node_set_selected(node, n.selected); }
	if (n.level > 0) { accesskit_node_set_level(node, static_cast<std::size_t>(n.level)); }
	if (n.live != AccessLive::Off)
	{
		accesskit_node_set_live(node, n.live == AccessLive::Assertive ? ACCESSKIT_LIVE_ASSERTIVE : ACCESSKIT_LIVE_POLITE);
	}
	if (n.hasRange)
	{
		accesskit_node_set_min_numeric_value(node, n.rangeMin);
		accesskit_node_set_max_numeric_value(node, n.rangeMax);
		if (n.rangeStep > 0.0) { accesskit_node_set_numeric_value_step(node, n.rangeStep); }
		accesskit_node_set_numeric_value(node, n.rangeValue);
	}
	return node;
}

/// changed の節だけを入れた更新。changed が nullptr なら全部の節。tree 情報 (根) は全体を渡すときだけ付ける。
[[nodiscard]] inline accesskit_tree_update* accessKitUpdate(
	const ui_rml::UiAccessTree& tree, const std::vector<std::size_t>* changed, BoundsScale scale)
{
	const std::size_t count = changed != nullptr ? changed->size() : tree.nodes.size();
	accesskit_tree_update* update = accesskit_tree_update_with_capacity_and_focus(count, tree.focus);
	for (std::size_t i = 0; i < count; ++i)
	{
		const ui_rml::AccessNode& n = tree.nodes[changed != nullptr ? (*changed)[i] : i];
		accesskit_tree_update_push_node(update, n.id, accessKitNode(n, scale));
	}
	if (changed == nullptr)
	{
		accesskit_tree_update_set_tree_info(update, accesskit_tree_info_new(tree.nodes.front().id));
	}
	return update;
}

}  // namespace detail

class AccessKitBridge
{
public:
	AccessKitBridge() = default;
	~AccessKitBridge() { detach(); }
	AccessKitBridge(const AccessKitBridge&) = delete;
	AccessKitBridge& operator=(const AccessKitBridge&) = delete;

	/// accesskit.dll を読めるか。遅延読み込みの dll が無いと最初の呼び出しで例外になるので、先に確かめる。
	[[nodiscard]] static bool dllAvailable() noexcept
	{
		static const bool ok = ::LoadLibraryW(L"accesskit.dll") != nullptr;
		return ok;
	}

	/// @brief hwnd にアダプターを付ける。dll が無ければ false で、以後の呼び出しは何もしない。
	bool attach(HWND hwnd, bool windowFocused)
	{
		detach();
		if (hwnd == nullptr || !dllAvailable()) { return false; }
		m_adapter = accesskit_windows_adapter_new(hwnd, windowFocused, &AccessKitBridge::onAction, this);
		if (m_adapter == nullptr) { return false; }
		m_hwnd = hwnd;
		return true;
	}

	void detach() noexcept
	{
		if (m_adapter != nullptr) { accesskit_windows_adapter_free(m_adapter); }
		m_adapter = nullptr;
		m_hwnd = nullptr;
		m_active.store(false);
		m_hasSent = false;
		m_latest = {};
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_requests.clear();
	}

	[[nodiscard]] bool attached() const noexcept { return m_adapter != nullptr; }
	[[nodiscard]] HWND window() const noexcept { return m_hwnd; }

	/// 読み上げソフトが木を要求したか (WM_GETOBJECT で一度でも根を問い合わせられた)。要求が来るまで木は送らない。
	[[nodiscard]] bool active() const noexcept { return m_active.load(); }

	/// 窓の客領域の大きさ (画素)。UI の座標 (論理解像度) をこの大きさへ伸ばして、節の位置にする。窓の大きさが変わったら呼ぶ。
	void setClientSize(int width, int height) noexcept { m_clientW = width; m_clientH = height; }

	/// @brief 窓手続きの WM_GETOBJECT から呼ぶ。UI Automation の根の問い合わせだけ答え、他は OS の既定に返す。
	[[nodiscard]] LRESULT handleGetObject(HWND hwnd, WPARAM wParam, LPARAM lParam)
	{
		if (m_adapter != nullptr)
		{
			const accesskit_opt_lresult r =
				accesskit_windows_adapter_handle_wm_getobject(m_adapter, wParam, lParam, &AccessKitBridge::onActivate, this);
			if (r.has_value) { return r.value; }
		}
		return ::DefWindowProcW(hwnd, WM_GETOBJECT, wParam, lParam);
	}

	void setWindowFocused(bool focused)
	{
		if (m_adapter == nullptr) { return; }
		if (accesskit_windows_queued_events* events = accesskit_windows_adapter_update_window_focus_state(m_adapter, focused))
		{
			accesskit_windows_queued_events_raise(events);
		}
	}

	/// @brief 最新の木を渡す。読み上げソフトが要求している間だけ、前から変わった節を AccessKit へ送る。
	/// @return 送ったら true。形の崩れた木は送らず false
	bool push(const ui_rml::UiAccessTree& tree)
	{
		if (m_adapter == nullptr || !ui_rml::isWellFormed(tree)) { return false; }
		m_latest = tree;
		m_scale = scaleFor(tree);
		if (!m_active.load()) { return false; }
		const ui_rml::AccessDelta delta = ui_rml::diffAccessTree(m_hasSent ? &m_sent : nullptr, tree);
		if (delta.changed.empty() && !delta.focusChanged) { return false; }
		struct Context
		{
			const ui_rml::UiAccessTree* tree;
			const ui_rml::AccessDelta* delta;
			bool full;
			detail::BoundsScale scale;
		};
		Context ctx{ &tree, &delta, !m_hasSent, m_scale };
		accesskit_windows_queued_events* events = accesskit_windows_adapter_update_if_active(
			m_adapter,
			[](void* userdata) {
				const auto* c = static_cast<const Context*>(userdata);
				return detail::accessKitUpdate(*c->tree, c->full ? nullptr : &c->delta->changed, c->scale);
			},
			&ctx);
		if (events != nullptr) { accesskit_windows_queued_events_raise(events); }
		m_sent = tree;
		m_hasSent = true;
		return true;
	}

	/// 溜まった操作の頼みを取り出す。フレームのスレッドで呼び、UI に届ける。
	[[nodiscard]] std::vector<ui_rml::AccessRequest> takeRequests()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return std::exchange(m_requests, {});
	}

private:
	accesskit_windows_adapter* m_adapter = nullptr;
	HWND m_hwnd = nullptr;
	std::atomic<bool> m_active{ false };
	detail::BoundsScale m_scale;
	int m_clientW = 0;
	int m_clientH = 0;
	ui_rml::UiAccessTree m_latest;   ///< 最後に渡された木。読み上げソフトが後から来たとき、最初に見せる
	ui_rml::UiAccessTree m_sent;     ///< AccessKit に送った最後の木。差分の元
	bool m_hasSent = false;
	std::mutex m_mutex;
	std::vector<ui_rml::AccessRequest> m_requests;

	[[nodiscard]] detail::BoundsScale scaleFor(const ui_rml::UiAccessTree& tree) const noexcept
	{
		if (tree.logicalWidth <= 0.0f || tree.logicalHeight <= 0.0f || m_clientW <= 0 || m_clientH <= 0) { return {}; }
		return { m_clientW / static_cast<double>(tree.logicalWidth), m_clientH / static_cast<double>(tree.logicalHeight) };
	}

	// 窓のスレッドで、読み上げソフトが最初に問い合わせた時に呼ばれる。null は返せないので、木がまだ無ければ根だけを返す。
	static accesskit_tree_update* onActivate(void* userdata)
	{
		auto* self = static_cast<AccessKitBridge*>(userdata);
		self->m_active.store(true);
		if (!ui_rml::isWellFormed(self->m_latest))
		{
			ui_rml::UiAccessTree rootOnly;
			ui_rml::AccessNode root;
			root.id = ui_rml::kAccessRootId;
			root.role = ui_rml::AccessRole::Window;
			rootOnly.nodes.push_back(std::move(root));
			self->m_latest = std::move(rootOnly);
		}
		self->m_sent = self->m_latest;
		self->m_hasSent = true;
		return detail::accessKitUpdate(self->m_latest, nullptr, self->m_scale);
	}

	// UIA のスレッドから来る。キューに積むだけで、UI には触らない。request の持ち主はこちら
	static void onAction(accesskit_action_request* request, void* userdata)
	{
		auto* self = static_cast<AccessKitBridge*>(userdata);
		using Kind = ui_rml::AccessRequest::Kind;
		ui_rml::AccessRequest out;
		out.target = request->target_node;
		bool wanted = true;
		switch (request->action)
		{
		case ACCESSKIT_ACTION_FOCUS: out.kind = Kind::Focus; break;
		case ACCESSKIT_ACTION_CLICK: out.kind = Kind::Click; break;
		case ACCESSKIT_ACTION_INCREMENT: out.kind = Kind::Increment; break;
		case ACCESSKIT_ACTION_DECREMENT: out.kind = Kind::Decrement; break;
		case ACCESSKIT_ACTION_SET_VALUE:
			out.kind = Kind::SetValue;
			if (request->data.has_value && request->data.value.tag == ACCESSKIT_ACTION_DATA_VALUE &&
			    request->data.value.value != nullptr)
			{
				out.text = request->data.value.value;
			}
			else if (request->data.has_value && request->data.value.tag == ACCESSKIT_ACTION_DATA_NUMERIC_VALUE)
			{
				out.number = request->data.value.numeric_value;
				out.hasNumber = true;
			}
			else { wanted = false; }
			break;
		default: wanted = false; break;
		}
		if (wanted)
		{
			const std::lock_guard<std::mutex> lock(self->m_mutex);
			// 読み上げソフトが連打しても無限にたまらないようにする
			if (self->m_requests.size() < 64) { self->m_requests.push_back(std::move(out)); }
		}
		accesskit_action_request_free(request);
	}
};

}  // namespace mitiru::platform

#endif  // MITIRU_HAS_ACCESSKIT
