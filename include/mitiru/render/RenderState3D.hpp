#pragma once

/// @file RenderState3D.hpp
/// @brief 3Dレンダリングステート管理
/// @details ワイヤーフレーム、カリング、深度テスト、ブレンド等の3D描画状態を定義する。
///          プリセットファクトリで一般的な描画状態を簡単に取得できる。

namespace mitiru::render
{

/// @brief カリングモード
enum class CullMode
{
	None,   ///< カリングなし（両面描画）
	Back,   ///< 背面カリング（デフォルト）
	Front   ///< 前面カリング
};

/// @brief 3D描画状態
/// @details ラスタライザ・深度・ブレンドの各種設定をまとめた構造体。
///          ファクトリメソッドで一般的なプリセットを取得できる。
///
/// @code
/// auto state = mitiru::render::RenderState3D::opaque();
/// state.wireframe = true;
///
/// if (state != mitiru::render::RenderState3D::opaque())
/// {
///     // ステート変更を検出
/// }
/// @endcode
struct RenderState3D
{
	bool wireframe = false;          ///< ワイヤーフレーム表示
	CullMode cullMode = CullMode::Back;  ///< カリングモード
	bool depthTest = true;           ///< 深度テスト有効
	bool depthWrite = true;          ///< 深度書き込み有効
	bool blendEnabled = false;       ///< ブレンド有効

	/// @brief 不透明描画用のデフォルト状態を取得する
	/// @return 背面カリング・深度テスト有効・ブレンド無効の状態
	[[nodiscard]] static constexpr RenderState3D opaque() noexcept
	{
		return RenderState3D{};
	}

	/// @brief 半透明描画用の状態を取得する
	/// @return 背面カリング・深度テスト有効・深度書き込み無効・ブレンド有効の状態
	[[nodiscard]] static constexpr RenderState3D transparent() noexcept
	{
		RenderState3D state;
		state.depthWrite = false;
		state.blendEnabled = true;
		return state;
	}

	/// @brief ワイヤーフレーム描画用の状態を取得する
	/// @return ワイヤーフレーム・カリングなし・深度テスト有効の状態
	[[nodiscard]] static constexpr RenderState3D wireframeState() noexcept
	{
		RenderState3D state;
		state.wireframe = true;
		state.cullMode = CullMode::None;
		return state;
	}

	/// @brief カリングなしの状態を取得する
	/// @return 両面描画・深度テスト有効の状態
	[[nodiscard]] static constexpr RenderState3D noCull() noexcept
	{
		RenderState3D state;
		state.cullMode = CullMode::None;
		return state;
	}

	/// @brief 等値比較演算子
	/// @param other 比較対象
	/// @return 全フィールドが一致すればtrue
	[[nodiscard]] constexpr bool operator==(const RenderState3D& other) const noexcept
	{
		return wireframe == other.wireframe
			&& cullMode == other.cullMode
			&& depthTest == other.depthTest
			&& depthWrite == other.depthWrite
			&& blendEnabled == other.blendEnabled;
	}

	/// @brief 非等値比較演算子
	/// @param other 比較対象
	/// @return いずれかのフィールドが異なればtrue
	[[nodiscard]] constexpr bool operator!=(const RenderState3D& other) const noexcept
	{
		return !(*this == other);
	}
};

} // namespace mitiru::render
