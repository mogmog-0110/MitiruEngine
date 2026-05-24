#pragma once

/// @file TypeTraits.hpp
/// @brief sgc数学型の型特性（メタプログラミングユーティリティ）
///
/// sgcの数学型を判定するための型特性とコンセプト。
/// テンプレート関数で「任意のVecを受け取る」等の制約に使用。
///
/// @code
/// template <sgc::AnyVec V>
/// auto length(const V& v) { return v.length(); }
/// @endcode

#include <type_traits>

#include "sgc/math/Vec2.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/math/Vec4.hpp"
#include "sgc/math/Mat3.hpp"
#include "sgc/math/Mat4.hpp"
#include "sgc/math/Quaternion.hpp"

namespace sgc
{

// ── is_vec ──────────────────────────────────────────────────────

/// @brief Vec2型かどうかを判定する型特性
template <class T> struct IsVec2 : std::false_type {};
template <class T> struct IsVec2<Vec2<T>> : std::true_type {};

/// @brief Vec3型かどうかを判定する型特性
template <class T> struct IsVec3 : std::false_type {};
template <class T> struct IsVec3<Vec3<T>> : std::true_type {};

/// @brief Vec4型かどうかを判定する型特性
template <class T> struct IsVec4 : std::false_type {};
template <class T> struct IsVec4<Vec4<T>> : std::true_type {};

/// @brief 任意のVec型（Vec2, Vec3, Vec4）かどうかを判定する型特性
template <class T> struct IsVec : std::false_type {};
template <class T> struct IsVec<Vec2<T>> : std::true_type {};
template <class T> struct IsVec<Vec3<T>> : std::true_type {};
template <class T> struct IsVec<Vec4<T>> : std::true_type {};

/// @brief Vec2型かどうか（変数テンプレート）
template <class T> inline constexpr bool IS_VEC2 = IsVec2<T>::value;

/// @brief Vec3型かどうか（変数テンプレート）
template <class T> inline constexpr bool IS_VEC3 = IsVec3<T>::value;

/// @brief Vec4型かどうか（変数テンプレート）
template <class T> inline constexpr bool IS_VEC4 = IsVec4<T>::value;

/// @brief 任意のVec型かどうか（変数テンプレート）
template <class T> inline constexpr bool IS_VEC = IsVec<T>::value;

// ── is_mat ──────────────────────────────────────────────────────

/// @brief Mat3型かどうかを判定する型特性
template <class T> struct IsMat3 : std::false_type {};
template <class T> struct IsMat3<Mat3<T>> : std::true_type {};

/// @brief Mat4型かどうかを判定する型特性
template <class T> struct IsMat4 : std::false_type {};
template <class T> struct IsMat4<Mat4<T>> : std::true_type {};

/// @brief 任意のMat型（Mat3, Mat4）かどうかを判定する型特性
template <class T> struct IsMat : std::false_type {};
template <class T> struct IsMat<Mat3<T>> : std::true_type {};
template <class T> struct IsMat<Mat4<T>> : std::true_type {};

/// @brief Mat3型かどうか（変数テンプレート）
template <class T> inline constexpr bool IS_MAT3 = IsMat3<T>::value;

/// @brief Mat4型かどうか（変数テンプレート）
template <class T> inline constexpr bool IS_MAT4 = IsMat4<T>::value;

/// @brief 任意のMat型かどうか（変数テンプレート）
template <class T> inline constexpr bool IS_MAT = IsMat<T>::value;

// ── is_quaternion ───────────────────────────────────────────────

/// @brief Quaternion型かどうかを判定する型特性
template <class T> struct IsQuaternion : std::false_type {};
template <class T> struct IsQuaternion<Quaternion<T>> : std::true_type {};

/// @brief Quaternion型かどうか（変数テンプレート）
template <class T> inline constexpr bool IS_QUATERNION = IsQuaternion<T>::value;

// ── コンセプト ──────────────────────────────────────────────────

/// @brief 任意のsgc::Vec型であることを要求するコンセプト
template <class T>
concept AnyVec = IS_VEC<T>;

/// @brief 任意のsgc::Mat型であることを要求するコンセプト
template <class T>
concept AnyMat = IS_MAT<T>;

/// @brief 任意のsgc::Quaternion型であることを要求するコンセプト
template <class T>
concept AnyQuaternion = IS_QUATERNION<T>;

/// @brief sgcの数学型（Vec, Mat, Quaternion）であることを要求するコンセプト
template <class T>
concept MathType = IS_VEC<T> || IS_MAT<T> || IS_QUATERNION<T>;

// ── 要素型の取得 ────────────────────────────────────────────────

/// @brief sgc数学型から要素型を取得する
/// @tparam T sgc数学型（Vec2<float>など）
template <class T> struct ElementType {};

template <class T> struct ElementType<Vec2<T>> { using Type = T; };
template <class T> struct ElementType<Vec3<T>> { using Type = T; };
template <class T> struct ElementType<Vec4<T>> { using Type = T; };
template <class T> struct ElementType<Mat3<T>> { using Type = T; };
template <class T> struct ElementType<Mat4<T>> { using Type = T; };
template <class T> struct ElementType<Quaternion<T>> { using Type = T; };

/// @brief sgc数学型から要素型を取得するエイリアス
/// @code
/// using T = sgc::ElementTypeT<sgc::Vec3f>; // float
/// @endcode
template <class T>
using ElementTypeT = typename ElementType<T>::Type;

// ── 次元数の取得 ────────────────────────────────────────────────

/// @brief ベクトル型の次元数を取得する
template <class T> struct VecDimension {};
template <class T> struct VecDimension<Vec2<T>> { static constexpr int VALUE = 2; };
template <class T> struct VecDimension<Vec3<T>> { static constexpr int VALUE = 3; };
template <class T> struct VecDimension<Vec4<T>> { static constexpr int VALUE = 4; };

/// @brief ベクトル型の次元数を取得する（変数テンプレート）
/// @code
/// constexpr int dim = sgc::VEC_DIMENSION<sgc::Vec3f>; // 3
/// @endcode
template <AnyVec T>
inline constexpr int VEC_DIMENSION = VecDimension<T>::VALUE;

} // namespace sgc
