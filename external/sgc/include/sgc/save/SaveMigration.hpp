#pragma once

/// @file SaveMigration.hpp
/// @brief セーブデータのマイグレーション/バージョニング
///
/// セーブデータをバージョンN→N+1に変換するマイグレーション機能を提供。
/// 複数バージョンを跨ぐチェーンマイグレーションにも対応。
///
/// @code
/// using namespace sgc::save;
/// MigrationChain chain;
/// chain.addMigration(1, 2, [](SaveData data) {
///     // v1→v2: "hp"をintからfloatに変換
///     if (auto hp = data.getAs<int>("hp"))
///     {
///         data.set("hp", static_cast<float>(*hp));
///     }
///     data.setVersion(2);
///     return data;
/// });
/// auto migrated = chain.migrate(oldData, 1, 3);
/// @endcode

#include "sgc/save/SaveSystem.hpp"

#include <functional>
#include <map>
#include <stdexcept>

namespace sgc::save
{

/// @brief マイグレーション関数型（SaveDataを受け取り変換後のSaveDataを返す）
using MigrationFunc = std::function<SaveData(SaveData)>;

/// @brief 単一バージョン間のマイグレーション
struct SaveMigration
{
	uint32_t fromVersion;        ///< 変換元バージョン
	uint32_t toVersion;          ///< 変換先バージョン
	MigrationFunc transform;     ///< 変換関数
};

/// @brief マイグレーションチェーン（順序付きマイグレーションリスト）
///
/// バージョンN→N+1のマイグレーションを登録し、
/// 任意のバージョン間のチェーンマイグレーションを実行する。
class MigrationChain
{
public:
	/// @brief マイグレーションを登録する
	/// @param fromVersion 変換元バージョン
	/// @param toVersion 変換先バージョン
	/// @param transform 変換関数
	void addMigration(uint32_t fromVersion, uint32_t toVersion, MigrationFunc transform)
	{
		m_migrations[fromVersion] = SaveMigration{
			fromVersion,
			toVersion,
			std::move(transform)
		};
	}

	/// @brief セーブデータをマイグレーションする
	/// @param data 元のセーブデータ
	/// @param fromVersion 現在のバージョン
	/// @param toVersion 目標バージョン
	/// @return マイグレーション後のセーブデータ。失敗時はstd::nullopt
	[[nodiscard]] std::optional<SaveData> migrate(
		SaveData data, uint32_t fromVersion, uint32_t toVersion) const
	{
		if (fromVersion == toVersion)
		{
			return data;
		}
		if (fromVersion > toVersion)
		{
			return std::nullopt; // ダウングレードは非サポート
		}

		uint32_t currentVersion = fromVersion;
		SaveData current = std::move(data);

		while (currentVersion < toVersion)
		{
			const auto it = m_migrations.find(currentVersion);
			if (it == m_migrations.end())
			{
				return std::nullopt; // マイグレーションパスが途切れている
			}

			const auto& migration = it->second;
			current = migration.transform(std::move(current));
			currentVersion = migration.toVersion;
		}

		if (currentVersion != toVersion)
		{
			return std::nullopt; // 目標バージョンに到達できなかった
		}

		return current;
	}

	/// @brief 登録されたマイグレーション数を取得
	/// @return マイグレーション数
	[[nodiscard]] size_t migrationCount() const noexcept { return m_migrations.size(); }

	/// @brief 指定バージョンからのマイグレーションが存在するか
	/// @param fromVersion 変換元バージョン
	/// @return 存在すればtrue
	[[nodiscard]] bool hasMigration(uint32_t fromVersion) const noexcept
	{
		return m_migrations.find(fromVersion) != m_migrations.end();
	}

	/// @brief マイグレーション可能な最大バージョンを取得
	/// @param fromVersion 開始バージョン
	/// @return 到達可能な最大バージョン
	[[nodiscard]] uint32_t maxReachableVersion(uint32_t fromVersion) const noexcept
	{
		uint32_t current = fromVersion;
		while (true)
		{
			const auto it = m_migrations.find(current);
			if (it == m_migrations.end())
			{
				break;
			}
			current = it->second.toVersion;
		}
		return current;
	}

private:
	/// @brief fromVersion → マイグレーション のマップ
	std::map<uint32_t, SaveMigration> m_migrations;
};

} // namespace sgc::save
