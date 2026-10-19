#include "TuneModel.hpp"

#include <mitiru/data/JsonText.hpp>

#include <algorithm>
#include <cmath>

namespace mitiru::tool::motiontune
{

using mitiru::data::JsonText;

bool sameValue(double a, double b) noexcept
{
	return static_cast<float>(a) == static_cast<float>(b);
}

TuneModel::TuneModel(const TuneSchema& schema) : m_schema(&schema)
{
	for (std::size_t fi = 0; fi < schema.files.size(); ++fi)
	{
		const FileDecl& file = schema.files[fi];
		for (std::size_t gi = 0; gi < file.groups.size(); ++gi)
		{
			const Group& group = file.groups[gi];
			for (const Field& field : group.fields)
			{
				Entry e;
				e.name = memoryName(file, group, field);
				e.pointer = jsonPointer(group, field);
				e.key = field.key;
				e.groupLabel = group.label;
				e.hint = field.hint;
				e.kind = field.kind;
				e.file = static_cast<int>(fi);
				e.group = static_cast<int>(gi);
				e.min = field.min;
				e.max = field.max;
				m_byName[e.name] = static_cast<int>(m_entries.size());
				m_entries.push_back(std::move(e));
			}
		}
	}
}

namespace
{

std::optional<double> numberAt(const JsonText& doc, const std::string& pointer)
{
	const JsonText::Node* n = doc.find(pointer);
	if (n == nullptr) { return std::nullopt; }
	const nlohmann::ordered_json v = doc.value(pointer);
	if (v.is_boolean()) { return v.get<bool>() ? 1.0 : 0.0; }
	if (v.is_number()) { return v.get<double>(); }
	return std::nullopt;
}

} // namespace

bool TuneModel::loadFile(int file, const std::string& text, std::string& error)
{
	std::optional<JsonText> doc = JsonText::parse(text, error);
	if (!doc) { return false; }
	for (Entry& e : m_entries)
	{
		if (e.file != file) { continue; }
		const std::optional<double> v = numberAt(*doc, e.pointer);
		e.hasFile = v.has_value();
		e.fileValue = v.value_or(0.0);
		e.editValue = e.fileValue;
	}
	return true;
}

int TuneModel::setLive(const nlohmann::json& quiet)
{
	int n = 0;
	if (!quiet.is_object()) { return 0; }
	for (Entry& e : m_entries)
	{
		const auto it = quiet.find(e.name);
		if (it == quiet.end()) { e.hasLive = false; continue; }
		if (it->is_boolean()) { e.liveValue = it->get<bool>() ? 1.0 : 0.0; }
		else if (it->is_number()) { e.liveValue = it->get<double>(); }
		else { e.hasLive = false; continue; }
		e.hasLive = true;
		++n;
	}
	return n;
}

bool TuneModel::set(const std::string& name, double value)
{
	const auto it = m_byName.find(name);
	if (it == m_byName.end() || !std::isfinite(value)) { return false; }
	Entry& e = m_entries[static_cast<std::size_t>(it->second)];
	value = std::clamp(value, e.min, e.max);
	if (e.kind == "whole") { value = std::round(value); }
	else if (e.kind == "flag") { value = value >= 0.5 ? 1.0 : 0.0; }
	e.editValue = value;
	return true;
}

const Entry* TuneModel::find(const std::string& name) const
{
	const auto it = m_byName.find(name);
	return it == m_byName.end() ? nullptr : &m_entries[static_cast<std::size_t>(it->second)];
}

std::vector<int> TuneModel::unsaved() const
{
	std::vector<int> out;
	for (std::size_t i = 0; i < m_entries.size(); ++i)
	{
		if (m_entries[i].hasFile && !sameValue(m_entries[i].editValue, m_entries[i].fileValue)) { out.push_back(static_cast<int>(i)); }
	}
	return out;
}

std::vector<int> TuneModel::untried() const
{
	std::vector<int> out;
	for (std::size_t i = 0; i < m_entries.size(); ++i)
	{
		if (m_entries[i].hasLive && !sameValue(m_entries[i].editValue, m_entries[i].liveValue)) { out.push_back(static_cast<int>(i)); }
	}
	return out;
}

std::vector<int> TuneModel::tried() const
{
	std::vector<int> out;
	for (std::size_t i = 0; i < m_entries.size(); ++i)
	{
		if (m_entries[i].hasLive && m_entries[i].hasFile && !sameValue(m_entries[i].liveValue, m_entries[i].fileValue)) { out.push_back(static_cast<int>(i)); }
	}
	return out;
}

std::vector<int> TuneModel::toRevert() const
{
	std::vector<int> out;
	for (std::size_t i = 0; i < m_entries.size(); ++i)
	{
		const Entry& e = m_entries[i];
		if (!e.hasFile) { continue; }
		if (e.pushed || (e.hasLive && !sameValue(e.liveValue, e.fileValue))) { out.push_back(static_cast<int>(i)); }
	}
	return out;
}

nlohmann::json TuneModel::valuesOf(const std::vector<int>& indexes, bool useFile) const
{
	nlohmann::json out = nlohmann::json::object();
	for (const int i : indexes)
	{
		const Entry& e = m_entries[static_cast<std::size_t>(i)];
		const double v = useFile ? e.fileValue : e.editValue;
		if (e.kind == "flag") { out[e.name] = v >= 0.5; }
		else if (e.kind == "whole") { out[e.name] = static_cast<long long>(std::llround(v)); }
		else { out[e.name] = v; }
	}
	return out;
}

std::optional<std::string> TuneModel::render(int file, const std::string& text, std::string& error) const
{
	std::optional<JsonText> doc = JsonText::parse(text, error);
	if (!doc) { return std::nullopt; }
	for (const int i : unsaved())
	{
		const Entry& e = m_entries[static_cast<std::size_t>(i)];
		if (e.file != file) { continue; }
		const bool ok = e.kind == "flag" ? doc->setBool(e.pointer, e.editValue >= 0.5) : doc->setNumber(e.pointer, e.editValue);
		if (!ok) { error = "Can't rewrite " + e.pointer + " in the JSON"; return std::nullopt; }
	}
	return doc->render();
}

void TuneModel::markKept(int file)
{
	for (Entry& e : m_entries) { if (e.hasFile && e.file == file) { e.fileValue = e.editValue; e.pushed = false; } }
}

void TuneModel::revertEdits()
{
	for (Entry& e : m_entries) { if (e.hasFile) { e.editValue = e.fileValue; } }
}

void TuneModel::markPushed(const nlohmann::json& sent)
{
	if (!sent.is_object()) { return; }
	for (Entry& e : m_entries) { if (sent.contains(e.name)) { e.pushed = true; } }
}

void TuneModel::clearPushed()
{
	for (Entry& e : m_entries) { e.pushed = false; }
}

void TuneModel::markLive(const nlohmann::json& sent)
{
	if (!sent.is_object()) { return; }
	for (Entry& e : m_entries)
	{
		const auto it = sent.find(e.name);
		if (it == sent.end() || !e.hasLive) { continue; }
		e.liveValue = it->is_boolean() ? (it->get<bool>() ? 1.0 : 0.0) : it->get<double>();
	}
}

} // namespace mitiru::tool::motiontune
