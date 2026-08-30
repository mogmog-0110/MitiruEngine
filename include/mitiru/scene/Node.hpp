#pragma once

/// @file Node.hpp
/// @brief Godot風ノードシステム。シーンツリーの基盤
/// @details 全ゲームオブジェクトの基底クラス。親子関係、ライフサイクル管理、
///          Update/Draw伝播を提供する。
///
/// @note Design Decision: Node と GameWorld は意図的に共存する。
/// - **Node** (このファイル): 階層シーンツリー。親子トランスフォーム伝播、
///   updateTree/drawTree による再帰的更新。UI、2Dゲーム、シーン構成に最適。
/// - **GameWorld**: ECSベースの型消去コンポーネント管理。データ指向設計、
///   大量エンティティ、forEach/システムクエリに最適。
/// - **ブリッジ**: NodeはECSエンティティをattachEntity()で保持可能。
///   GameWorldシステムはNodeコンポーネントをクエリ可能。
/// @see GameWorld.hpp
///
/// @code
/// auto root = std::make_shared<mitiru::scene::Node>("Root");
/// auto child = std::make_shared<mitiru::scene::SpriteNode>();
/// child->setName("Player");
/// child->position = {100, 200};
/// root->addChild(child);
/// root->updateTree(dt);
/// root->drawTree(screen);
/// @endcode

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>
#include <sgc/math/Rect.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::scene
{

class Node;

/// @brief ノードの共有ポインタ型
using NodePtr = std::shared_ptr<Node>;

/// @brief シーンツリーのノード基底クラス（Godot Node風）
/// @details 親子関係を持つツリー構造で、updateTree/drawTreeにより
///          再帰的にライフサイクルコールバックが呼ばれる。
class Node
{
public:
	/// @brief デフォルトコンストラクタ
	Node() = default;

	/// @brief 名前付きコンストラクタ
	/// @param name ノード名
	explicit Node(const std::string& name)
		: m_name(name)
	{
	}

	/// @brief デストラクタ
	virtual ~Node() = default;

	// ── ライフサイクル（Godot風） ──

	/// @brief ツリーに追加された直後に呼ばれる
	virtual void onReady() {}

	/// @brief 毎フレーム更新
	/// @param dt デルタタイム（秒）
	virtual void onUpdate(float dt) { static_cast<void>(dt); }

	/// @brief 描画
	/// @param screen 描画先サーフェス
	virtual void onDraw(Screen& screen) const { static_cast<void>(screen); }

	/// @brief ツリーから除去される直前に呼ばれる
	virtual void onExit() {}

	// ── 子ノード管理 ──

	/// @brief 子ノードを追加する
	/// @param child 追加するノード
	void addChild(NodePtr child)
	{
		child->m_parent = this;
		m_children.push_back(std::move(child));
		m_children.back()->onReady();
	}

	/// @brief 名前で子ノードを除去する
	/// @param name 除去するノードの名前
	void removeChild(const std::string& name)
	{
		auto it = std::find_if(m_children.begin(), m_children.end(),
			[&](const auto& c) { return c->name() == name; });
		if (it != m_children.end())
		{
			(*it)->onExit();
			(*it)->m_parent = nullptr;
			m_children.erase(it);
		}
	}

	/// @brief 名前で子ノードを検索する
	/// @param name 検索するノード名
	/// @return 見つかったノード（見つからなければnullptr）
	[[nodiscard]] NodePtr findChild(const std::string& name) const
	{
		for (const auto& c : m_children)
		{
			if (c->name() == name)
			{
				return c;
			}
		}
		return nullptr;
	}

	/// @brief 再帰的に名前でノードを検索する
	/// @param name 検索するノード名
	/// @return 見つかったノード（見つからなければnullptr）
	[[nodiscard]] NodePtr findDescendant(const std::string& name) const
	{
		for (const auto& c : m_children)
		{
			if (c->name() == name)
			{
				return c;
			}
			auto found = c->findDescendant(name);
			if (found)
			{
				return found;
			}
		}
		return nullptr;
	}

	// ── ツリー全体の更新/描画伝播 ──

	/// @brief ツリー全体を再帰的に更新する
	/// @param dt デルタタイム（秒）
	void updateTree(float dt)
	{
		if (!m_active) return;
		onUpdate(dt);
		for (auto& child : m_children)
		{
			child->updateTree(dt);
		}
	}

	/// @brief ツリー全体を再帰的に描画する
	/// @param screen 描画先サーフェス
	void drawTree(Screen& screen) const
	{
		if (!m_visible) return;
		onDraw(screen);
		for (const auto& child : m_children)
		{
			child->drawTree(screen);
		}
	}

	// ── プロパティ ──

	/// @brief ノード名を取得する
	[[nodiscard]] const std::string& name() const noexcept { return m_name; }

	/// @brief ノード名を設定する
	/// @param n 新しい名前
	void setName(const std::string& n) { m_name = n; }

	/// @brief ローカル座標
	sgc::Vec2f position{0, 0};

	/// @brief 回転角（ラジアン）
	float rotation = 0.0f;

	/// @brief スケール
	sgc::Vec2f scale{1, 1};

	/// @brief アクティブフラグ（falseならupdate伝播をスキップ）
	bool m_active = true;

	/// @brief 表示フラグ（falseならdraw伝播をスキップ）
	bool m_visible = true;

	/// @brief 親ノードを取得する
	/// @return 親ノードへのポインタ（ルートならnullptr）
	[[nodiscard]] Node* parent() const noexcept { return m_parent; }

	/// @brief 子ノード一覧を取得する
	[[nodiscard]] const std::vector<NodePtr>& children() const noexcept { return m_children; }

	/// @brief 子ノード数を取得する
	[[nodiscard]] int childCount() const noexcept { return static_cast<int>(m_children.size()); }

private:
	std::string m_name;              ///< ノード名
	Node* m_parent = nullptr;        ///< 親ノード（非所有）
	std::vector<NodePtr> m_children; ///< 子ノードリスト
};

// ── 便利ノードタイプ ──

/// @brief スプライトノード（位置+矩形描画）
/// @details 指定色の矩形を描画するシンプルなノード。
class SpriteNode : public Node
{
public:
	/// @brief 描画色
	sgc::Colorf color{1, 1, 1, 1};

	/// @brief 描画幅
	float width = 32;

	/// @brief 描画高さ
	float height = 32;

	/// @brief 矩形を描画する
	/// @param screen 描画先サーフェス
	void onDraw(Screen& screen) const override;
};

/// @brief ラベルノード（テキスト描画）
/// @details 指定テキストを描画するノード。
class LabelNode : public Node
{
public:
	/// @brief 表示テキスト
	std::string text;

	/// @brief テキスト色
	sgc::Colorf color{1, 1, 1, 1};

	/// @brief フォントサイズ
	float fontSize = 16.0f;

	/// @brief テキストを描画する
	/// @param screen 描画先サーフェス
	void onDraw(Screen& screen) const override;
};

/// @brief タイマーノード（指定時間後にコールバック）
/// @details Godot Timerノード風。開始後、duration経過でonTimeoutを呼ぶ。
class TimerNode : public Node
{
public:
	/// @brief タイマー時間（秒）
	float duration = 1.0f;

	/// @brief ワンショットモード（trueなら一度だけ発火）
	bool oneShot = true;

	/// @brief タイムアウト時のコールバック
	std::function<void()> onTimeout;

	/// @brief タイマーを開始する
	void start()
	{
		m_elapsed = 0;
		m_running = true;
	}

	/// @brief タイマーを停止する
	void stop() { m_running = false; }

	/// @brief タイマーが動作中か
	[[nodiscard]] bool isRunning() const noexcept { return m_running; }

	/// @brief 経過時間を取得する
	[[nodiscard]] float elapsed() const noexcept { return m_elapsed; }

	/// @brief 更新処理
	/// @param dt デルタタイム（秒）
	void onUpdate(float dt) override
	{
		if (!m_running) return;
		m_elapsed += dt;
		if (m_elapsed >= duration)
		{
			if (onTimeout) onTimeout();
			if (oneShot)
			{
				m_running = false;
			}
			else
			{
				m_elapsed -= duration;
			}
		}
	}

private:
	float m_elapsed = 0;   ///< 経過時間
	bool m_running = false; ///< 動作中フラグ
};

} // namespace mitiru::scene

// ── Screen依存の実装 ──
#include <mitiru/core/Screen.hpp>

inline void mitiru::scene::SpriteNode::onDraw(Screen& screen) const
{
	screen.drawRect(sgc::Rectf{position.x, position.y, width, height}, color);
}

inline void mitiru::scene::LabelNode::onDraw(Screen& screen) const
{
	screen.text(text, position.x, position.y, color, fontSize);
}
