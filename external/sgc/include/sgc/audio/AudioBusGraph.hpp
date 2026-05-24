#pragma once

/// @file AudioBusGraph.hpp
/// @brief 動的オーディオバス階層・エフェクトチェーン管理
///
/// 文字列名ベースのバス階層を動的に構築し、
/// エフェクトチェーン（関数リスト）を通じた信号処理を提供する。
///
/// @code
/// sgc::audio::AudioBusGraph graph;
/// auto master = graph.createBus("Master");
/// auto bgm = graph.createBus("BGM", master);
/// auto se = graph.createBus("SE", master);
/// graph.setVolume(master, 0.8f);
/// graph.setVolume(bgm, 0.5f);
/// float eff = graph.getBusVolume(bgm); // 0.4f
///
/// // エフェクトチェーン
/// graph.addEffect(bgm, [](float s) { return s * 0.5f; }); // ハーフゲイン
/// float processed = graph.processEffects(bgm, 1.0f); // 0.5f
/// @endcode

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sgc::audio
{

/// @brief バスハンドル型
using BusHandle = uint32_t;

/// @brief 無効なバスハンドル定数
inline constexpr BusHandle INVALID_BUS = 0;

/// @brief オーディオバスノード
struct BusNode
{
	std::string name;                               ///< バス名
	float volume = 1.0f;                            ///< バス音量 [0.0, 1.0]
	bool muted = false;                             ///< ミュート状態
	BusHandle parent = INVALID_BUS;                 ///< 親バスハンドル
	std::vector<BusHandle> children;                ///< 子バスハンドルリスト
	std::vector<std::function<float(float)>> effects;  ///< エフェクトチェーン
};

/// @brief 動的オーディオバスグラフ
///
/// 文字列名ベースでバス階層を構築し、音量伝播とエフェクト処理を行う。
/// AudioBusManager（固定enum）の拡張版として、動的なバス追加に対応する。
class AudioBusGraph
{
public:
	/// @brief バスを作成する
	/// @param name バス名（一意である必要がある）
	/// @param parent 親バスハンドル（INVALID_BUSならルート）
	/// @return 新しいバスハンドル（名前重複時はINVALID_BUS）
	BusHandle createBus(const std::string& name, BusHandle parent = INVALID_BUS)
	{
		/// 名前重複チェック
		if (m_nameToHandle.count(name) > 0) return INVALID_BUS;

		/// 親が有効か検証（INVALID_BUS以外は存在チェック）
		if (parent != INVALID_BUS && m_buses.count(parent) == 0) return INVALID_BUS;

		const BusHandle handle = m_nextHandle++;
		BusNode node;
		node.name = name;
		node.parent = parent;
		m_buses[handle] = std::move(node);
		m_nameToHandle[name] = handle;

		/// 親の子リストに追加
		if (parent != INVALID_BUS)
		{
			m_buses[parent].children.push_back(handle);
		}

		return handle;
	}

	/// @brief バスを名前で検索する
	/// @param name バス名
	/// @return バスハンドル（見つからない場合はINVALID_BUS）
	[[nodiscard]] BusHandle findBus(const std::string& name) const
	{
		auto it = m_nameToHandle.find(name);
		return (it != m_nameToHandle.end()) ? it->second : INVALID_BUS;
	}

	/// @brief バスの音量を設定する
	/// @param handle バスハンドル
	/// @param volume 音量 [0.0, 1.0]
	/// @return 設定できたらtrue
	bool setVolume(BusHandle handle, float volume)
	{
		auto it = m_buses.find(handle);
		if (it == m_buses.end()) return false;
		it->second.volume = std::clamp(volume, 0.0f, 1.0f);
		return true;
	}

	/// @brief バスの音量を取得する
	/// @param handle バスハンドル
	/// @return 音量（バスが無い場合は0.0）
	[[nodiscard]] float getVolume(BusHandle handle) const
	{
		auto it = m_buses.find(handle);
		return (it != m_buses.end()) ? it->second.volume : 0.0f;
	}

	/// @brief バスのミュート状態を設定する
	/// @param handle バスハンドル
	/// @param muted ミュートフラグ
	/// @return 設定できたらtrue
	bool setMuted(BusHandle handle, bool muted)
	{
		auto it = m_buses.find(handle);
		if (it == m_buses.end()) return false;
		it->second.muted = muted;
		return true;
	}

	/// @brief バスのミュート状態を取得する
	/// @param handle バスハンドル
	/// @return ミュート中ならtrue（バスが無い場合もtrue）
	[[nodiscard]] bool isMuted(BusHandle handle) const
	{
		auto it = m_buses.find(handle);
		return (it != m_buses.end()) ? it->second.muted : true;
	}

	/// @brief 親チェーンを辿った実効音量を取得する
	///
	/// 自身の音量 × 親の音量 × 祖父の音量 × ... を返す。
	/// ミュートされたバスが途中にあれば0.0を返す。
	///
	/// @param handle バスハンドル
	/// @return 実効音量 [0.0, 1.0]
	[[nodiscard]] float getBusVolume(BusHandle handle) const
	{
		float vol = 1.0f;
		BusHandle current = handle;

		/// 無限ループ防止（最大深度制限）
		constexpr int MAX_DEPTH = 32;
		for (int depth = 0; depth < MAX_DEPTH; ++depth)
		{
			auto it = m_buses.find(current);
			if (it == m_buses.end()) return 0.0f;

			if (it->second.muted) return 0.0f;
			vol *= it->second.volume;

			const BusHandle parentH = it->second.parent;
			if (parentH == INVALID_BUS || parentH == current) break;
			current = parentH;
		}

		return vol;
	}

	/// @brief エフェクトをバスに追加する
	/// @param handle バスハンドル
	/// @param effect エフェクト関数 float(float)
	/// @return 追加できたらtrue
	bool addEffect(BusHandle handle, std::function<float(float)> effect)
	{
		auto it = m_buses.find(handle);
		if (it == m_buses.end()) return false;
		it->second.effects.push_back(std::move(effect));
		return true;
	}

	/// @brief バスのエフェクトチェーンをクリアする
	/// @param handle バスハンドル
	/// @return クリアできたらtrue
	bool clearEffects(BusHandle handle)
	{
		auto it = m_buses.find(handle);
		if (it == m_buses.end()) return false;
		it->second.effects.clear();
		return true;
	}

	/// @brief エフェクトチェーンを通じてサンプルを処理する
	/// @param handle バスハンドル
	/// @param sample 入力サンプル値
	/// @return 処理後のサンプル値（バスが無い場合はそのまま返す）
	[[nodiscard]] float processEffects(BusHandle handle, float sample) const
	{
		auto it = m_buses.find(handle);
		if (it == m_buses.end()) return sample;

		float result = sample;
		for (const auto& effect : it->second.effects)
		{
			result = effect(result);
		}
		return result;
	}

	/// @brief バスのエフェクト数を取得する
	/// @param handle バスハンドル
	/// @return エフェクト数
	[[nodiscard]] std::size_t effectCount(BusHandle handle) const
	{
		auto it = m_buses.find(handle);
		return (it != m_buses.end()) ? it->second.effects.size() : 0;
	}

	/// @brief バスの子バスリストを取得する
	/// @param handle バスハンドル
	/// @return 子バスハンドルリスト（バスが無い場合は空）
	[[nodiscard]] std::vector<BusHandle> getChildren(BusHandle handle) const
	{
		auto it = m_buses.find(handle);
		return (it != m_buses.end()) ? it->second.children : std::vector<BusHandle>{};
	}

	/// @brief バスの名前を取得する
	/// @param handle バスハンドル
	/// @return バス名（見つからない場合は空文字列）
	[[nodiscard]] std::string getBusName(BusHandle handle) const
	{
		auto it = m_buses.find(handle);
		return (it != m_buses.end()) ? it->second.name : std::string{};
	}

	/// @brief 登録バス数を返す
	[[nodiscard]] std::size_t busCount() const noexcept
	{
		return m_buses.size();
	}

	/// @brief 全バスをクリアする
	void clear()
	{
		m_buses.clear();
		m_nameToHandle.clear();
	}

private:
	std::unordered_map<BusHandle, BusNode> m_buses;        ///< バスマップ
	std::unordered_map<std::string, BusHandle> m_nameToHandle;  ///< 名前→ハンドル
	BusHandle m_nextHandle = 1;                             ///< 次のハンドル値
};

} // namespace sgc::audio
