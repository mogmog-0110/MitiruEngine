#pragma once

/// @file SceneDocument.hpp
/// @brief Node + Trait ベースのシーンドキュメント
/// @details コード（C++ゲーム）とエディタGUIの両方が共有するシーンデータ形式。
///          各ノードはTransformを持ち、Traitの合成でメッシュ・ライト・物理等の機能を付与する。
///          JSON形式でのシリアライズ/デシリアライズ、Undo用スナップショットを提供する。
///
/// @code
/// mitiru::Scene scene;
/// int player = scene.addMesh("Player", "models/player.obj");
/// auto* node = scene.getNode(player);
/// node->addTrait<mitiru::PhysicsTrait>();
/// scene.saveToFile("scene.json");
/// @endcode
///
/// @note JSON のパース／生成は nlohmann/json に委譲する。Trait ごとの
///       toJson()/fromJson(string) シグネチャは外部から保たれており、
///       内部のみ nlohmann::json オブジェクトを経由してエスケープ・
///       数値変換を一元化している（手書き parser/writer の対称性バグ
///       回避が目的）。
///
/// @note Trait 層 (ITrait + 標準 Trait 群 + JSON helper) は 800 行ルールで
///       core/detail/SceneDocument_Traits.hpp に分離されている。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/core/detail/SceneDocument_Traits.hpp>

namespace mitiru
{

// =============================================================================
// Node — 世界に存在するもの
// =============================================================================

/// @brief シーンノード
/// @details Transform（全ノード共通）とTrait（合成可能な機能）を持つ。
///          旧SceneNodeDataとの後方互換のため、type/meshPath等のアクセサを提供する。
struct Node
{
	int id = -1;           ///< ノード固有ID
	std::string name;      ///< ノード名
	int parentId = -1;     ///< 親ノードID（ルートの場合 -1）

	// Transform（全Nodeが持つ）
	float position[3] = {0.0f, 0.0f, 0.0f}; ///< ローカル座標
	float rotation[3] = {0.0f, 0.0f, 0.0f}; ///< オイラー角（度）
	float scale[3] = {1.0f, 1.0f, 1.0f};    ///< スケール

	// エディタ制御
	bool visible = true;  ///< 非表示フラグ（Hキーで切替）
	bool locked = false;  ///< ロックフラグ（Ctrl+Lで切替、ギズモ操作不可）

	// Traits（合成可能な機能）
	std::vector<std::shared_ptr<ITrait>> traits; ///< Traitリスト

	// ── Traitアクセス ──

	/// @brief 指定型のTraitを取得する
	/// @return Traitへのポインタ（存在しない場合 nullptr）
	template<typename T>
	[[nodiscard]] T* getTrait()
	{
		for (auto& t : traits)
		{
			if (auto* p = dynamic_cast<T*>(t.get()))
			{
				return p;
			}
		}
		return nullptr;
	}

	/// @brief 指定型のTraitを取得する（const版）
	template<typename T>
	[[nodiscard]] const T* getTrait() const
	{
		for (const auto& t : traits)
		{
			if (const auto* p = dynamic_cast<const T*>(t.get()))
			{
				return p;
			}
		}
		return nullptr;
	}

	/// @brief Traitを追加する
	/// @return 追加されたTraitへの参照
	template<typename T, typename... Args>
	T& addTrait(Args&&... args)
	{
		auto trait = std::make_shared<T>(std::forward<Args>(args)...);
		auto* ptr = trait.get();
		traits.push_back(std::move(trait));
		return *ptr;
	}

	/// @brief 指定型のTraitを持つか
	template<typename T>
	[[nodiscard]] bool hasTrait() const
	{
		return getTrait<T>() != nullptr;
	}

	/// @brief Trait種別名で削除する
	void removeTrait(const std::string& traitTypeName)
	{
		traits.erase(
			std::remove_if(traits.begin(), traits.end(),
				[&](const auto& t) { return t->traitType() == traitTypeName; }),
			traits.end());
	}

	// ── 後方互換アクセサ（旧SceneNodeDataとの互換性） ──

	/// @brief ノード種別を返す（Traitから推定）
	/// @details mesh/light/camera Traitがあればその種別、なければ "empty"
	[[nodiscard]] std::string type() const
	{
		if (hasTrait<MeshTrait>())   return "mesh";
		if (hasTrait<LightTrait>())  return "light";
		if (hasTrait<CameraTrait>()) return "camera";
		return "empty";
	}

	/// @brief メッシュパスへのアクセス（後方互換）
	[[nodiscard]] std::string meshPath() const
	{
		if (const auto* m = getTrait<MeshTrait>()) return m->meshPath;
		return "";
	}

	/// @brief マテリアル名へのアクセス（後方互換）
	[[nodiscard]] std::string materialName() const
	{
		if (const auto* m = getTrait<MeshTrait>()) return m->materialName;
		return "";
	}

	/// @brief ライト種別へのアクセス（後方互換）
	[[nodiscard]] std::string lightType() const
	{
		if (const auto* l = getTrait<LightTrait>()) return l->lightType;
		return "";
	}

	/// @brief ライト色へのアクセス（後方互換）
	/// @note 書き込みにはgetTrait<LightTrait>()を使用すること
	[[nodiscard]] const float* lightColor() const
	{
		if (const auto* l = getTrait<LightTrait>()) return l->color;
		static const float white[3] = {1.0f, 1.0f, 1.0f};
		return white;
	}

	/// @brief ライト色への書き込みアクセス（後方互換）
	[[nodiscard]] float* lightColor()
	{
		if (auto* l = getTrait<LightTrait>()) return l->color;
		return nullptr;
	}

	/// @brief ライト強度への参照（後方互換）
	[[nodiscard]] float lightIntensity() const
	{
		if (const auto* l = getTrait<LightTrait>()) return l->intensity;
		return 1.0f;
	}

	/// @brief カスタムプロパティへのアクセス（後方互換）
	[[nodiscard]] const std::map<std::string, std::string>& properties() const
	{
		if (const auto* c = getTrait<CustomTrait>()) return c->properties;
		static const std::map<std::string, std::string> empty;
		return empty;
	}

	/// @brief カスタムプロパティへの書き込みアクセス（後方互換）
	[[nodiscard]] std::map<std::string, std::string>& propertiesMut()
	{
		auto* c = getTrait<CustomTrait>();
		if (!c)
		{
			c = &addTrait<CustomTrait>();
		}
		return c->properties;
	}
};

// =============================================================================
// Scene — Nodeの集合
// =============================================================================

/// @brief 統一シーンドキュメント
/// @details コードとエディタが同一のAPIでシーンを操作するための中心クラス。
///          ノードの追加・削除・検索、JSON形式でのファイル入出力、
///          ダーティ追跡、Undo用スナップショットを提供する。
class Scene
{
public:
	// ── ノード管理 ──

	/// @brief 名前を指定してノードを追加する
	/// @param name ノード名
	/// @param parent 親ノードID（デフォルト: ルート）
	/// @return 割り当てられたノードID
	int addNode(const std::string& name, int parent = -1)
	{
		Node node;
		node.id = m_nextId++;
		node.name = name;
		node.parentId = parent;
		m_nodes.push_back(std::move(node));
		m_dirty = true;
		return m_nodes.back().id;
	}

	/// @brief 既存のNodeオブジェクトを追加する
	/// @param node ノードデータ（idは自動割り当て）
	/// @return 割り当てられたノードID
	int addNode(const Node& node)
	{
		Node added = node;
		added.id = m_nextId++;
		m_nodes.push_back(std::move(added));
		m_dirty = true;
		return m_nodes.back().id;
	}

	/// @brief ノードを削除する
	/// @param id 削除対象のノードID
	void removeNode(int id)
	{
		auto it = std::remove_if(m_nodes.begin(), m_nodes.end(),
			[id](const Node& n) { return n.id == id; });
		if (it != m_nodes.end())
		{
			m_nodes.erase(it, m_nodes.end());
			m_dirty = true;
		}
	}

	/// @brief IDでノードを取得する
	[[nodiscard]] Node* getNode(int id)
	{
		for (auto& node : m_nodes)
		{
			if (node.id == id) return &node;
		}
		return nullptr;
	}

	/// @brief IDでノードを取得する（const版）
	[[nodiscard]] const Node* getNode(int id) const
	{
		for (const auto& node : m_nodes)
		{
			if (node.id == id) return &node;
		}
		return nullptr;
	}

	/// @brief 全ノードを取得する
	[[nodiscard]] const std::vector<Node>& nodes() const noexcept
	{
		return m_nodes;
	}

	/// @brief ノード数を取得する
	[[nodiscard]] std::size_t nodeCount() const noexcept
	{
		return m_nodes.size();
	}

	// ── 検索 ──

	/// @brief 名前でノードを検索する
	[[nodiscard]] int findByName(const std::string& name) const
	{
		for (const auto& node : m_nodes)
		{
			if (node.name == name) return node.id;
		}
		return -1;
	}

	/// @brief Trait種別名でノードを検索する
	[[nodiscard]] std::vector<int> findByTrait(const std::string& traitTypeName) const
	{
		std::vector<int> result;
		for (const auto& node : m_nodes)
		{
			for (const auto& t : node.traits)
			{
				if (t->traitType() == traitTypeName)
				{
					result.push_back(node.id);
					break;
				}
			}
		}
		return result;
	}

	/// @brief ノード種別（type()推定値）で検索する（後方互換）
	[[nodiscard]] std::vector<int> findByType(const std::string& typeName) const
	{
		std::vector<int> result;
		for (const auto& node : m_nodes)
		{
			if (node.type() == typeName)
			{
				result.push_back(node.id);
			}
		}
		return result;
	}

	/// @brief ルートノードのIDリストを取得する
	[[nodiscard]] std::vector<int> rootNodes() const
	{
		std::vector<int> roots;
		for (const auto& node : m_nodes)
		{
			if (node.parentId == -1) roots.push_back(node.id);
		}
		return roots;
	}

	/// @brief 指定親の子ノードIDリストを取得する
	[[nodiscard]] std::vector<int> children(int parentId) const
	{
		std::vector<int> result;
		for (const auto& node : m_nodes)
		{
			if (node.parentId == parentId) result.push_back(node.id);
		}
		return result;
	}

	/// @brief ノードを複製する（名前に "_copy" を付加）
	/// @param id 複製元ノードID
	/// @return 新しいノードID（-1で失敗）
	int duplicateNode(int id)
	{
		const auto* src = getNode(id);
		if (!src) { return -1; }
		const int newId = addNode(src->name + "_copy", src->parentId);
		auto* dst = getNode(newId);
		if (!dst) { return -1; }
		std::memcpy(dst->position, src->position, sizeof(float) * 3);
		std::memcpy(dst->rotation, src->rotation, sizeof(float) * 3);
		std::memcpy(dst->scale, src->scale, sizeof(float) * 3);
		dst->visible = src->visible;
		// Traitのシャローコピー（共有参照）
		dst->traits = src->traits;
		return newId;
	}

	// ── 便利ファクトリ（Trait付きノード作成） ──

	/// @brief 空ノードを作成する
	int createEmpty(const std::string& name, int parent = -1)
	{
		return addNode(name, parent);
	}

	/// @brief メッシュノードを作成する
	int addMesh(const std::string& name, const std::string& meshPath, int parent = -1)
	{
		int id = addNode(name, parent);
		auto* node = getNode(id);
		auto& mesh = node->addTrait<MeshTrait>();
		mesh.meshPath = meshPath;
		return id;
	}

	/// @brief ライトノードを作成する
	int addLight(const std::string& name, const std::string& lt = "directional", int parent = -1)
	{
		int id = addNode(name, parent);
		auto* node = getNode(id);
		auto& light = node->addTrait<LightTrait>();
		light.lightType = lt;
		return id;
	}

	/// @brief カメラノードを作成する
	int addCamera(const std::string& name, int parent = -1)
	{
		int id = addNode(name, parent);
		auto* node = getNode(id);
		node->addTrait<CameraTrait>();
		return id;
	}

	/// @brief 旧API互換: createMesh
	int createMesh(const std::string& name, const std::string& meshPath, int parent = -1)
	{
		return addMesh(name, meshPath, parent);
	}

	/// @brief 旧API互換: createLight
	int createLight(const std::string& name, const std::string& lt, int parent = -1)
	{
		return addLight(name, lt, parent);
	}

	/// @brief 旧API互換: createCamera
	int createCamera(const std::string& name, int parent = -1)
	{
		return addCamera(name, parent);
	}

	// ── シリアライゼーション ──

	/// @brief シーンをJSON文字列にシリアライズする
	/// @details nlohmann/json で組み立て、2 スペースインデントの pretty-print
	///          を返す。Trait は各 `ITrait::toJson()` が返した文字列を
	///          再パースして埋め込む（compact JSON）。
	[[nodiscard]] std::string toJson() const
	{
		nlohmann::json doc;
		doc["nodes"] = nlohmann::json::array();
		for (const auto& n : m_nodes)
		{
			nlohmann::json node;
			node["id"] = n.id;
			node["name"] = n.name;
			node["parentId"] = n.parentId;
			node["position"] = detail::floatArray(n.position, 3);
			node["rotation"] = detail::floatArray(n.rotation, 3);
			node["scale"] = detail::floatArray(n.scale, 3);
			if (!n.traits.empty())
			{
				node["traits"] = nlohmann::json::array();
				for (const auto& t : n.traits)
				{
					// 各 trait の toJson() は文字列を返すので再パースして埋める。
					// trait API シグネチャ（string toJson / void fromJson(string)）
					// を保つための橋渡し。
					auto inner = nlohmann::json::parse(
						t->toJson(), nullptr, /*allow_exceptions=*/false);
					if (inner.is_discarded()) inner = nlohmann::json::object();
					node["traits"].push_back(std::move(inner));
				}
			}
			doc["nodes"].push_back(std::move(node));
		}
		return doc.dump(2);
	}

	/// @brief JSON文字列からシーンをデシリアライズする
	/// @details 失敗時 (パースエラー or "nodes" 配列なし) は false を返し
	///          内部状態を空にリセットする。旧形式 (`"type": "mesh"` 等を
	///          ノード直下に持つレイアウト) との後方互換もここで吸収する。
	bool fromJson(const std::string& json)
	{
		m_nodes.clear();
		m_nextId = 0;

		auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
		if (doc.is_discarded() || !doc.is_object()) return false;

		auto nodesIt = doc.find("nodes");
		if (nodesIt == doc.end() || !nodesIt->is_array()) return false;

		for (const auto& nj : *nodesIt)
		{
			if (!nj.is_object()) continue;
			Node node;
			node.id = nj.value("id", -1);
			node.name = nj.value("name", std::string{});
			node.parentId = nj.value("parentId", -1);

			detail::readFloatArray(nj, "position", node.position, 3);
			detail::readFloatArray(nj, "rotation", node.rotation, 3);
			detail::readFloatArray(nj, "scale", node.scale, 3);

			// 新形式: traits 配列
			auto tit = nj.find("traits");
			if (tit != nj.end() && tit->is_array())
			{
				for (const auto& tj : *tit)
				{
					if (!tj.is_object()) continue;
					parseTrait(tj, node);
				}
			}

			// 旧形式: ノード直下の "type": "mesh"/"light"/"camera"
			if (node.traits.empty())
			{
				const auto oldType = nj.value("type", std::string{"empty"});
				if (oldType == "mesh")
				{
					auto& mesh = node.addTrait<MeshTrait>();
					mesh.meshPath = nj.value("meshPath", std::string{});
					mesh.materialName = nj.value("materialName", std::string{});
				}
				else if (oldType == "light")
				{
					auto& light = node.addTrait<LightTrait>();
					light.lightType = nj.value("lightType", std::string{"directional"});
					detail::readFloatArray(nj, "lightColor", light.color, 3);
					light.intensity = nj.value("lightIntensity", 1.0f);
				}
				else if (oldType == "camera")
				{
					node.addTrait<CameraTrait>();
				}
			}

			if (node.id >= m_nextId) m_nextId = node.id + 1;
			m_nodes.push_back(std::move(node));
		}

		m_dirty = false;
		return true;
	}

	/// @brief シーンをJSONファイルに保存する
	bool saveToFile(const std::string& path) const
	{
		std::ofstream ofs(path);
		if (!ofs.is_open()) return false;
		ofs << toJson();
		return ofs.good();
	}

	/// @brief JSONファイルからシーンをロードする
	bool loadFromFile(const std::string& path)
	{
		std::ifstream ifs(path);
		if (!ifs.is_open()) return false;
		std::ostringstream ss;
		ss << ifs.rdbuf();
		return fromJson(ss.str());
	}

	// ── ダーティ追跡 ──

	[[nodiscard]] bool isDirty() const noexcept { return m_dirty; }
	void clearDirty() noexcept { m_dirty = false; }

	// ── Undoサポート ──

	[[nodiscard]] std::string snapshot() const { return toJson(); }

	void restore(const std::string& snap)
	{
		fromJson(snap);
		m_dirty = true;
	}

private:
	std::vector<Node> m_nodes; ///< ノードリスト
	int m_nextId = 0;          ///< 次に割り当てるノードID
	bool m_dirty = false;      ///< 変更フラグ

	/// @brief trait オブジェクトをパースして node に追加する
	/// @details 各 ITrait::fromJson(string) を再利用するため、ここで
	///          一旦 dump() して trait に渡す。trait 個別の数値・配列
	///          パースを重複実装しないための橋渡し。
	static void parseTrait(const nlohmann::json& tj, Node& node)
	{
		const auto type = tj.value("type", std::string{});
		const auto payload = tj.dump();

		if (type == "mesh")
		{
			auto& t = node.addTrait<MeshTrait>();
			t.fromJson(payload);
		}
		else if (type == "light")
		{
			auto& t = node.addTrait<LightTrait>();
			t.fromJson(payload);
		}
		else if (type == "camera")
		{
			auto& t = node.addTrait<CameraTrait>();
			t.fromJson(payload);
		}
		else if (type == "physics")
		{
			auto& t = node.addTrait<PhysicsTrait>();
			t.fromJson(payload);
		}
		else if (type == "script")
		{
			auto& t = node.addTrait<ScriptTrait>();
			t.fromJson(payload);
		}
		else if (type == "audio")
		{
			auto& t = node.addTrait<AudioTrait>();
			t.fromJson(payload);
		}
		else
		{
			auto& t = node.addTrait<CustomTrait>();
			t.customType = type;
			t.fromJson(payload);
		}
	}
};

// =============================================================================
// 後方互換エイリアス
// =============================================================================

/// @brief 旧名との互換エイリアス
using SceneDocument = Scene;

/// @brief 旧名との互換エイリアス
using SceneNodeData = Node;

} // namespace mitiru
