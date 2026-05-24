#pragma once

/// @file Result.hpp
/// @brief 成功値またはエラー値を保持する Result<T, E> 型
///
/// std::expected（C++23）の代替として、std::variantベースで実装。
/// MSVC/GCC/Clang全てのC++20コンパイラで動作する。

#include <functional>
#include <string>
#include <type_traits>
#include <variant>

namespace sgc
{

/// @brief Result のデフォルトエラー型
///
/// 文字列メッセージを保持するシンプルなエラー型。
///
/// @code
/// sgc::Error err{"ファイルが見つかりません"};
/// @endcode
struct Error
{
	std::string message;  ///< エラーメッセージ

	/// @brief エラーメッセージを指定して構築する
	/// @param msg エラーメッセージ文字列
	explicit Error(std::string msg)
		: message(std::move(msg))
	{
	}
};

/// @brief エラー状態でResultを構築するためのタグ型
struct ErrorTag
{
};

/// @brief ErrorTag のグローバルインスタンス
inline constexpr ErrorTag ERROR_TAG{};

/// @brief 成功（T）またはエラー（E）を保持する直和型
///
/// 関数の戻り値として使用し、例外を使わないエラーハンドリングを実現する。
/// map(), mapError(), andThen() によるモナディックな連鎖操作をサポートする。
///
/// @tparam T 成功時の値の型
/// @tparam E エラー時の値の型（デフォルト: sgc::Error）
///
/// @note T と E は異なる型でなければならない（std::variant の制約）
///
/// @code
/// sgc::Result<int> divide(int a, int b)
/// {
///     if (b == 0)
///     {
///         return {sgc::ERROR_TAG, sgc::Error{"ゼロ除算"}};
///     }
///     return a / b;
/// }
///
/// auto result = divide(10, 2);
/// if (result)
/// {
///     // result.value() == 5
/// }
/// @endcode
template <typename T, typename E = Error>
class Result
{
	static_assert(!std::is_same_v<T, E>, "Result<T, E>: T と E は異なる型でなければなりません");

public:
	/// @brief 成功値からの暗黙変換コンストラクタ（const参照版）
	/// @param value 成功値
	constexpr Result(const T& value)  // NOLINT: 意図的な暗黙変換
		: m_storage(value)
	{
	}

	/// @brief 成功値からの暗黙変換コンストラクタ（ムーブ版）
	/// @param value 成功値（ムーブされる）
	constexpr Result(T&& value)  // NOLINT: 意図的な暗黙変換
		: m_storage(std::move(value))
	{
	}

	/// @brief エラー値から構築する（const参照版）
	/// @param tag   エラータグ（ERROR_TAG を使用）
	/// @param error エラー値
	constexpr Result(ErrorTag, const E& error)
		: m_storage(error)
	{
	}

	/// @brief エラー値から構築する（ムーブ版）
	/// @param tag   エラータグ（ERROR_TAG を使用）
	/// @param error エラー値（ムーブされる）
	constexpr Result(ErrorTag, E&& error)
		: m_storage(std::move(error))
	{
	}

	/// @brief 成功値を保持しているか判定する
	/// @return 成功状態なら true
	[[nodiscard]] constexpr bool hasValue() const noexcept
	{
		return std::holds_alternative<T>(m_storage);
	}

	/// @brief エラーを保持しているか判定する
	/// @return エラー状態なら true
	[[nodiscard]] constexpr bool hasError() const noexcept
	{
		return std::holds_alternative<E>(m_storage);
	}

	/// @brief bool への明示的変換。成功状態なら true を返す
	[[nodiscard]] constexpr explicit operator bool() const noexcept
	{
		return hasValue();
	}

	/// @brief 成功値を取得する（const左辺値参照版）
	/// @return 成功値への参照
	/// @throws std::bad_variant_access エラー状態の場合
	[[nodiscard]] constexpr const T& value() const&
	{
		return std::get<T>(m_storage);
	}

	/// @brief 成功値を取得する（左辺値参照版）
	/// @return 成功値への参照
	/// @throws std::bad_variant_access エラー状態の場合
	[[nodiscard]] constexpr T& value() &
	{
		return std::get<T>(m_storage);
	}

	/// @brief 成功値を取得する（右辺値参照版、ムーブ用）
	/// @return 成功値の右辺値参照
	/// @throws std::bad_variant_access エラー状態の場合
	[[nodiscard]] constexpr T&& value() &&
	{
		return std::get<T>(std::move(m_storage));
	}

	/// @brief エラー値を取得する（const左辺値参照版）
	/// @return エラー値への参照
	/// @throws std::bad_variant_access 成功状態の場合
	[[nodiscard]] constexpr const E& error() const&
	{
		return std::get<E>(m_storage);
	}

	/// @brief エラー値を取得する（左辺値参照版）
	/// @return エラー値への参照
	/// @throws std::bad_variant_access 成功状態の場合
	[[nodiscard]] constexpr E& error() &
	{
		return std::get<E>(m_storage);
	}

	/// @brief エラー値を取得する（右辺値参照版、ムーブ用）
	/// @return エラー値の右辺値参照
	/// @throws std::bad_variant_access 成功状態の場合
	[[nodiscard]] constexpr E&& error() &&
	{
		return std::get<E>(std::move(m_storage));
	}

	/// @brief 成功値を取得する。エラー状態の場合はデフォルト値を返す
	/// @param defaultValue エラー時に返す値
	/// @return 成功値またはデフォルト値
	[[nodiscard]] constexpr T valueOr(const T& defaultValue) const&
	{
		if (hasValue())
		{
			return value();
		}
		return defaultValue;
	}

	/// @brief 成功値をムーブで取得する。エラー状態の場合はデフォルト値を返す
	/// @param defaultValue エラー時に返す値（ムーブされる）
	/// @return 成功値またはデフォルト値
	[[nodiscard]] constexpr T valueOr(T&& defaultValue) &&
	{
		if (hasValue())
		{
			return std::get<T>(std::move(m_storage));
		}
		return std::move(defaultValue);
	}

	/// @brief 成功値を関数で変換する（ファンクタmap操作）
	///
	/// 成功状態の場合、funcで値を変換した新しいResultを返す。
	/// エラー状態の場合、エラーをそのまま伝播する。
	///
	/// @tparam F 変換関数の型（const T& -> U）
	/// @param func 成功値を変換する関数
	/// @return 変換後の Result<U, E>
	///
	/// @code
	/// sgc::Result<int> r{10};
	/// auto doubled = r.map([](int x) { return x * 2; });
	/// // doubled.value() == 20
	/// @endcode
	template <typename F>
		requires std::invocable<F, const T&>
	[[nodiscard]] constexpr auto map(F&& func) const&
		-> Result<std::invoke_result_t<F, const T&>, E>
	{
		using U = std::invoke_result_t<F, const T&>;
		if (hasValue())
		{
			return Result<U, E>{std::invoke(std::forward<F>(func), value())};
		}
		return Result<U, E>{ERROR_TAG, error()};
	}

	/// @brief エラー値を関数で変換する
	///
	/// エラー状態の場合、funcでエラーを変換した新しいResultを返す。
	/// 成功状態の場合、成功値をそのまま伝播する。
	///
	/// @tparam F 変換関数の型（const E& -> U）
	/// @param func エラー値を変換する関数
	/// @return 変換後の Result<T, U>
	template <typename F>
		requires std::invocable<F, const E&>
	[[nodiscard]] constexpr auto mapError(F&& func) const&
		-> Result<T, std::invoke_result_t<F, const E&>>
	{
		using U = std::invoke_result_t<F, const E&>;
		if (hasError())
		{
			return Result<T, U>{ERROR_TAG, std::invoke(std::forward<F>(func), error())};
		}
		return Result<T, U>{value()};
	}

	/// @brief 成功値に対してResult を返す関数を連鎖する（flatMap / モナディックbind）
	///
	/// 成功状態の場合、funcを適用して新しいResultを返す。
	/// エラー状態の場合、エラーをそのまま伝播する。
	///
	/// @tparam F 連鎖関数の型（const T& -> Result<U, E>）
	/// @param func 成功値を受け取りResultを返す関数
	/// @return funcの戻り値、またはエラーを伝播したResult
	///
	/// @code
	/// auto parseInt = [](const std::string& s) -> sgc::Result<int>
	/// {
	///     return std::stoi(s);
	/// };
	/// sgc::Result<std::string> input{"42"};
	/// auto result = input.andThen(parseInt);
	/// @endcode
	template <typename F>
		requires std::invocable<F, const T&>
	[[nodiscard]] constexpr auto andThen(F&& func) const&
		-> std::invoke_result_t<F, const T&>
	{
		if (hasValue())
		{
			return std::invoke(std::forward<F>(func), value());
		}
		return std::invoke_result_t<F, const T&>{ERROR_TAG, error()};
	}

	/// @brief エラー値に対してResultを返す関数を連鎖する（エラー回復チェーン）
	///
	/// エラー状態の場合、funcを適用して新しいResultを返す。
	/// 成功状態の場合、成功値をそのまま伝播する。
	/// andThen() の対になるメソッドで、エラーからの回復パスを構築できる。
	///
	/// @tparam F 回復関数の型（const E& -> Result<T, E2>）
	/// @param func エラー値を受け取りResultを返す関数
	/// @return funcの戻り値、または成功値を伝播したResult
	///
	/// @code
	/// auto loadConfig = [](const std::string& path) -> sgc::Result<Config>
	/// {
	///     // ファイル読み込み
	/// };
	/// auto useDefault = [](const sgc::Error&) -> sgc::Result<Config>
	/// {
	///     return Config::defaultConfig();
	/// };
	/// auto config = loadConfig("app.json").orElse(useDefault);
	/// @endcode
	template <typename F>
		requires std::invocable<F, const E&>
	[[nodiscard]] constexpr auto orElse(F&& func) const&
		-> std::invoke_result_t<F, const E&>
	{
		if (hasError())
		{
			return std::invoke(std::forward<F>(func), error());
		}
		return std::invoke_result_t<F, const E&>{value()};
	}

private:
	std::variant<T, E> m_storage;  ///< 成功値またはエラー値を保持する内部ストレージ
};

} // namespace sgc
