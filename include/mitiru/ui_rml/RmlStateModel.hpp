#pragma once
// hud.set(key, 値) で C++ が押し出す HUD 状態を、RmlUi の data model として RML 文書へ見せる橋。
// 「C++ → 表示」と「操作 → C++」を、RmlUi 標準の data-* 束縛 (data-model / {{ }} / data-for / data-if / data-class-* / data-style-* /
// data-event-*) で受けるための最小部品。状態の持ち主は C++ のままで、ここは値を写して操作を貯めるだけ。
//
// キー "view.level" は model "view" の変数 "level" になる。model は 1 つの接頭辞につき 1 つ。
// 操作は data-event-click="dispatch('pick.color', 'id', c.id)" のように書き、名前の後ろは
// key, value の組で payload ({"id":2}) になる。値を 1 つだけ渡すと payload はその値そのもの
// (dispatch('speed', ev.value) → "240"。HTML 版の data-m-action と同じ形)。
// UI の中だけの小さな状態として、確認 (confirm) と文字入力 (prompt) のダイアログの開閉を持つ。

#include "RmlBinderElements.hpp"
#include "RmlFormatters.hpp"
#include "RmlJsonVariable.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mitiru::ui_rml
{

struct DispatchedAction
{
	std::string name;
	std::string payloadJson;   // HTML 版の data-m-payload と同じ JSON 文字列
};

class RmlStateModel
{
public:
	bool create(Rml::Context& context, const std::string& modelName)
	{
		m_prefix = modelName + ".";
		// 変数は文書の読み込み後に初めて set されることがある (HTML 版も push 前は空表示)。
		auto ctor = context.CreateDataModel(modelName, nullptr, /*allow_missing_variables*/ true);
		if (!ctor) { return false; }
		registerBinderFormatters(ctor);
		bindCallbacks(ctor);
		m_ctor = ctor;
		m_handle = ctor.GetModelHandle();
		bindUiVariable("ui_confirm_open", false);
		bindUiVariable("ui_confirm_text", "");
		bindUiVariable("ui_confirm_title", "");
		bindUiVariable("ui_prompt_open", false);
		bindUiVariable("ui_prompt_title", "");
		bindUiVariable("ui_prompt_text", "");
		bindUiVariable("ui_prompt_value", "");
		return true;
	}

	// key は "view.level" のように model 名を含む完全名。別の model のキーは false で無視する。
	// game は毎フレーム同じ文字列を送ってくるので、前と同じ文字列なら JSON として読み直さない。
	bool set(std::string_view key, std::string_view raw)
	{
		if (!ownsKey(key)) { return false; }
		const auto it = m_lastRaw.find(key);
		if (it != m_lastRaw.end() && it->second == raw) { return true; }
		if (it == m_lastRaw.end()) { m_lastRaw.emplace(std::string(key), std::string(raw)); }
		else                       { it->second.assign(raw); }
		apply(key.substr(m_prefix.size()), parseStateValue(raw));
		return true;
	}

	/// 型の決まった値 (hud.set の int / float / bool) はそのまま受ける。
	bool setValue(std::string_view key, nlohmann::json value)
	{
		if (!ownsKey(key)) { return false; }
		if (const auto it = m_lastRaw.find(key); it != m_lastRaw.end()) { m_lastRaw.erase(it); }
		apply(key.substr(m_prefix.size()), std::move(value));
		return true;
	}

	// Context::Update はこれ経由で呼ぶ。RmlUi が束縛値を要素へ書き込むと、<input type=range> などは
	// 利用者が動かしたときと同じ change イベントを出す。それを操作として C++ へ返すと、C++ が送った値が
	// そのまま「お願い」になって戻ってくるので、反映中に起きた dispatch は捨てる (HTML の input.value 代入は
	// change を出さないので、HTML 版と同じ振る舞いになる)。
	void update(Rml::Context& context)
	{
		m_applyingState = true;
		context.Update();
		m_applyingState = false;
		if (std::exchange(m_focusPromptField, false)) { focusAutofocusField(); }
	}

	// 文書の読み込みもこれ経由で行う。range は min / max 属性を読んだ時点で value を丸め直し、
	// そのたびに change を出すので、読み込み中も同じ理由で dispatch を捨てる。
	Rml::ElementDocument* loadDocument(Rml::Context& context, const std::string& path)
	{
		m_applyingState = true;
		Rml::ElementDocument* doc = context.LoadDocument(path);
		m_applyingState = false;
		attachDocument(doc);
		return doc;
	}

	Rml::ElementDocument* loadDocumentFromMemory(Rml::Context& context, const std::string& rml)
	{
		m_applyingState = true;
		Rml::ElementDocument* doc = context.LoadDocumentFromMemory(rml);
		m_applyingState = false;
		attachDocument(doc);
		return doc;
	}

	/// IME で変換中の文字を入力欄に見せている間は true。変換中の文字は入力ではないので、その間の
	/// change から操作を送らない (HTML 版の data-m-input も compositionend まで送らない)。
	void setComposing(bool composing) noexcept { m_composing = composing; }

	std::vector<DispatchedAction> takeActions() { return std::exchange(m_actions, {}); }

private:
	using Args = Rml::VariantList;

	void bindCallbacks(Rml::DataModelConstructor& ctor)
	{
		ctor.BindEventCallback("dispatch", &RmlStateModel::onDispatch, this);
		ctor.BindEventCallback("confirm", &RmlStateModel::onConfirm, this);
		ctor.BindEventCallback("confirm_ok", &RmlStateModel::onConfirmOk, this);
		ctor.BindEventCallback("confirm_cancel", &RmlStateModel::onConfirmCancel, this);
		ctor.BindEventCallback("prompt", &RmlStateModel::onPrompt, this);
		ctor.BindEventCallback("prompt_input", &RmlStateModel::onPromptInput, this);
		ctor.BindEventCallback("prompt_ok", &RmlStateModel::onPromptOk, this);
		ctor.BindEventCallback("prompt_cancel", &RmlStateModel::onPromptCancel, this);
		ctor.BindEventCallback("commit_input", &RmlStateModel::onCommitInput, this);
		ctor.BindEventCallback("live_input", &RmlStateModel::onLiveInput, this);
		ctor.BindEventCallback("drop", &RmlStateModel::onDrop, this);
	}

	void attachDocument(Rml::ElementDocument* doc)
	{
		m_document = doc;
		if (doc != nullptr) { m_dragClasses.attach(*doc); }
	}

	// prompt のダイアログは data-if で開くので、入力欄は開いた後の更新で初めてできる。そこで autofocus の欄を選ぶ。
	void focusAutofocusField()
	{
		Rml::Element* field = m_document != nullptr ? m_document->QuerySelector("input[autofocus]") : nullptr;
		if (field == nullptr) { return; }
		field->Focus();
		const int end = static_cast<int>(Rml::StringUtilities::LengthUTF8(field->GetAttribute<Rml::String>("value", "")));
		if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(field)) { control->SetSelectionRange(end, end); }
	}

	void apply(std::string_view name, nlohmann::json value)
	{
		if (const auto found = m_values.find(name); found != m_values.end())
		{
			// StateStore と同じく、値が変わらなければ表示側へ何も伝えない (再レイアウトも履歴の追加も起きない)。
			if (found->second == value) { return; }
			found->second = std::move(value);
			m_handle.DirtyVariable(found->first);
			return;
		}
		const auto it = m_values.emplace(std::string(name), std::move(value)).first;
		m_ctor.BindCustomDataVariable(it->first, Rml::DataVariable(&JsonVariableDefinition::instance(), &it->second));
		m_handle.DirtyVariable(it->first);
	}

	[[nodiscard]] bool ownsKey(std::string_view key) const noexcept
	{
		return key.size() > m_prefix.size() && key.substr(0, m_prefix.size()) == m_prefix;
	}

	[[nodiscard]] bool acceptsUiEvents() const noexcept { return !m_applyingState && !m_composing; }

	void bindUiVariable(const std::string& name, nlohmann::json initial)
	{
		auto& slot = m_values[name];
		slot = std::move(initial);
		m_ctor.BindCustomDataVariable(name, Rml::DataVariable(&JsonVariableDefinition::instance(), &slot));
	}

	void setUi(const std::string& name, nlohmann::json value)
	{
		m_values[name] = std::move(value);
		m_handle.DirtyVariable(name);
	}

	static nlohmann::json variantToJson(const Rml::Variant& v)
	{
		switch (v.GetType())
		{
		case Rml::Variant::BOOL:   return v.Get<bool>();
		case Rml::Variant::INT:
		case Rml::Variant::INT64:  return v.Get<long long>();
		case Rml::Variant::FLOAT:
		case Rml::Variant::DOUBLE: return numberToJson(v.Get<double>());
		default:                   return parseStateValue(v.Get<Rml::String>());
		}
	}

	// RML の式は数を全て実数で持つ。dispatch('pick', 'id', 3) の 3 は、JS の JSON.stringify と同じく 3 と書く。
	static nlohmann::json numberToJson(double d)
	{
		if (std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 9.0e15) { return static_cast<long long>(d); }
		return d;
	}

	static std::string payloadFrom(const Args& args, std::size_t first)
	{
		if (args.size() == first + 1) { return variantToJson(args[first]).dump(); }
		nlohmann::json payload = nlohmann::json::object();
		for (std::size_t i = first; i + 1 < args.size(); i += 2)
		{
			payload[args[i].Get<Rml::String>()] = variantToJson(args[i + 1]);
		}
		return payload.dump();
	}

	// 入力欄の今の文字列。change なら引数に、blur なら要素の value 属性にある。
	static std::string fieldValue(Rml::Event& ev)
	{
		const Rml::Dictionary& params = ev.GetParameters();
		if (const auto it = params.find("value"); it != params.end()) { return it->second.Get<Rml::String>(); }
		Rml::Element* target = ev.GetTargetElement();
		return target != nullptr ? target->GetAttribute<Rml::String>("value", "") : std::string();
	}

	static std::string valuePayload(const std::string& text)
	{
		return nlohmann::json{ { "value", text } }.dump();
	}

	void onDispatch(Rml::DataModelHandle, Rml::Event&, const Args& args)
	{
		if (args.empty() || !acceptsUiEvents()) { return; }
		m_actions.push_back({ args[0].Get<Rml::String>(), payloadFrom(args, 1) });
	}

	void onConfirm(Rml::DataModelHandle, Rml::Event&, const Args& args)
	{
		if (args.size() < 3) { return; }
		m_pending = { args[2].Get<Rml::String>(), payloadFrom(args, 3) };
		setUi("ui_confirm_text", args[0].Get<Rml::String>());
		setUi("ui_confirm_title", args[1].Get<Rml::String>());
		setUi("ui_confirm_open", true);
	}

	void onConfirmOk(Rml::DataModelHandle, Rml::Event&, const Args&)
	{
		if (!m_pending.name.empty()) { m_actions.push_back(std::exchange(m_pending, {})); }
		setUi("ui_confirm_open", false);
	}

	void onConfirmCancel(Rml::DataModelHandle, Rml::Event&, const Args&)
	{
		m_pending = {};
		setUi("ui_confirm_open", false);
	}

	// prompt('見出し', '本文', 'rename', '初期値')。OK で rename を {"value": 入力した文字} 付きで送る。
	void onPrompt(Rml::DataModelHandle, Rml::Event&, const Args& args)
	{
		if (args.size() < 3) { return; }
		m_promptAction = args[2].Get<Rml::String>();
		m_promptValue = args.size() > 3 ? args[3].Get<Rml::String>() : std::string();
		setUi("ui_prompt_title", args[0].Get<Rml::String>());
		setUi("ui_prompt_text", args[1].Get<Rml::String>());
		setUi("ui_prompt_value", m_promptValue);
		setUi("ui_prompt_open", true);
		m_focusPromptField = true;
	}

	// ダイアログの入力欄の change に付ける。打った文字を覚え、Enter なら OK と同じにする。
	void onPromptInput(Rml::DataModelHandle h, Rml::Event& ev, const Args& args)
	{
		if (m_composing) { return; }
		m_promptValue = fieldValue(ev);
		if (ev.GetParameter<bool>("linebreak", false)) { onPromptOk(h, ev, args); }
	}

	void onPromptOk(Rml::DataModelHandle, Rml::Event&, const Args&)
	{
		if (!m_promptAction.empty()) { m_actions.push_back({ std::exchange(m_promptAction, {}), valuePayload(m_promptValue) }); }
		setUi("ui_prompt_open", false);
	}

	void onPromptCancel(Rml::DataModelHandle, Rml::Event&, const Args&)
	{
		m_promptAction.clear();
		setUi("ui_prompt_open", false);
	}

	// commit_input('player.name') を入力欄の change と blur に付ける。Enter か、欄を離れた時だけ
	// action "input:player.name" を {"value": 文字} 付きで送る (HTML 版の data-m-input と同じ名前と形)。
	void onCommitInput(Rml::DataModelHandle, Rml::Event& ev, const Args& args)
	{
		if (args.empty() || !acceptsUiEvents()) { return; }
		const bool commit = ev.GetId() == Rml::EventId::Blur || ev.GetParameter<bool>("linebreak", false);
		if (commit) { m_actions.push_back({ "input:" + args[0].Get<Rml::String>(), valuePayload(fieldValue(ev)) }); }
	}

	// live_input('search') は 1 文字ごとに送る (data-m-input-live)。
	void onLiveInput(Rml::DataModelHandle, Rml::Event& ev, const Args& args)
	{
		if (args.empty() || !acceptsUiEvents()) { return; }
		m_actions.push_back({ "input:" + args[0].Get<Rml::String>(), valuePayload(fieldValue(ev)) });
	}

	// 落とし先の data-event-dragdrop="drop('move', s.id)"。掴んだ要素の drag-value 属性を from、
	// 引数を to にして {"from": …, "to": …} を送る (HTML 版の data-m-drag / data-m-drop と同じ形)。
	void onDrop(Rml::DataModelHandle, Rml::Event& ev, const Args& args)
	{
		if (args.empty() || !acceptsUiEvents()) { return; }
		auto* dragged = static_cast<Rml::Element*>(ev.GetParameter<void*>("drag_element", nullptr));
		if (dragged == nullptr || dragged == ev.GetCurrentElement()) { return; }
		nlohmann::json payload = nlohmann::json::object();
		payload["from"] = parseStateValue(dragged->GetAttribute<Rml::String>("drag-value", ""));
		payload["to"] = args.size() > 1 ? variantToJson(args[1]) : nlohmann::json();
		m_actions.push_back({ args[0].Get<Rml::String>(), payload.dump() });
	}

	std::string m_prefix;
	Rml::DataModelConstructor m_ctor;
	Rml::DataModelHandle m_handle;
	// std::map のノードは挿入しても動かないので、RmlUi に渡したポインタは有効なまま残る。
	std::map<std::string, nlohmann::json, std::less<>> m_values;
	std::map<std::string, std::string, std::less<>> m_lastRaw;
	std::vector<DispatchedAction> m_actions;
	DispatchedAction m_pending;
	std::string m_promptAction;
	std::string m_promptValue;
	DragClassListener m_dragClasses;
	Rml::ElementDocument* m_document = nullptr;
	bool m_applyingState = false;
	bool m_focusPromptField = false;
	bool m_composing = false;
};

} // namespace mitiru::ui_rml
