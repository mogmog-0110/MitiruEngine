#pragma once

/// @file Game.hpp
/// @brief ゲームインターフェース定義
/// @details Mitiruエンジンのコアとなるゲーム抽象インターフェース。
///          ユーザーはこのクラスを継承して update / draw / layout を実装する。

#include <string>

#include <mitiru/core/SceneDocument.hpp>
// IDevice.hpp は ABI安定のため Game基底からは除外（EditorApp側で直接管理）
#include <mitiru/input/InputState.hpp>
#include <mitiru/render/IRenderer3D.hpp>

namespace mitiru
{

/// @brief Engine 前方宣言 (Game から弱参照する)
class Engine;

/// @brief 2D整数サイズ
struct Size
{
	int width = 0;   ///< 幅（ピクセル）
	int height = 0;  ///< 高さ（ピクセル）

	/// @brief 等価比較
	[[nodiscard]] constexpr bool operator==(const Size&) const noexcept = default;
};

/// @brief 描画サーフェス（前方宣言）
class Screen;

/// @brief ゲームの抽象インターフェース
/// @details エンジンが毎フレーム呼び出す3つの純粋仮想関数を定義する。
///          - update(): ゲームロジックの更新
///          - draw(): 描画処理
///          - layout(): ウィンドウサイズ変更時の論理サイズ決定
class Game
{
public:
	/// @brief 仮想デストラクタ
	virtual ~Game() = default;

	/// @brief ゲームループ終了直後に呼ばれる後処理フック
	/// @details Engine が CEF を shutdown する前にここで現在シーンを exit() する。
	///          デフォルト実装は no-op。終了時処理が必要なゲームは override して
	///          現在シーンの exit() を呼び出すこと。
	virtual void onExit() {}

	/// @brief ゲームロジックを1フレーム分更新する
	/// @param dt 前フレームからの経過時間（秒）
	virtual void update(float dt) = 0;

	/// @brief 描画処理を実行する
	/// @param screen 描画先サーフェス
	virtual void draw(Screen& screen) = 0;

	/// @brief ウィンドウの論理サイズを決定する
	/// @param outsideWidth 外部ウィンドウの幅（ピクセル）
	/// @param outsideHeight 外部ウィンドウの高さ（ピクセル）
	/// @return ゲームが要求する論理サイズ
	[[nodiscard]] virtual Size layout(int outsideWidth, int outsideHeight) = 0;

	/// @brief エンジンがフレーム開始前に呼び出し、入力状態ポインタを設定する
	void setInputState(const InputState* input) noexcept { m_engineInput = input; }

	/// @brief エンジンが初期化後に呼び出し、3Dレンダラーポインタを設定する
	void setRenderer3D(render::IRenderer3D* renderer) noexcept { m_engineRenderer3D = renderer; }

	/// @brief エンジンが初期化後に呼び出し、Engine 自身への参照を設定する
	void setEngine(Engine* engine) noexcept { m_engine = engine; }

	/// @brief Engine への参照 (初期化後に有効)
	[[nodiscard]] Engine* engine() noexcept { return m_engine; }
	[[nodiscard]] const Engine* engine() const noexcept { return m_engine; }

	/// @brief シーンへの参照（コードとエディタの共有データ）
	[[nodiscard]] Scene& scene() noexcept { return m_scene; }
	[[nodiscard]] const Scene& scene() const noexcept { return m_scene; }

	/// @brief シーンをJSONファイルからロードする
	/// @param path ファイルパス
	/// @return ロード成功時 true
	bool loadScene(const std::string& path) { return m_scene.loadFromFile(path); }

	/// @brief シーンをJSONファイルに保存する
	/// @param path ファイルパス
	/// @return 保存成功時 true
	bool saveScene(const std::string& path) const { return m_scene.saveToFile(path); }

protected:
	/// @brief サブクラスから入力状態にアクセスする
	[[nodiscard]] const InputState& input() const noexcept {
		static const InputState empty;
		return m_engineInput ? *m_engineInput : empty;
	}

	/// @brief 入力状態が設定されているか
	[[nodiscard]] bool hasInput() const noexcept { return m_engineInput != nullptr; }

	/// @brief サブクラスから3Dレンダラーにアクセスする
	/// @return IRenderer3Dへのポインタ（GPU非対応時はnullptr）
	[[nodiscard]] render::IRenderer3D* renderer3D() noexcept { return m_engineRenderer3D; }
	[[nodiscard]] const render::IRenderer3D* renderer3D() const noexcept { return m_engineRenderer3D; }

	/// @brief 3Dレンダラーが使用可能か
	[[nodiscard]] bool hasRenderer3D() const noexcept {
		return m_engineRenderer3D && m_engineRenderer3D->isInitialized();
	}

private:
	const InputState* m_engineInput = nullptr;
	render::IRenderer3D* m_engineRenderer3D = nullptr;
	Engine* m_engine = nullptr;       ///< Engine 自身への弱参照
	Scene m_scene;                    ///< シーン（コードとエディタで共有）
};

} // namespace mitiru
