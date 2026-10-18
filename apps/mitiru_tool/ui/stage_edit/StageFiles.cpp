#include "StageFiles.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace mitiru::tool::stageedit
{

namespace fs = std::filesystem;

namespace
{

std::optional<fs::path> upwardFind(fs::path from, const char* marker)
{
	std::error_code ec;
	for (int i = 0; i < 8 && !from.empty(); ++i)
	{
		if (fs::exists(from / marker, ec)) { return from; }
		const fs::path parent = from.parent_path();
		if (parent == from) { break; }
		from = parent;
	}
	return std::nullopt;
}

fs::path utf8Path(const std::string& s)
{
	return fs::path(std::u8string(s.begin(), s.end()));
}

} // namespace

std::optional<fs::path> StageFiles::findProjectRoot(const std::string& hint)
{
	std::error_code ec;
	if (!hint.empty()) { return fs::exists(utf8Path(hint), ec) ? std::optional<fs::path>(utf8Path(hint)) : std::nullopt; }
	if (const char* env = std::getenv("MITIRU_PROJECT_ROOT"); env != nullptr && env[0] != '\0') { return utf8Path(env); }
	return upwardFind(fs::current_path(ec), "mitiru.toml");
}

std::vector<std::string> StageFiles::listJson(const std::string& dirLogical)
{
	std::error_code ec;
	const fs::path rel = utf8Path(dirLogical);
	fs::path dir;
	if (const char* root = std::getenv("MITIRU_ASSET_ROOT"); root != nullptr && root[0] != '\0' && fs::is_directory(utf8Path(root) / rel, ec)) { dir = utf8Path(root) / rel; }
	else if (fs::is_directory(rel, ec)) { dir = rel; }
	std::vector<std::string> out;
	if (dir.empty()) { return out; }
	for (const auto& e : fs::directory_iterator(dir, ec))
	{
		const std::u8string name = e.path().filename().u8string();
		if (!name.empty() && name.front() != u8'.' && e.path().extension() == ".json") { out.emplace_back(name.begin(), name.end()); }
	}
	std::sort(out.begin(), out.end());
	return out;
}

bool StageFiles::readAll(const fs::path& path, std::string& out)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) { return false; }
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}

bool StageFiles::writeAtomic(const fs::path& path, const std::string& text, std::string& error)
{
	const fs::path tmp = path.parent_path() / ("." + path.filename().string() + ".tmp");
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out) { error = "Can't write: " + path.string(); return false; }
		out.write(text.data(), static_cast<std::streamsize>(text.size()));
		if (!out) { error = "Can't write: " + path.string(); return false; }
	}
	std::error_code ec;
	fs::rename(tmp, path, ec);
	if (ec) { error = "Can't replace: " + path.string() + " (" + ec.message() + ")"; return false; }
	return true;
}

bool StageFiles::open(const std::string& logical, const SourceMap& map, const std::string& projectRootHint, std::string& error)
{
	m_logical = logical;
	m_source.reset();
	std::error_code ec;
	const fs::path rel = utf8Path(logical);
	m_deployed.clear();
	if (const char* root = std::getenv("MITIRU_ASSET_ROOT"); root != nullptr && root[0] != '\0')
	{
		const fs::path candidate = utf8Path(root) / rel;
		if (fs::exists(candidate, ec)) { m_deployed = candidate; }
	}
	if (m_deployed.empty() && fs::exists(rel, ec)) { m_deployed = fs::absolute(rel, ec); }
	if (m_deployed.empty()) { error = "File not found: " + logical + " (put it under MITIRU_ASSET_ROOT or the working folder)"; return false; }
	if (map.valid() && logical.compare(0, map.logicalPrefix.size(), map.logicalPrefix) == 0)
	{
		if (const auto root = findProjectRoot(projectRootHint))
		{
			const fs::path candidate = *root / utf8Path(map.diskPrefix + logical.substr(map.logicalPrefix.size()));
			if (fs::exists(candidate, ec)) { m_source = candidate; }
		}
	}
	const fs::path& truth = m_source ? *m_source : m_deployed;
	if (!readAll(truth, m_original)) { error = "Can't read: " + truth.string(); return false; }
	return true;
}

bool StageFiles::deployedDiffers() const
{
	if (!m_source) { return false; }
	std::string now;
	return readAll(m_deployed, now) && now != m_original;
}

bool StageFiles::tryOut(const std::string& text, std::string& error)
{
	return writeAtomic(m_deployed, text, error);
}

bool StageFiles::keep(const std::string& text, std::string& error)
{
	if (m_source)
	{
		const fs::path backup = m_source->parent_path() / ("." + m_source->filename().string() + ".bak");
		std::string before;
		if (readAll(*m_source, before) && !writeAtomic(backup, before, error)) { return false; }
		if (!writeAtomic(*m_source, text, error)) { return false; }
		m_original = text;
	}
	if (!writeAtomic(m_deployed, text, error)) { return false; }
	m_original = text;
	return true;
}

bool StageFiles::restore(std::string& error)
{
	return writeAtomic(m_deployed, m_original, error);
}

} // namespace mitiru::tool::stageedit
