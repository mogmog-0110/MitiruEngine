#pragma once

/// @file EntityInspector.hpp
/// @brief ECSエンティティデバッグインスペクター
/// @details エンティティの情報（ID、名前、コンポーネント）を表示・検索する。
///          データソースはIEntitySourceインターフェースで抽象化される。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mitiru::editor
{

/// @brief コンポーネント情報
/// @details コンポーネントの型名とフィールド（キー・値ペア）を保持する。
struct ComponentInfo
{
	std::string name;                                    ///< コンポーネント型名
	std::vector<std::pair<std::string, std::string>> fields;  ///< フィールド名と値のペア
};

/// @brief エンティティ情報
/// @details 1つのエンティティのID、名前、コンポーネントリスト、アクティブ状態を保持する。
struct EntityInfo
{
	std::uint64_t id = 0;                 ///< エンティティID
	std::string name;                     ///< エンティティ名
	std::vector<ComponentInfo> components; ///< コンポーネント情報のリスト
	bool active = true;                   ///< アクティブ状態
};

/// @brief エンティティデータソースの抽象インターフェース
/// @details ECSワールドから情報を取得するためのインターフェース。
class IEntitySource
{
public:
	/// @brief デストラクタ
	virtual ~IEntitySource() = default;

	/// @brief エンティティ総数を取得する
	/// @return エンティティの数
	[[nodiscard]] virtual std::size_t getEntityCount() const = 0;

	/// @brief 指定インデックスのエンティティ情報を取得する
	/// @param index エンティティのインデックス
	/// @return エンティティ情報
	[[nodiscard]] virtual EntityInfo getEntityInfo(std::size_t index) const = 0;
};

/// @brief エンティティインスペクター
/// @details IEntitySourceからエンティティ情報を取得し、フィルタリングや検索を行う。
///
/// @code
/// mitiru::editor::EntityInspector inspector;
/// inspector.setSource(&myEntitySource);
/// inspector.setNameFilter("Player");
/// auto data = inspector.getDisplayData();
/// @endcode
class EntityInspector
{
public:
	/// @brief データソースを設定する
	/// @param source エンティティソース（非所有、nullptrを許容）
	void setSource(IEntitySource* source) noexcept
	{
		m_source = source;
	}

	/// @brief 選択中のエンティティIDを設定する
	/// @param entityId エンティティID
	void selectEntity(std::uint64_t entityId) noexcept
	{
		m_selectedEntityId = entityId;
	}

	/// @brief 選択中のエンティティIDを取得する
	/// @return 選択中のエンティティID（未選択の場合はnullopt）
	[[nodiscard]] std::optional<std::uint64_t> selectedEntity() const noexcept
	{
		return m_selectedEntityId;
	}

	/// @brief 選択を解除する
	void clearSelection() noexcept
	{
		m_selectedEntityId = std::nullopt;
	}

	/// @brief 名前フィルターを設定する
	/// @param filter フィルター文字列（部分一致）
	void setNameFilter(std::string_view filter)
	{
		m_nameFilter = std::string(filter);
	}

	/// @brief コンポーネント型フィルターを設定する
	/// @param componentType コンポーネント型名（空文字列でフィルター解除）
	void setComponentFilter(std::string_view componentType)
	{
		m_componentFilter = std::string(componentType);
	}

	/// @brief 名前フィルターを取得する
	/// @return 現在の名前フィルター文字列
	[[nodiscard]] const std::string& nameFilter() const noexcept
	{
		return m_nameFilter;
	}

	/// @brief コンポーネント型フィルターを取得する
	/// @return 現在のコンポーネント型フィルター
	[[nodiscard]] const std::string& componentFilter() const noexcept
	{
		return m_componentFilter;
	}

	/// @brief フィルタリングされたエンティティ情報のリストを取得する
	/// @return 表示用のエンティティ情報リスト
	[[nodiscard]] std::vector<EntityInfo> getDisplayData() const
	{
		if (!m_source)
		{
			return {};
		}

		std::vector<EntityInfo> result;
		const auto count = m_source->getEntityCount();

		for (std::size_t i = 0; i < count; ++i)
		{
			auto info = m_source->getEntityInfo(i);
			if (matchesFilter(info))
			{
				result.push_back(std::move(info));
			}
		}

		return result;
	}

	/// @brief 選択中のエンティティ情報を取得する
	/// @return 選択中のエンティティ情報（未選択またはソースなしの場合はnullopt）
	[[nodiscard]] std::optional<EntityInfo> getSelectedEntityInfo() const
	{
		if (!m_source || !m_selectedEntityId)
		{
			return std::nullopt;
		}

		const auto count = m_source->getEntityCount();
		for (std::size_t i = 0; i < count; ++i)
		{
			auto info = m_source->getEntityInfo(i);
			if (info.id == *m_selectedEntityId)
			{
				return info;
			}
		}

		return std::nullopt;
	}

private:
	/// @brief エンティティがフィルター条件に一致するか判定する
	/// @param info エンティティ情報
	/// @return 一致する場合 true
	[[nodiscard]] bool matchesFilter(const EntityInfo& info) const
	{
		/// 名前フィルターの確認
		if (!m_nameFilter.empty())
		{
			if (info.name.find(m_nameFilter) == std::string::npos)
			{
				return false;
			}
		}

		/// コンポーネント型フィルターの確認
		if (!m_componentFilter.empty())
		{
			const bool hasComponent = std::any_of(
				info.components.begin(), info.components.end(),
				[this](const ComponentInfo& comp)
				{
					return comp.name == m_componentFilter;
				}
			);
			if (!hasComponent)
			{
				return false;
			}
		}

		return true;
	}

	IEntitySource* m_source = nullptr;                    ///< エンティティソース（非所有）
	std::optional<std::uint64_t> m_selectedEntityId;      ///< 選択中のエンティティID
	std::string m_nameFilter;                             ///< 名前フィルター
	std::string m_componentFilter;                        ///< コンポーネント型フィルター
};

} // namespace mitiru::editor
