#pragma once

/// @file PackageRuntime.hpp
/// @brief ランタイムパッケージローダー
/// @details パッケージのマニフェスト読み込み、コマンド登録、アセットパス追加、
///          初期化スクリプト実行を管理する。CommandSystem・AssetManager と統合。

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mitiru::resource
{

using PackageHandle = std::uint32_t;
inline constexpr PackageHandle INVALID_PACKAGE_HANDLE = 0;

/// @brief パッケージのコマンド定義
struct PackageCommand
{
	std::string name;
	std::string description;
};

/// @brief パッケージ情報
struct PackageInfo
{
	std::string id;
	std::string name;
	std::string version;
	std::string path;
	std::vector<std::string> dependencies;
	std::vector<std::string> headers;
	std::vector<PackageCommand> commands;
	std::string initScript;
};

/// @brief コマンド登録コールバック型
using CommandRegistrar = std::function<void(const std::string& name, const std::string& desc)>;
/// @brief アセットパス追加コールバック型
using AssetPathRegistrar = std::function<void(const std::string& path)>;
/// @brief スクリプト実行コールバック型
using ScriptRunner = std::function<bool(const std::string& path)>;

/// @brief ランタイムパッケージローダー
/// @details package.json マニフェストを読み込み、依存関係解決・コマンド登録・
///          アセットパス追加・初期化スクリプト実行を行う。
///
/// @code
/// PackageRuntime runtime;
/// runtime.setCommandRegistrar([&](auto& n, auto& d) { cmdSys.reg(n, d); });
/// runtime.setAssetPathRegistrar([&](auto& p) { assets.addPath(p); });
/// auto h = runtime.loadPackage("packages/my_plugin");
/// @endcode
class PackageRuntime
{
	struct Loaded { PackageHandle handle{}; PackageInfo info; bool initialized = false; };

	std::vector<Loaded> m_packages;
	std::unordered_map<std::string, PackageHandle> m_idMap;
	PackageHandle m_nextHandle = 1;
	std::string m_lastError;
	CommandRegistrar m_cmdReg;
	AssetPathRegistrar m_assetReg;
	ScriptRunner m_scriptRunner;

public:
	PackageRuntime() = default;
	~PackageRuntime() = default;
	PackageRuntime(const PackageRuntime&) = delete;
	PackageRuntime& operator=(const PackageRuntime&) = delete;
	PackageRuntime(PackageRuntime&&) noexcept = default;
	PackageRuntime& operator=(PackageRuntime&&) noexcept = default;

	void setCommandRegistrar(CommandRegistrar r) { m_cmdReg = std::move(r); }
	void setAssetPathRegistrar(AssetPathRegistrar r) { m_assetReg = std::move(r); }
	void setScriptRunner(ScriptRunner r) { m_scriptRunner = std::move(r); }

	/// @brief パッケージをロードする
	/// @param path パッケージのルートディレクトリ
	/// @return ハンドル（失敗時は INVALID_PACKAGE_HANDLE）
	[[nodiscard]] PackageHandle loadPackage(std::string_view path)
	{
		const std::string pkgPath{path};
		const auto manifest = parseManifest(pkgPath + "/package.json");
		if (manifest.id.empty())
		{
			m_lastError = "Failed to parse manifest: " + pkgPath + "/package.json";
			return INVALID_PACKAGE_HANDLE;
		}
		if (isLoaded(manifest.id))
		{
			m_lastError = "Already loaded: " + manifest.id;
			return INVALID_PACKAGE_HANDLE;
		}
		for (const auto& dep : manifest.dependencies)
		{
			if (!isLoaded(dep))
			{
				m_lastError = "Missing dependency: " + dep + " (required by " + manifest.id + ")";
				return INVALID_PACKAGE_HANDLE;
			}
		}

		const auto handle = m_nextHandle++;
		Loaded pkg;
		pkg.handle = handle;
		pkg.info = PackageInfo{manifest.id, manifest.name.empty() ? manifest.id : manifest.name,
			manifest.version, pkgPath, manifest.dependencies, manifest.headers,
			manifest.commands, manifest.initScript};

		if (m_assetReg) { m_assetReg(pkgPath + "/assets"); }
		if (m_cmdReg) { for (const auto& c : manifest.commands) { m_cmdReg(c.name, c.description); } }

		if (!manifest.initScript.empty() && m_scriptRunner)
		{
			pkg.initialized = m_scriptRunner(pkgPath + "/" + manifest.initScript);
			if (!pkg.initialized) { m_lastError = "Init script failed"; }
		}
		else { pkg.initialized = true; }

		m_idMap[manifest.id] = handle;
		m_packages.push_back(std::move(pkg));
		if (m_lastError.empty() || pkg.initialized) { m_lastError.clear(); }
		return handle;
	}

	/// @brief パッケージをアンロードする
	bool unloadPackage(PackageHandle handle)
	{
		auto it = std::find_if(m_packages.begin(), m_packages.end(),
			[handle](const Loaded& p) { return p.handle == handle; });
		if (it == m_packages.end())
		{
			m_lastError = "Not found: " + std::to_string(handle);
			return false;
		}
		const auto& tid = it->info.id;
		for (const auto& pkg : m_packages)
		{
			if (pkg.handle == handle) { continue; }
			for (const auto& d : pkg.info.dependencies)
			{
				if (d == tid)
				{
					m_lastError = "Cannot unload: " + tid + " required by " + pkg.info.id;
					return false;
				}
			}
		}
		m_idMap.erase(it->info.id);
		m_packages.erase(it);
		m_lastError.clear();
		return true;
	}

	/// @brief ロード済みパッケージ一覧を取得する
	[[nodiscard]] std::vector<PackageInfo> listLoaded() const
	{
		std::vector<PackageInfo> r;
		r.reserve(m_packages.size());
		for (const auto& p : m_packages) { r.push_back(p.info); }
		return r;
	}

	[[nodiscard]] bool isLoaded(std::string_view id) const
	{
		return m_idMap.find(std::string{id}) != m_idMap.end();
	}

	[[nodiscard]] const PackageInfo& getPackageInfo(PackageHandle handle) const
	{
		for (const auto& p : m_packages) { if (p.handle == handle) { return p.info; } }
		static const PackageInfo empty{};
		return empty;
	}

	[[nodiscard]] std::size_t packageCount() const noexcept { return m_packages.size(); }
	[[nodiscard]] const std::string& lastError() const noexcept { return m_lastError; }

private:
	struct Manifest
	{
		std::string id, name, version, initScript;
		std::vector<std::string> dependencies, headers;
		std::vector<PackageCommand> commands;
	};

	/// @brief package.json を簡易パースする
	[[nodiscard]] static Manifest parseManifest(const std::string& path)
	{
		Manifest m;
		std::ifstream file(path);
		if (!file.is_open()) { return m; }
		const std::string json{std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>()};

		m.id = strVal(json, "id");
		m.name = strVal(json, "name");
		m.version = strVal(json, "version");
		m.initScript = strVal(json, "initScript");
		m.dependencies = strArr(json, "dependencies");
		m.headers = strArr(json, "headers");
		m.commands = parseCommands(json);
		return m;
	}

	/// @brief JSON文字列から "key": "value" を抽出する
	[[nodiscard]] static std::string strVal(const std::string& json, const std::string& key)
	{
		const auto pat = "\"" + key + "\"";
		auto pos = json.find(pat);
		if (pos == std::string::npos) { return {}; }
		pos = json.find(':', pos + pat.size());
		if (pos == std::string::npos) { return {}; }
		pos = json.find('"', pos + 1);
		if (pos == std::string::npos) { return {}; }
		auto end = json.find('"', pos + 1);
		if (end == std::string::npos) { return {}; }
		return json.substr(pos + 1, end - pos - 1);
	}

	/// @brief JSON文字列から "key": ["a", "b"] を抽出する
	[[nodiscard]] static std::vector<std::string> strArr(const std::string& json, const std::string& key)
	{
		std::vector<std::string> result;
		auto pos = json.find("\"" + key + "\"");
		if (pos == std::string::npos) { return result; }
		pos = json.find('[', pos);
		if (pos == std::string::npos) { return result; }
		auto end = json.find(']', pos);
		if (end == std::string::npos) { return result; }
		auto sub = json.substr(pos + 1, end - pos - 1);
		std::size_t s = 0;
		while (s < sub.size())
		{
			auto q1 = sub.find('"', s);
			if (q1 == std::string::npos) { break; }
			auto q2 = sub.find('"', q1 + 1);
			if (q2 == std::string::npos) { break; }
			result.push_back(sub.substr(q1 + 1, q2 - q1 - 1));
			s = q2 + 1;
		}
		return result;
	}

	/// @brief commands 配列を抽出する
	[[nodiscard]] static std::vector<PackageCommand> parseCommands(const std::string& json)
	{
		std::vector<PackageCommand> result;
		auto pos = json.find("\"commands\"");
		if (pos == std::string::npos) { return result; }
		pos = json.find('[', pos);
		if (pos == std::string::npos) { return result; }
		std::size_t s = pos + 1;
		while (s < json.size())
		{
			auto ob = json.find('{', s);
			if (ob == std::string::npos) { break; }
			auto oe = json.find('}', ob);
			if (oe == std::string::npos) { break; }
			auto obj = json.substr(ob, oe - ob + 1);
			PackageCommand cmd;
			cmd.name = strVal(obj, "name");
			cmd.description = strVal(obj, "description");
			if (!cmd.name.empty()) { result.push_back(std::move(cmd)); }
			s = oe + 1;
			auto nb = json.find(']', s);
			auto nc = json.find('{', s);
			if (nb != std::string::npos && (nc == std::string::npos || nb < nc)) { break; }
		}
		return result;
	}
};

} // namespace mitiru::resource
