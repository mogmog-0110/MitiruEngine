#pragma once

/// @file JsonConfig.hpp
/// @brief 構造体とJSONファイルの双方向バインディング
///
/// DataNodeを介して、ユーザー定義構造体とJSONファイルの
/// 読み書きを自動化するテンプレートクラス。
///
/// 利用する構造体は toDataNode() / fromDataNode() メソッドを持つ必要がある。
///
/// @code
/// struct PlayerConfig
/// {
///     double speed = 5200.0;
///     double jumpForce = 12.5;
///
///     sgc::data::DataNode toDataNode() const
///     {
///         auto node = sgc::data::DataNode::object();
///         node["speed"] = sgc::data::DataNode(speed);
///         node["jumpForce"] = sgc::data::DataNode(jumpForce);
///         return node;
///     }
///
///     void fromDataNode(const sgc::data::DataNode& node)
///     {
///         speed = node["speed"].asFloat(speed);
///         jumpForce = node["jumpForce"].asFloat(jumpForce);
///     }
/// };
///
/// sgc::data::JsonConfig<PlayerConfig> config("player.json");
/// config.load();     // ファイルから読み込み
/// config.data().speed = 6000.0;
/// config.save();     // ファイルに保存
/// @endcode

#include "sgc/data/DataNode.hpp"
#include <concepts>
#include <string>

namespace sgc::data
{

/// @brief DataNodeシリアライズ可能な型の制約
template <typename T>
concept DataNodeSerializable = requires(T t, const T ct, const DataNode& node)
{
	{ ct.toDataNode() } -> std::same_as<DataNode>;
	{ t.fromDataNode(node) } -> std::same_as<void>;
};

/// @brief 構造体とJSONファイルの双方向バインディング
///
/// @tparam T DataNodeSerializableを満たす型
template <DataNodeSerializable T>
class JsonConfig
{
public:
	/// @brief コンストラクタ
	/// @param filePath JSONファイルパス
	explicit JsonConfig(const std::string& filePath)
		: m_filePath{filePath}
	{
	}

	/// @brief コンストラクタ（ムーブ文字列）
	explicit JsonConfig(std::string&& filePath) noexcept
		: m_filePath{std::move(filePath)}
	{
	}

	/// @brief ファイルからデータを読み込む
	/// @return 読み込み成功時true
	bool load()
	{
		auto node = DataNode::loadFromFile(m_filePath);
		if (node.isNull()) return false;

		m_data.fromDataNode(node);
		return true;
	}

	/// @brief データをファイルに保存する
	/// @param pretty 整形出力するかどうか（デフォルト: true）
	/// @return 保存成功時true
	bool save(bool pretty = true) const
	{
		const auto node = m_data.toDataNode();
		return node.saveToFile(m_filePath, pretty);
	}

	/// @brief データへの参照を取得する
	[[nodiscard]] T& data() noexcept { return m_data; }

	/// @brief データへのconst参照を取得する
	[[nodiscard]] const T& data() const noexcept { return m_data; }

	/// @brief デフォルト値にリセットする
	void resetToDefault()
	{
		m_data = T{};
	}

	/// @brief ファイルパスを取得する
	[[nodiscard]] const std::string& filePath() const noexcept { return m_filePath; }

	/// @brief ファイルパスを変更する
	void setFilePath(const std::string& path) { m_filePath = path; }

private:
	std::string m_filePath;
	T m_data{};
};

} // namespace sgc::data
