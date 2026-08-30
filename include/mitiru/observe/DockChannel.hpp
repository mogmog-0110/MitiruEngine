#pragma once

/// @file DockChannel.hpp
/// @brief host が自窓の画面矩形を broadcast し、ドックしたツール窓が追従するためのチャネル。
/// @details [[ScrubControlChannel]] と同じ temp-file + atomic rename + mtime polling の流儀。
///          host→tool の一方向。host が自窓の外側矩形と状態を書き、ツール窓が読んで自分の
///          ドック位置 (辺への吸着) を計算して SetWindowPos で追従する。
///
/// wire format (`%TEMP%/mitiru_wnd_<hostpid>.json`):
/// @code
///   { "x": 100, "y": 80, "w": 1280, "h": 720, "active": true, "min": false }
/// @endcode

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

namespace mitiru::observe
{

/// @brief dock broadcast file の場所 (`%TEMP%/mitiru_wnd_<hostpid>.json`)
inline std::filesystem::path dockPathForPid(int hostPid)
{
	return std::filesystem::temp_directory_path()
	     / ("mitiru_wnd_" + std::to_string(hostPid) + ".json");
}

/// @brief host 側。自窓の矩形を書く (writer)。変わった時だけ書けば十分。
class DockWriter
{
public:
	explicit DockWriter(int hostPid)
		: m_path(dockPathForPid(hostPid)), m_tmp(m_path.string() + ".tmp") {}

	/// @brief 矩形 payload を書き出す (atomic rename)。エラーは silent。
	bool write(const nlohmann::json& payload)
	{
		try
		{
			{
				std::ofstream out(m_tmp, std::ios::binary | std::ios::trunc);
				if (!out) { return false; }
				out << payload.dump();
			}
			std::error_code ec;
			std::filesystem::rename(m_tmp, m_path, ec);
			if (ec)
			{
				std::filesystem::remove(m_path, ec);
				std::filesystem::rename(m_tmp, m_path, ec);
				if (ec) { return false; }
			}
			return true;
		}
		catch (...) { return false; }
	}

	[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
	std::filesystem::path m_path, m_tmp;
};

/// @brief ツール側。host 窓の矩形を polling 読み (reader)。mtime が変わった時だけ返す。
class DockReader
{
public:
	explicit DockReader(int hostPid) : m_path(dockPathForPid(hostPid)) {}

	[[nodiscard]] std::optional<nlohmann::json> poll()
	{
		std::error_code ec;
		if (!std::filesystem::exists(m_path, ec)) { return std::nullopt; }
		const auto mt = std::filesystem::last_write_time(m_path, ec);
		if (ec) { return std::nullopt; }
		if (m_have && mt == m_last) { return std::nullopt; }
		try
		{
			std::ifstream in(m_path, std::ios::binary);
			if (!in) { return std::nullopt; }
			auto j = nlohmann::json::parse(in);
			m_last = mt; m_have = true;
			return j;
		}
		catch (...) { return std::nullopt; }
	}

private:
	std::filesystem::path           m_path;
	std::filesystem::file_time_type m_last{};
	bool                            m_have{false};
};

}  // namespace mitiru::observe
