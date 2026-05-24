#pragma once

/// @file Concepts.hpp
/// @brief ライブラリ全体で使用する汎用コンセプト定義
///
/// C++20 conceptsを用いて、テンプレートパラメータの制約を明示的に表現する。
/// 標準ライブラリのコンセプトをラップし、sgc名前空間で統一的に使用できるようにする。

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace sgc
{

// ── 算術型コンセプト ────────────────────────────────────────────

/// @brief 算術演算（+, -, *, /）をサポートする型
/// @tparam T 検査対象の型
///
/// int, float, double, char, bool 等の組み込み算術型にマッチする。
/// std::string やユーザー定義型にはマッチしない。
template <typename T>
concept Arithmetic = std::is_arithmetic_v<T>;

/// @brief 浮動小数点型（float, double, long double）
/// @tparam T 検査対象の型
template <typename T>
concept FloatingPoint = std::floating_point<T>;

/// @brief 整数型（int, unsigned, char 等）
/// @tparam T 検査対象の型
template <typename T>
concept Integral = std::integral<T>;

/// @brief 符号付き整数型
/// @tparam T 検査対象の型
template <typename T>
concept SignedIntegral = std::signed_integral<T>;

/// @brief 符号なし整数型
/// @tparam T 検査対象の型
template <typename T>
concept UnsignedIntegral = std::unsigned_integral<T>;

// ── ハッシュ可能 ────────────────────────────────────────────────

/// @brief std::hashでハッシュ化可能な型
/// @tparam T 検査対象の型
///
/// @code
/// static_assert(sgc::Hashable<int>);          // OK
/// static_assert(sgc::Hashable<std::string>);  // OK
/// @endcode
template <typename T>
concept Hashable = requires(const T& a)
{
	{ std::hash<T>{}(a) } -> std::convertible_to<std::size_t>;
};

// ── 等値比較・全順序 ───────────────────────────────────────────

/// @brief ==, != による等値比較が可能な型
/// @tparam T 検査対象の型
template <typename T>
concept EqualityComparable = std::equality_comparable<T>;

/// @brief <, >, <=, >= による全順序比較が可能な型
/// @tparam T 検査対象の型
template <typename T>
concept TotallyOrdered = std::totally_ordered<T>;

// ── 呼び出し可能型コンセプト ────────────────────────────────────

/// @brief 引数Args...で呼び出し可能で、戻り値がRに変換できる型
/// @tparam F    呼び出し可能オブジェクトの型
/// @tparam R    期待する戻り値の型
/// @tparam Args 引数の型パック
///
/// @code
/// auto fn = [](int x) -> double { return x * 1.5; };
/// static_assert(sgc::InvocableReturning<decltype(fn), double, int>);
/// @endcode
template <typename F, typename R, typename... Args>
concept InvocableReturning = std::invocable<F, Args...>
	&& std::convertible_to<std::invoke_result_t<F, Args...>, R>;

/// @brief 引数Args...を受け取りboolを返す述語型
/// @tparam F    述語の型
/// @tparam Args 引数の型パック
template <typename F, typename... Args>
concept Predicate = std::predicate<F, Args...>;

// ── コンテナ系コンセプト ────────────────────────────────────────

/// @brief begin()/end() を持ち、範囲forが使用可能な型
/// @tparam T 検査対象の型
///
/// @code
/// static_assert(sgc::Iterable<std::vector<int>>);  // OK
/// static_assert(!sgc::Iterable<int>);              // intは反復不可
/// @endcode
template <typename T>
concept Iterable = requires(T& t)
{
	{ t.begin() } -> std::input_or_output_iterator;
	{ t.end() } -> std::sentinel_for<decltype(t.begin())>;
};

/// @brief size() メンバを持つ型
/// @tparam T 検査対象の型
template <typename T>
concept Sizable = requires(const T& t)
{
	{ t.size() } -> std::convertible_to<std::size_t>;
};

// ── 列挙型コンセプト ───────────────────────────────────────────

/// @brief 列挙型（enum または enum class）
/// @tparam T 検査対象の型
template <typename T>
concept Enum = std::is_enum_v<T>;

/// @brief スコープ付き列挙型（enum class）のみにマッチ
/// @tparam T 検査対象の型
///
/// enum class は基底型への暗黙変換が不可能であることを利用して判定する。
template <typename T>
concept ScopedEnum = Enum<T> && !std::is_convertible_v<T, std::underlying_type_t<T>>;

// ── ECSコンポーネント ──────────────────────────────────────────

/// @brief ECSコンポーネントとして使用可能な型
/// @tparam T 検査対象の型
///
/// デフォルト構築可能かつムーブ構築可能であることを要求する。
/// ComponentStorageのswap-and-pop削除にはムーブ代入も必要となるため、
/// std::movableを基本とする。
///
/// @code
/// struct Position { float x{0}; float y{0}; };
/// static_assert(sgc::Component<Position>);  // OK
/// @endcode
template <typename T>
concept Component = std::movable<T> && std::default_initializable<T>;

// ── インターフェース検出 ─────────────────────────────────────

/// @brief serialize(writer)/deserialize(reader)メソッドを持つ型
/// @tparam T 検査対象の型
/// @tparam Writer シリアライズ先の型
/// @tparam Reader デシリアライズ元の型
///
/// @code
/// struct SaveData {
///     void serialize(JsonWriter& writer) const { ... }
///     void deserialize(JsonReader& reader) { ... }
/// };
/// static_assert(sgc::Serializable<SaveData, JsonWriter, JsonReader>);
/// @endcode
template <typename T, typename Writer, typename Reader>
concept Serializable = requires(const T ct, T t, Writer& w, Reader& r)
{
	ct.serialize(w);
	t.deserialize(r);
};

/// @brief draw()メソッドを持つ型
/// @tparam T 検査対象の型
///
/// @code
/// struct Sprite {
///     void draw() const { ... }
/// };
/// static_assert(sgc::Drawable<Sprite>);  // OK
/// @endcode
template <typename T>
concept Drawable = requires(const T t)
{
	{ t.draw() };
};

/// @brief update(float dt)メソッドを持つ型
/// @tparam T 検査対象の型
///
/// @code
/// struct Particle {
///     void update(float dt) { ... }
/// };
/// static_assert(sgc::Updatable<Particle>);  // OK
/// @endcode
template <typename T>
concept Updatable = requires(T t)
{
	{ t.update(std::declval<float>()) };
};

} // namespace sgc
