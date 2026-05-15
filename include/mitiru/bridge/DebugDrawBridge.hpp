#pragma once

/// @file DebugDrawBridge.hpp
/// @brief sgc デバッグ描画統合ブリッジ
/// @details sgcのDebugDrawシステムをMitiruエンジンに統合する。
///          デバッグ用のプリミティブ描画キューをラップする。

#include <cstddef>
#include <string>
#include <vector>

#include <sgc/debug/DebugDraw.hpp>

namespace mitiru::bridge
{

/// @brief sgc デバッグ描画統合ブリッジ
/// @details デバッグ用の矩形・円・線・矢印・パスの描画を管理する。
///          flush()でIRendererに一括描画する。
///
/// @code
/// mitiru::bridge::DebugDrawBridge debug;
/// debug.drawRect({{0,0},{100,50}}, {1,0,0,1});
/// debug.drawCircle({200,200}, 30.0f, {0,1,0,1});
/// debug.flush(renderer);
/// @endcode
class DebugDrawBridge
{
public:
	/// @brief 矩形枠を描画キューに追加する
	/// @param rect 矩形
	/// @param color 描画色
	/// @param thickness 線の太さ（デフォルト: 1.0f）
	void drawRect(const sgc::AABB2f& rect, const sgc::Colorf& color, float thickness = 1.0f)
	{
		m_debugDraw.drawRect(rect, color, thickness);
	}

	/// @brief 円枠を描画キューに追加する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param color 描画色
	void drawCircle(const sgc::Vec2f& center, float radius, const sgc::Colorf& color)
	{
		m_debugDraw.drawCircle(center, radius, color);
	}

	/// @brief 線分を描画キューに追加する
	/// @param from 始点
	/// @param to 終点
	/// @param color 描画色
	void drawLine(const sgc::Vec2f& from, const sgc::Vec2f& to, const sgc::Colorf& color)
	{
		m_debugDraw.drawLine(from, to, color);
	}

	/// @brief 矢印を描画キューに追加する
	/// @param from 始点
	/// @param to 終点（矢印の先端）
	/// @param color 描画色
	void drawArrow(const sgc::Vec2f& from, const sgc::Vec2f& to, const sgc::Colorf& color)
	{
		m_debugDraw.drawArrow(from, to, color);
	}

	/// @brief 折れ線パスを描画キューに追加する
	/// @param points 頂点列
	/// @param color 描画色
	void drawPath(const std::vector<sgc::Vec2f>& points, const sgc::Colorf& color)
	{
		m_debugDraw.drawPath(points, color);
	}

	/// @brief キュー内の全コマンドをIRendererに描画し、キューをクリアする
	/// @param renderer 描画先レンダラー
	void flush(sgc::IRenderer& renderer)
	{
		m_debugDraw.flush(renderer);
	}

	/// @brief 描画の有効/無効を切り替える
	/// @param enabled trueで有効化
	void setEnabled(bool enabled)
	{
		m_debugDraw.setEnabled(enabled);
	}

	/// @brief 描画が有効かどうかを取得する
	/// @return 有効ならtrue
	[[nodiscard]] bool isEnabled() const noexcept
	{
		return m_debugDraw.isEnabled();
	}

	/// @brief キュー内の保留中コマンド数を取得する
	/// @return コマンド数
	[[nodiscard]] std::size_t pendingCount() const noexcept
	{
		return m_debugDraw.pendingCount();
	}

	// ── シリアライズ ──────────────────────────────────────────

	/// @brief デバッグ描画状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"enabled\":" + std::string(m_debugDraw.isEnabled() ? "true" : "false");
		json += ",\"pendingCount\":" + std::to_string(m_debugDraw.pendingCount());
		json += "}";
		return json;
	}

private:
	sgc::debug::DebugDraw m_debugDraw;  ///< デバッグ描画キュー
};

} // namespace mitiru::bridge
