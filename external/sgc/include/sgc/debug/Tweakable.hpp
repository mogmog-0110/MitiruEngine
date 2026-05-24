#pragma once

/// @file Tweakable.hpp
/// @brief ランタイムで調整可能な変数システム
///
/// デバッグUI統合を想定した、名前付きの調整可能変数を管理する。
/// 最小値・最大値の範囲制約と、全変数の列挙機能を提供する。
///
/// @code
/// sgc::debug::TweakRegistry registry;
/// registry.registerTweak<float>("gravity", 9.8f, 0.0f, 100.0f);
/// registry.setTweak<float>("gravity", 15.0f);
/// auto val = registry.getTweak<float>("gravity"); // 15.0f
///
/// auto infos = registry.listTweaks();
/// for (const auto& info : infos)
/// {
///     // info.name, info.typeName, info.currentValueStr
/// }
/// @endcode

#include <any>
#include <concepts>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sgc::debug
{

/// @brief 調整可能変数の情報（UI表示用）
struct TweakInfo
{
	std::string name;              ///< 変数名
	std::string typeName;          ///< 型名文字列
	std::string currentValueStr;   ///< 現在値の文字列表現
};

/// @brief 調整可能変数のラッパー
/// @tparam T 変数の型（コピー可能であること。std::anyに格納するため必須）
template <std::copyable T>
struct TweakVar
{
	std::string name;     ///< 変数名
	T value;              ///< 現在値
	T defaultValue;       ///< デフォルト値
	T minValue;           ///< 最小値
	T maxValue;           ///< 最大値

	/// @brief デフォルト値にリセットする
	void reset() noexcept { value = defaultValue; }
};

namespace detail
{

/// @brief 型消去された調整変数エントリ（内部用）
struct TweakEntry
{
	std::any data;                                  ///< TweakVar<T>の実体
	std::function<std::string()> toStringFn;        ///< 値を文字列化する関数
	std::function<void()> resetFn;                  ///< デフォルト値にリセットする関数
	std::string typeName;                           ///< 型名
	std::string name;                               ///< 変数名
};

} // namespace detail

/// @brief 調整可能変数のレジストリ
///
/// 名前付きの変数を登録し、型安全に取得・設定できる。
/// listTweaks()で全変数の情報を取得し、デバッグUIに表示可能。
class TweakRegistry
{
public:
	/// @brief 調整変数を登録する
	/// @tparam T 変数の型（コピー可能であること）
	/// @param name 変数名
	/// @param defaultVal デフォルト値
	/// @param minVal 最小値
	/// @param maxVal 最大値
	/// @return 登録されたTweakVar<T>へのポインタ
	template <std::copyable T>
	TweakVar<T>* registerTweak(
		const std::string& name,
		const T& defaultVal,
		const T& minVal,
		const T& maxVal)
	{
		TweakVar<T> var;
		var.name = name;
		var.value = defaultVal;
		var.defaultValue = defaultVal;
		var.minValue = minVal;
		var.maxValue = maxVal;

		detail::TweakEntry entry;
		entry.name = name;
		entry.typeName = typeid(T).name();
		entry.data = var;

		entry.toStringFn = [this, name]() -> std::string
		{
			const auto it = m_entries.find(name);
			if (it == m_entries.end())
			{
				return "";
			}
			const auto* ptr = std::any_cast<TweakVar<T>>(&it->second.data);
			if (!ptr)
			{
				return "";
			}
			if constexpr (std::is_same_v<T, std::string>)
			{
				return ptr->value;
			}
			else
			{
				return std::to_string(ptr->value);
			}
		};

		entry.resetFn = [this, name]()
		{
			const auto it = m_entries.find(name);
			if (it != m_entries.end())
			{
				auto* ptr = std::any_cast<TweakVar<T>>(&it->second.data);
				if (ptr)
				{
					ptr->reset();
				}
			}
		};

		m_entries[name] = std::move(entry);

		return std::any_cast<TweakVar<T>>(&m_entries[name].data);
	}

	/// @brief 値を設定する
	/// @tparam T 変数の型（コピー可能であること）
	/// @param name 変数名
	/// @param value 新しい値（最小・最大でクランプ）
	/// @return 設定成功時true
	template <std::copyable T>
	bool setTweak(const std::string& name, const T& value)
	{
		const auto it = m_entries.find(name);
		if (it == m_entries.end())
		{
			return false;
		}
		auto* ptr = std::any_cast<TweakVar<T>>(&it->second.data);
		if (!ptr)
		{
			return false;
		}
		// 範囲クランプ
		if constexpr (requires(T a, T b) { a < b; })
		{
			if (value < ptr->minValue)
			{
				ptr->value = ptr->minValue;
			}
			else if (value > ptr->maxValue)
			{
				ptr->value = ptr->maxValue;
			}
			else
			{
				ptr->value = value;
			}
		}
		else
		{
			ptr->value = value;
		}
		return true;
	}

	/// @brief 値を取得する
	/// @tparam T 変数の型（コピー可能であること）
	/// @param name 変数名
	/// @return 値（未登録または型不一致時はnullopt）
	template <std::copyable T>
	[[nodiscard]] std::optional<T> getTweak(const std::string& name) const
	{
		const auto it = m_entries.find(name);
		if (it == m_entries.end())
		{
			return std::nullopt;
		}
		const auto* ptr = std::any_cast<TweakVar<T>>(&it->second.data);
		if (!ptr)
		{
			return std::nullopt;
		}
		return ptr->value;
	}

	/// @brief TweakVar<T>へのポインタを取得する
	/// @tparam T 変数の型（コピー可能であること）
	/// @param name 変数名
	/// @return ポインタ（未登録時nullptr）
	template <std::copyable T>
	[[nodiscard]] TweakVar<T>* getVar(const std::string& name)
	{
		const auto it = m_entries.find(name);
		if (it == m_entries.end())
		{
			return nullptr;
		}
		return std::any_cast<TweakVar<T>>(&it->second.data);
	}

	/// @brief 変数をデフォルト値にリセットする
	/// @param name 変数名
	/// @return 成功時true
	bool resetTweak(const std::string& name)
	{
		const auto it = m_entries.find(name);
		if (it == m_entries.end())
		{
			return false;
		}
		it->second.resetFn();
		return true;
	}

	/// @brief 全変数の情報を取得する
	/// @return TweakInfo一覧
	[[nodiscard]] std::vector<TweakInfo> listTweaks() const
	{
		std::vector<TweakInfo> result;
		result.reserve(m_entries.size());
		for (const auto& [name, entry] : m_entries)
		{
			TweakInfo info;
			info.name = name;
			info.typeName = entry.typeName;
			info.currentValueStr = entry.toStringFn();
			result.push_back(std::move(info));
		}
		return result;
	}

	/// @brief 登録数を取得する
	[[nodiscard]] size_t size() const noexcept { return m_entries.size(); }

	/// @brief 変数が登録されているか判定する
	/// @param name 変数名
	[[nodiscard]] bool contains(const std::string& name) const
	{
		return m_entries.contains(name);
	}

	/// @brief 全変数をクリアする
	void clear() { m_entries.clear(); }

private:
	std::unordered_map<std::string, detail::TweakEntry> m_entries;
};

/// @brief 便利マクロ：TweakVarをレジストリに登録して変数を宣言する
/// @param registry TweakRegistryのインスタンス
/// @param type 変数の型
/// @param name 変数名（文字列）
/// @param defaultVal デフォルト値
/// @param minVal 最小値
/// @param maxVal 最大値
#define SGC_TWEAK(registry, type, name, defaultVal, minVal, maxVal) \
	(registry).registerTweak<type>((name), (defaultVal), (minVal), (maxVal))

} // namespace sgc::debug
