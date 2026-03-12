#pragma once

/// @file Game.hpp
/// @brief ゲームインターフェース定義
/// @details Mitiruエンジンのコアとなるゲーム抽象インターフェース。
///          ユーザーはこのクラスを継承して update / draw / layout を実装する。

namespace mitiru
{

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
};

} // namespace mitiru
