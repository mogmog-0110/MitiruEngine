#pragma once

/// @file Tilemap.hpp
/// @brief 2Dタイルマップ管理
///
/// レイヤー付き2Dタイルマップの作成・操作を行う。
/// タイルIDによるグリッド管理、範囲チェック、レイヤー操作を提供。
///
/// @code
/// using namespace sgc::tilemap;
/// Tilemap map;
/// map.addLayer(16, 16);
/// map.setTile(0, 3, 4, TileId{5});
/// auto id = map.getTile(0, 3, 4); // 5
/// @endcode

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sgc::tilemap
{

/// @brief タイルID型（0は空タイル）
using TileId = uint16_t;

/// @brief 空タイルを表す定数
inline constexpr TileId EMPTY_TILE = 0;

/// @brief 2Dグリッド上の単一タイルレイヤー
///
/// 幅×高さの2DグリッドにタイルIDを格納する。
/// 内部は1次元配列で管理し、(x, y)座標でアクセスする。
class TileLayer
{
public:
	/// @brief デフォルトコンストラクタ（0×0の空レイヤー）
	constexpr TileLayer() noexcept = default;

	/// @brief 指定サイズのレイヤーを構築する
	/// @param width 横方向のタイル数
	/// @param height 縦方向のタイル数
	/// @param fillId 初期タイルID（デフォルト: EMPTY_TILE）
	TileLayer(size_t width, size_t height, TileId fillId = EMPTY_TILE)
		: m_width{width}
		, m_height{height}
		, m_tiles(width * height, fillId)
	{
	}

	/// @brief レイヤー幅を取得
	/// @return 横方向のタイル数
	[[nodiscard]] constexpr size_t width() const noexcept { return m_width; }

	/// @brief レイヤー高さを取得
	/// @return 縦方向のタイル数
	[[nodiscard]] constexpr size_t height() const noexcept { return m_height; }

	/// @brief 座標が範囲内かを判定する
	/// @param x X座標
	/// @param y Y座標
	/// @return 範囲内ならtrue
	[[nodiscard]] constexpr bool isInBounds(size_t x, size_t y) const noexcept
	{
		return x < m_width && y < m_height;
	}

	/// @brief タイルIDを設定する
	/// @param x X座標
	/// @param y Y座標
	/// @param id 設定するタイルID
	/// @return 範囲外ならfalse
	bool setTile(size_t x, size_t y, TileId id) noexcept
	{
		if (!isInBounds(x, y))
		{
			return false;
		}
		m_tiles[y * m_width + x] = id;
		return true;
	}

	/// @brief タイルIDを取得する
	/// @param x X座標
	/// @param y Y座標
	/// @return タイルID。範囲外ならstd::nullopt
	[[nodiscard]] std::optional<TileId> getTile(size_t x, size_t y) const noexcept
	{
		if (!isInBounds(x, y))
		{
			return std::nullopt;
		}
		return m_tiles[y * m_width + x];
	}

	/// @brief レイヤー全体を指定IDで塗りつぶす
	/// @param id 塗りつぶすタイルID
	void fill(TileId id) noexcept
	{
		for (auto& tile : m_tiles)
		{
			tile = id;
		}
	}

	/// @brief レイヤーをリサイズする（既存データは保持される）
	/// @param newWidth 新しい幅
	/// @param newHeight 新しい高さ
	/// @param fillId 新領域の初期ID（デフォルト: EMPTY_TILE）
	void resize(size_t newWidth, size_t newHeight, TileId fillId = EMPTY_TILE)
	{
		std::vector<TileId> newTiles(newWidth * newHeight, fillId);
		const size_t copyW = (newWidth < m_width) ? newWidth : m_width;
		const size_t copyH = (newHeight < m_height) ? newHeight : m_height;
		for (size_t y = 0; y < copyH; ++y)
		{
			for (size_t x = 0; x < copyW; ++x)
			{
				newTiles[y * newWidth + x] = m_tiles[y * m_width + x];
			}
		}
		m_tiles = std::move(newTiles);
		m_width = newWidth;
		m_height = newHeight;
	}

	/// @brief 内部タイルデータへの読み取り専用アクセス
	/// @return タイルID配列
	[[nodiscard]] const std::vector<TileId>& tiles() const noexcept { return m_tiles; }

private:
	size_t m_width{0};                ///< 横方向タイル数
	size_t m_height{0};               ///< 縦方向タイル数
	std::vector<TileId> m_tiles;      ///< タイルID配列（行優先）
};

/// @brief 複数レイヤーを持つ2Dタイルマップ
///
/// 複数のTileLayerを重ねて管理する。
/// レイヤーインデックスと(x, y)座標でタイルにアクセスする。
class Tilemap
{
public:
	/// @brief デフォルトコンストラクタ（空のタイルマップ）
	Tilemap() = default;

	/// @brief レイヤーを追加する
	/// @param width レイヤー幅
	/// @param height レイヤー高さ
	/// @param fillId 初期タイルID（デフォルト: EMPTY_TILE）
	/// @return 追加されたレイヤーのインデックス
	size_t addLayer(size_t width, size_t height, TileId fillId = EMPTY_TILE)
	{
		m_layers.emplace_back(width, height, fillId);
		return m_layers.size() - 1;
	}

	/// @brief 既存のTileLayerを追加する
	/// @param layer 追加するレイヤー
	/// @return 追加されたレイヤーのインデックス
	size_t addLayer(TileLayer layer)
	{
		m_layers.push_back(std::move(layer));
		return m_layers.size() - 1;
	}

	/// @brief レイヤー数を取得
	/// @return レイヤー数
	[[nodiscard]] size_t layerCount() const noexcept { return m_layers.size(); }

	/// @brief レイヤーを取得する（読み取り専用）
	/// @param layerIndex レイヤーインデックス
	/// @return レイヤーへの参照。範囲外ならstd::nullopt
	[[nodiscard]] const TileLayer* getLayer(size_t layerIndex) const noexcept
	{
		if (layerIndex >= m_layers.size())
		{
			return nullptr;
		}
		return &m_layers[layerIndex];
	}

	/// @brief レイヤーを取得する（書き込み可能）
	/// @param layerIndex レイヤーインデックス
	/// @return レイヤーへのポインタ。範囲外ならnullptr
	[[nodiscard]] TileLayer* getLayerMut(size_t layerIndex) noexcept
	{
		if (layerIndex >= m_layers.size())
		{
			return nullptr;
		}
		return &m_layers[layerIndex];
	}

	/// @brief タイルIDを設定する
	/// @param layerIndex レイヤーインデックス
	/// @param x X座標
	/// @param y Y座標
	/// @param id 設定するタイルID
	/// @return 範囲外ならfalse
	bool setTile(size_t layerIndex, size_t x, size_t y, TileId id) noexcept
	{
		if (layerIndex >= m_layers.size())
		{
			return false;
		}
		return m_layers[layerIndex].setTile(x, y, id);
	}

	/// @brief タイルIDを取得する
	/// @param layerIndex レイヤーインデックス
	/// @param x X座標
	/// @param y Y座標
	/// @return タイルID。範囲外ならstd::nullopt
	[[nodiscard]] std::optional<TileId> getTile(size_t layerIndex, size_t x, size_t y) const noexcept
	{
		if (layerIndex >= m_layers.size())
		{
			return std::nullopt;
		}
		return m_layers[layerIndex].getTile(x, y);
	}

	/// @brief 全レイヤーの指定座標タイルを全てリセットする
	/// @param x X座標
	/// @param y Y座標
	/// @param id リセット先タイルID（デフォルト: EMPTY_TILE）
	void clearColumn(size_t x, size_t y, TileId id = EMPTY_TILE) noexcept
	{
		for (auto& layer : m_layers)
		{
			layer.setTile(x, y, id);
		}
	}

private:
	std::vector<TileLayer> m_layers;  ///< レイヤー配列
};

} // namespace sgc::tilemap
