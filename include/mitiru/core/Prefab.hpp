#pragma once

/// @file Prefab.hpp
/// @brief Prefabシステム — 再利用可能なNodeサブツリーテンプレート
/// @details PrefabDef はノードサブツリーのスナップショットを保持し、
///          PrefabLibrary は登録・検索・インスタンス化・JSON入出力を提供する。
///
/// @code
/// mitiru::PrefabLibrary lib;
/// lib.createFromNode(scene, rootNodeId, "Enemy_Slime", "Characters");
/// int id = lib.instantiate(scene, "Enemy_Slime");
/// @endcode

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <mitiru/core/SceneDocument.hpp>

namespace mitiru
{

// =============================================================================
// PrefabDef — 再利用可能なノードテンプレート
// =============================================================================

/// @brief Prefab定義 — Nodeサブツリーのテンプレート
/// @details nodes[0] がルートノード。子ノードは parentId で相対的に参照する。
///          parentId は Prefab 内のローカルインデックス（0ベース）で管理される。
struct PrefabDef
{
	std::string name;         ///< Prefab名（例: "Enemy_Slime"）
	std::string category;     ///< カテゴリ（例: "Characters"）
	std::string description;  ///< 説明文
	std::vector<Node> nodes;  ///< ノードサブツリー（nodes[0]がルート）

	/// @brief Prefab内のノード数を返す
	[[nodiscard]] std::size_t nodeCount() const noexcept
	{
		return nodes.size();
	}

	/// @brief 空のPrefabか判定する
	[[nodiscard]] bool empty() const noexcept
	{
		return nodes.empty();
	}
};

// =============================================================================
// PrefabLibrary — Prefabの管理・インスタンス化
// =============================================================================

/// @brief Prefabライブラリ — Prefabの登録・検索・インスタンス化を管理する
class PrefabLibrary
{
public:
	// ── 登録 ──

	/// @brief PrefabDefを直接登録する
	/// @param def 登録するPrefab定義
	void registerPrefab(PrefabDef def)
	{
		const auto name = def.name;
		m_prefabs[name] = std::move(def);
	}

	/// @brief シーン内のノードからPrefabを作成して登録する
	/// @param scene 元のシーン
	/// @param nodeId ルートにするノードID
	/// @param name Prefab名
	/// @param category カテゴリ（省略可）
	void createFromNode(const Scene& scene, int nodeId,
	                    const std::string& name,
	                    const std::string& category = "")
	{
		const auto* root = scene.getNode(nodeId);
		if (!root) return;

		PrefabDef def;
		def.name = name;
		def.category = category;

		// ルートノードとその子孫を収集する
		std::vector<int> collected;
		collectSubtree(scene, nodeId, collected);

		// IDマッピング: sceneID -> prefab localIndex
		std::map<int, int> idMap;
		for (std::size_t i = 0; i < collected.size(); ++i)
		{
			idMap[collected[i]] = static_cast<int>(i);
		}

		for (std::size_t i = 0; i < collected.size(); ++i)
		{
			const auto* src = scene.getNode(collected[i]);
			if (!src) continue;

			Node copy = deepCopyNode(*src);
			// ルートノードの parentId は -1（Prefab内のルート）
			if (i == 0)
			{
				copy.parentId = -1;
			}
			else
			{
				auto it = idMap.find(src->parentId);
				copy.parentId = (it != idMap.end()) ? it->second : -1;
			}
			copy.id = static_cast<int>(i);
			def.nodes.push_back(std::move(copy));
		}

		m_prefabs[name] = std::move(def);
	}

	// ── インスタンス化 ──

	/// @brief Prefabをシーンにインスタンス化する
	/// @param scene インスタンス先のシーン
	/// @param prefabName Prefab名
	/// @param parentId 親ノードID（-1でルート直下）
	/// @return 生成されたルートノードのID（失敗時 -1）
	int instantiate(Scene& scene, const std::string& prefabName,
	                int parentId = -1) const
	{
		auto it = m_prefabs.find(prefabName);
		if (it == m_prefabs.end() || it->second.empty())
		{
			return -1;
		}

		const auto& def = it->second;

		// localIndex -> 実際に割り当てられたscene ID
		std::map<int, int> localToScene;
		int rootSceneId = -1;

		for (std::size_t i = 0; i < def.nodes.size(); ++i)
		{
			const auto& tmpl = def.nodes[i];
			Node copy = deepCopyNode(tmpl);

			// 親IDを解決する
			if (i == 0)
			{
				copy.parentId = parentId;
			}
			else
			{
				auto pit = localToScene.find(tmpl.parentId);
				copy.parentId = (pit != localToScene.end()) ? pit->second : parentId;
			}

			copy.id = -1; // Sceneが自動割り当て
			int newId = scene.addNode(copy);
			localToScene[static_cast<int>(i)] = newId;

			if (i == 0)
			{
				rootSceneId = newId;
			}
		}

		return rootSceneId;
	}

	// ── クエリ ──

	/// @brief Prefabが存在するか判定する
	[[nodiscard]] bool hasPrefab(const std::string& name) const
	{
		return m_prefabs.find(name) != m_prefabs.end();
	}

	/// @brief Prefab定義を取得する
	/// @return 見つからなければ nullptr
	[[nodiscard]] const PrefabDef* getPrefab(const std::string& name) const
	{
		auto it = m_prefabs.find(name);
		return (it != m_prefabs.end()) ? &it->second : nullptr;
	}

	/// @brief 登録済みPrefab名の一覧を返す
	[[nodiscard]] std::vector<std::string> prefabNames() const
	{
		std::vector<std::string> result;
		result.reserve(m_prefabs.size());
		for (const auto& [name, _] : m_prefabs)
		{
			result.push_back(name);
		}
		return result;
	}

	/// @brief 指定カテゴリのPrefab名一覧を返す
	[[nodiscard]] std::vector<std::string> prefabsInCategory(
		const std::string& category) const
	{
		std::vector<std::string> result;
		for (const auto& [name, def] : m_prefabs)
		{
			if (def.category == category)
			{
				result.push_back(name);
			}
		}
		return result;
	}

	/// @brief 全カテゴリ名の一覧を返す
	[[nodiscard]] std::vector<std::string> categories() const
	{
		std::set<std::string> cats;
		for (const auto& [_, def] : m_prefabs)
		{
			if (!def.category.empty())
			{
				cats.insert(def.category);
			}
		}
		return {cats.begin(), cats.end()};
	}

	/// @brief Prefab数を返す
	[[nodiscard]] std::size_t count() const noexcept
	{
		return m_prefabs.size();
	}

	// ── 削除 ──

	/// @brief Prefabを削除する
	void removePrefab(const std::string& name)
	{
		m_prefabs.erase(name);
	}

	/// @brief 全Prefabを削除する
	void clear()
	{
		m_prefabs.clear();
	}

	// ── JSON入出力 ──

	/// @brief 指定Prefabを簡易JSON文字列にシリアライズする
	[[nodiscard]] std::string toJson(const std::string& prefabName) const
	{
		auto it = m_prefabs.find(prefabName);
		if (it == m_prefabs.end()) return "{}";
		return serializePrefab(it->second);
	}

	/// @brief 全PrefabをJSON文字列にシリアライズする
	[[nodiscard]] std::string toJsonAll() const
	{
		std::ostringstream ss;
		ss << "{\n  \"prefabs\": [\n";
		bool first = true;
		for (const auto& [_, def] : m_prefabs)
		{
			if (!first) ss << ",\n";
			ss << "    " << serializePrefab(def);
			first = false;
		}
		ss << "\n  ]\n}";
		return ss.str();
	}

	/// @brief JSONファイルに保存する
	bool saveToFile(const std::string& prefabName,
	                const std::string& path) const
	{
		std::ofstream ofs(path);
		if (!ofs.is_open()) return false;
		ofs << toJson(prefabName);
		return ofs.good();
	}

	/// @brief 全Prefabをディレクトリに保存する
	void saveAllToFile(const std::string& dirPath) const
	{
		for (const auto& [name, _] : m_prefabs)
		{
			const auto path = dirPath + "/" + name + ".prefab.json";
			saveToFile(name, path);
		}
	}

	/// @brief JSON文字列からPrefabをロードする（簡易実装）
	/// @note フル実装にはJSONパーサーが必要。ここでは名前とカテゴリのみ復元。
	void loadFromJson(const std::string& json)
	{
		PrefabDef def;
		def.name = readStr(json, "name");
		def.category = readStr(json, "category");
		def.description = readStr(json, "description");
		if (!def.name.empty())
		{
			m_prefabs[def.name] = std::move(def);
		}
	}

	/// @brief JSONファイルからPrefabをロードする
	void loadFromFile(const std::string& path)
	{
		std::ifstream ifs(path);
		if (!ifs.is_open()) return;
		std::ostringstream ss;
		ss << ifs.rdbuf();
		loadFromJson(ss.str());
	}

private:
	std::map<std::string, PrefabDef> m_prefabs; ///< 名前 -> Prefab定義

	// ── ヘルパー ──

	/// @brief ノードのサブツリーを再帰的に収集する
	static void collectSubtree(const Scene& scene, int nodeId,
	                           std::vector<int>& out)
	{
		out.push_back(nodeId);
		for (int childId : scene.children(nodeId))
		{
			collectSubtree(scene, childId, out);
		}
	}

	/// @brief ノードのディープコピーを作成する（Traitを含む）
	static Node deepCopyNode(const Node& src)
	{
		Node copy;
		copy.id = src.id;
		copy.name = src.name;
		copy.parentId = src.parentId;
		for (int i = 0; i < 3; ++i)
		{
			copy.position[i] = src.position[i];
			copy.rotation[i] = src.rotation[i];
			copy.scale[i] = src.scale[i];
		}
		// Traitをシリアライズ→デシリアライズでコピーする
		for (const auto& trait : src.traits)
		{
			if (!trait) continue;
			auto cloned = cloneTrait(trait->traitType(), trait->toJson());
			if (cloned) copy.traits.push_back(std::move(cloned));
		}
		return copy;
	}

	/// @brief Trait種別名とJSONからTraitを複製する
	static std::shared_ptr<ITrait> cloneTrait(const std::string& type,
	                                          const std::string& json)
	{
		std::shared_ptr<ITrait> t;
		if      (type == "mesh")    t = std::make_shared<MeshTrait>();
		else if (type == "light")   t = std::make_shared<LightTrait>();
		else if (type == "camera")  t = std::make_shared<CameraTrait>();
		else if (type == "physics") t = std::make_shared<PhysicsTrait>();
		else if (type == "script")  t = std::make_shared<ScriptTrait>();
		else if (type == "audio")   t = std::make_shared<AudioTrait>();
		else                        t = std::make_shared<CustomTrait>();
		t->fromJson(json);
		return t;
	}

	/// @brief Prefab定義をJSON文字列にシリアライズする
	static std::string serializePrefab(const PrefabDef& def)
	{
		std::ostringstream ss;
		ss << "{ \"name\": " << esc(def.name);
		ss << ", \"category\": " << esc(def.category);
		ss << ", \"description\": " << esc(def.description);
		ss << ", \"nodeCount\": " << def.nodes.size();
		ss << " }";
		return ss.str();
	}

	/// @brief 文字列をJSONエスケープする
	static std::string esc(const std::string& s)
	{
		std::string r = "\"";
		for (char c : s)
		{
			switch (c)
			{
			case '"':  r += "\\\""; break;
			case '\\': r += "\\\\"; break;
			case '\n': r += "\\n";  break;
			default:   r += c;      break;
			}
		}
		r += '"';
		return r;
	}

	/// @brief JSON文字列から文字列値を読み取る
	static std::string readStr(const std::string& json, const std::string& key)
	{
		const auto k = "\"" + key + "\"";
		auto pos = json.find(k);
		if (pos == std::string::npos) return "";
		pos = json.find('"', json.find(':', pos + k.size()) + 1);
		if (pos == std::string::npos) return "";
		++pos;
		std::string result;
		while (pos < json.size() && json[pos] != '"')
		{
			if (json[pos] == '\\' && pos + 1 < json.size()) { ++pos; }
			result += json[pos++];
		}
		return result;
	}
};

} // namespace mitiru
