#pragma once

/// @file Transform.hpp
/// @brief 階層型トランスフォーム
///
/// 位置・回転・スケールを管理し、親子階層によるワールド座標変換を提供する。
/// ダーティフラグによる遅延計算で不要な行列再計算を回避する。
///
/// @code
/// sgc::Transformf root;
/// root.setPosition({0, 0, 0});
///
/// sgc::Transformf child;
/// child.setParent(&root);
/// child.setPosition({1, 0, 0});
///
/// auto worldPos = child.worldPosition(); // root.position + child.position
/// @endcode

#include <algorithm>
#include <cstddef>
#include <vector>

#include "sgc/types/Concepts.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/math/Quaternion.hpp"
#include "sgc/math/Mat4.hpp"

namespace sgc
{

/// @brief 階層型トランスフォーム
/// @tparam T 浮動小数点型
template <FloatingPoint T>
class Transform
{
public:
	/// @brief ローカル位置を設定する
	/// @param pos 位置
	void setPosition(const Vec3<T>& pos)
	{
		m_position = pos;
		markDirty();
	}

	/// @brief ローカル位置を取得する
	/// @return 位置
	[[nodiscard]] const Vec3<T>& position() const noexcept { return m_position; }

	/// @brief ローカル回転を設定する
	/// @param rot クォータニオン回転
	void setRotation(const Quaternion<T>& rot)
	{
		m_rotation = rot;
		markDirty();
	}

	/// @brief ローカル回転を取得する
	/// @return クォータニオン回転
	[[nodiscard]] const Quaternion<T>& rotation() const noexcept { return m_rotation; }

	/// @brief ローカルスケールを設定する
	/// @param scl スケール
	void setScale(const Vec3<T>& scl)
	{
		m_scale = scl;
		markDirty();
	}

	/// @brief ローカルスケールを取得する
	/// @return スケール
	[[nodiscard]] const Vec3<T>& scale() const noexcept { return m_scale; }

	/// @brief 親トランスフォームを設定する
	/// @param parent 親（nullptrでルート化）
	void setParent(Transform* parent)
	{
		// 旧親から自身を削除
		if (m_parent)
		{
			auto& siblings = m_parent->m_children;
			siblings.erase(
				std::remove(siblings.begin(), siblings.end(), this),
				siblings.end());
		}

		m_parent = parent;

		// 新親に自身を追加
		if (m_parent)
		{
			m_parent->m_children.push_back(this);
		}

		markDirty();
	}

	/// @brief 親トランスフォームを取得する
	/// @return 親（ルートならnullptr）
	[[nodiscard]] Transform* parent() const noexcept { return m_parent; }

	/// @brief 子トランスフォームのリスト
	/// @return 子ポインタ配列
	[[nodiscard]] const std::vector<Transform*>& children() const noexcept { return m_children; }

	/// @brief ローカル変換行列を計算する
	/// @return ローカルのモデル行列
	[[nodiscard]] Mat4<T> localMatrix() const
	{
		const auto translation = Mat4<T>::translation(m_position);
		const auto rotation = m_rotation.toMat4();
		const auto scale = Mat4<T>::scaling(m_scale);
		return translation * rotation * scale;
	}

	/// @brief ワールド変換行列を取得する（ダーティ時に再計算）
	/// @return ワールドのモデル行列
	[[nodiscard]] const Mat4<T>& worldMatrix() const
	{
		if (m_dirty)
		{
			updateWorldMatrix();
		}
		return m_worldMatrix;
	}

	/// @brief ワールド位置を取得する
	/// @return ワールド座標
	[[nodiscard]] Vec3<T> worldPosition() const
	{
		const auto& wm = worldMatrix();
		// 行優先: 平行移動は (row0,col3), (row1,col3), (row2,col3)
		return Vec3<T>{wm.m[0][3], wm.m[1][3], wm.m[2][3]};
	}

private:
	Vec3<T> m_position{};                  ///< ローカル位置
	Quaternion<T> m_rotation{};            ///< ローカル回転
	Vec3<T> m_scale{T{1}, T{1}, T{1}};    ///< ローカルスケール

	Transform* m_parent{nullptr};           ///< 親
	std::vector<Transform*> m_children;     ///< 子

	mutable Mat4<T> m_worldMatrix{Mat4<T>::identity()};  ///< キャッシュされたワールド行列
	mutable bool m_dirty{true};                          ///< ダーティフラグ

	/// @brief 自身と全子孫にダーティフラグを立てる
	void markDirty()
	{
		m_dirty = true;
		for (auto* child : m_children)
		{
			child->markDirty();
		}
	}

	/// @brief ワールド行列を再計算する
	void updateWorldMatrix() const
	{
		const auto local = localMatrix();
		if (m_parent)
		{
			m_worldMatrix = m_parent->worldMatrix() * local;
		}
		else
		{
			m_worldMatrix = local;
		}
		m_dirty = false;
	}
};

/// @brief float版 Transform
using Transformf = Transform<float>;

/// @brief double版 Transform
using Transformd = Transform<double>;

} // namespace sgc
