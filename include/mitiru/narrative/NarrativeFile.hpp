#pragma once

/// @file NarrativeFile.hpp
/// @brief カットシーン・会話の台本・クエスト表のファイル 1 つ。ゲーム DLL の static に置き、`asset.reloaded` で読み直す。
/// @details 中身は読み取り専用のアセットで、ゲームの状態ではない (状態は GameMemory の Playback / State 側)。
///          読み直しに失敗したら (書きかけの保存など) 前の中身を残し、理由を error() に置く。
///          訳 (localize) は読み直しの後にも掛け直す。

#include <optional>
#include <string>
#include <string_view>

#include <mitiru/asset/AssetReload.hpp>
#include <mitiru/core/Localization.hpp>

namespace mitiru::narrative
{

template <class T>
class NarrativeFile
{
public:
	using Loader = std::optional<T> (*)(std::string_view path, std::string& error);

	NarrativeFile(std::string path, Loader loader) : m_path(std::move(path)), m_loader(loader) {}

	/// @brief 初めて呼んだときに読む。読めなければ空の T を返し、理由は error()
	[[nodiscard]] const T& get()
	{
		if (!m_loaded) { reload(); }
		return m_value;
	}

	/// @brief payload (`in.actionPayload("asset.reloaded")`) がこのファイルなら読み直して true
	bool reloadIf(const char* reloadPayloadJson)
	{
		if (!asset::isReloadOf(reloadPayloadJson, m_path)) { return false; }
		reload();
		return true;
	}

	/// @brief 文言を language の訳に替える (鍵の無い文言は書いたまま)。language が前と同じなら何もしない
	void localize(const LocalizationManager& loc, std::string_view language)
	{
		if (m_loc == &loc && m_language == language) { return; }
		m_loc = &loc;
		m_language = std::string(language);
		(void)get();
		m_value.localize(loc, m_language);
	}

	[[nodiscard]] const std::string& path() const noexcept { return m_path; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

private:
	void reload()
	{
		m_loaded = true;
		std::optional<T> fresh = m_loader(m_path, m_error);
		if (!fresh) { return; }
		m_value = std::move(*fresh);
		if (m_loc != nullptr) { m_value.localize(*m_loc, m_language); }
	}

	std::string                m_path;
	Loader                     m_loader;
	T                          m_value{};
	std::string                m_error;
	const LocalizationManager* m_loc = nullptr;
	std::string                m_language;
	bool                       m_loaded = false;
};

}  // namespace mitiru::narrative
