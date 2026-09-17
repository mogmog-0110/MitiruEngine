#pragma once

/// @file WorldObjects.hpp
/// @brief HE2 の `ObjectWorldChunk` / `WorldObjectStatus` 相当 (§4-1)。距離で spawn/despawn し、
/// 倒したオブジェクトの生死をビットで持つ配置レイヤー。`resource/StreamingManager.hpp` はアセット
/// の距離ロード、`SceneDocument` は静的配置だけを持ち、オブジェクトの生死状態を持つ層が無かった
/// ので追加する。flat POD (T slots / uint8 status / Vec3f pos の並行配列) にすることで
/// GameMemory に直接置ける。巻き戻しは GameMemory のバイト単位コピーで自動的に戻るため、
/// このファイルには rewind 専用のコードは要らない。
///
/// 生死は 3 値 enum ではなくビット (Enabled/Alive/Shutdown/NoRestart、★1-3) で持つ。3 値では
/// 「時間が経ったら戻る」と「スイッチで隠す」を同時に表せなかった (Dead は復活不可能な 1 状態
/// しか無い)。`restartAt` はフレーム番号で持ち、`tickRestarts()` を呼んだ側が渡す現在フレームと
/// 比較するだけなので、壁時計を挟まず決定論を保てる。

#include <cstdint>
#include <type_traits>

#include <sgc/math/Vec3.hpp>

namespace mitiru::scene
{

/// @brief status[N] の 1 byte を構成するビット (★1-3)。
enum class WorldObjectBit : std::uint8_t
{
	Enabled   = 1u << 0,  ///< false ならスイッチ OFF。update() の距離判定対象から外れる
	Alive     = 1u << 1,  ///< true = 現在アクティブ (旧 Alive 相当)
	Shutdown  = 1u << 2,  ///< true = 倒された/消費された (旧 Dead 相当)
	NoRestart = 1u << 3,  ///< true なら restart() (force なし) を拒否する
};

/// @brief 旧 3 値 API (`statusOf` / `kill`) との互換のための表現。Shutdown を最優先で判定する。
enum class WorldObjectStatus : std::uint8_t
{
	Dormant = 0,  ///< Enabled かつ Alive でも Shutdown でもない。update() の候補になりうる
	Alive   = 1,  ///< Alive ビットが立っている
	Dead    = 2,  ///< Shutdown ビットが立っている (NoRestart の有無は問わない)
};

/// @brief T 型のオブジェクトを最大 N 個、位置と生死状態つきで持つ POD 配置レイヤー。
/// @tparam T スロットのペイロード型 (`mitiru::module::spawnFromJson` で埋める想定、flat POD)
/// @tparam N 最大スロット数
template <class T, int N>
struct WorldObjects
{
	static_assert(N > 0, "WorldObjects<T,N>: N は 1 以上");
	static_assert(std::is_trivially_copyable_v<T>,
		"WorldObjects<T,N>: T は flat POD である必要があります (GameMemory 経由の巻き戻しのため)");

	/// @brief restartAt に立てる「予約なし」の番兵。frame 0 への予約と区別するため 0 は使わない。
	static constexpr std::uint32_t kNoRestartScheduled = 0xFFFFFFFFu;

	T             slots[N]{};
	std::uint8_t  status[N]{};       ///< WorldObjectBit の組み合わせ (GameMemory 越しに素の byte で持つ)
	sgc::Vec3f    pos[N]{};
	std::uint16_t spawnPriority[N]{};  ///< 小さいほど先に起こす (HE2 の spawnPriority、既定 0)
	std::uint16_t objState[N]{};       ///< HE2 の GetObjectState(stateId) 相当。ゲーム定義の値 (扉の開閉等)
	std::uint32_t restartAt[N]{};      ///< scheduleRestart() が予約したフレーム番号。既定値は不定だが
	                                    ///< Shutdown が立っていない間は tickRestarts() から見えない
	std::uint32_t count = 0;    ///< 使用中スロット数 (末尾から詰める。途中の空きは作らない)

	/// @brief スロットを 1 つ確保して Enabled (旧 Dormant 相当) で登録する。満杯なら false。
	/// priority が小さいほど、予算つき update() で先に Alive へ起こされる (HE2 の spawnPriority)。
	[[nodiscard]] bool spawn(const T& value, const sgc::Vec3f& worldPos, std::uint16_t priority = 0) noexcept
	{
		if (count >= static_cast<std::uint32_t>(N)) { return false; }
		const std::uint32_t i = count++;
		slots[i]         = value;
		pos[i]           = worldPos;
		spawnPriority[i] = priority;
		objState[i]      = 0;
		restartAt[i]     = kNoRestartScheduled;
		status[i]        = static_cast<std::uint8_t>(WorldObjectBit::Enabled);
		return true;
	}

	/// @brief activateRange < deactivateRange のヒステリシスつき距離判定 (★1-2)。境界ちょうどに
	/// 立ち続けても 1 本の閾値と違って毎フレーム反転しない。budgetPerFrame は 1 フレームに
	/// Alive へ上げてよい最大数 (0 = 無制限)。予算切れの候補は次フレームへ持ち越されるが
	/// 状態としては保持しない (同じ判定式を毎フレーム再評価するだけ)。決定論のため、予算で
	/// どれを選ぶかは常に (spawnPriority 昇順, 添字昇順) の辞書順で固定する。
	/// despawn (Alive→非 Alive) 側は予算の対象外 (明滅の原因は「出しすぎ」側だけなので無制限のまま)。
	/// Enabled が落ちている (setEnabled(false)) スロットと Shutdown 済みのスロットは候補から外れる。
	void update(const sgc::Vec3f& viewerPos, float activateRange, float deactivateRange,
		std::uint32_t budgetPerFrame = 0) noexcept
	{
		const float activateSq   = activateRange * activateRange;
		const float deactivateSq = deactivateRange * deactivateRange;
		const auto  aliveBit     = static_cast<std::uint8_t>(WorldObjectBit::Alive);
		const auto  enabledBit   = static_cast<std::uint8_t>(WorldObjectBit::Enabled);
		const auto  shutdownBit  = static_cast<std::uint8_t>(WorldObjectBit::Shutdown);

		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (!(status[i] & aliveBit)) { continue; }
			const sgc::Vec3f d = pos[i] - viewerPos;
			if (d.dot(d) > deactivateSq) { status[i] = static_cast<std::uint8_t>(status[i] & ~aliveBit); }
		}

		std::uint32_t candidates[N];
		std::uint32_t candidateCount = 0;
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const std::uint8_t s = status[i];
			if (!(s & enabledBit) || (s & aliveBit) || (s & shutdownBit)) { continue; }
			const sgc::Vec3f d = pos[i] - viewerPos;
			if (d.dot(d) <= activateSq) { candidates[candidateCount++] = i; }
		}

		// 挿入ソート (安定): candidates は添字昇順で積んだので、priority が等しい候補は
		// 添字昇順のまま残る。候補数は通常小さい (新たに視界に入った分だけ)。
		for (std::uint32_t a = 1; a < candidateCount; ++a)
		{
			const std::uint32_t key = candidates[a];
			std::uint32_t b = a;
			while (b > 0 && spawnPriority[candidates[b - 1]] > spawnPriority[key])
			{
				candidates[b] = candidates[b - 1];
				--b;
			}
			candidates[b] = key;
		}

		const std::uint32_t limit =
			(budgetPerFrame == 0) ? candidateCount : (budgetPerFrame < candidateCount ? budgetPerFrame : candidateCount);
		for (std::uint32_t k = 0; k < limit; ++k)
		{
			status[candidates[k]] = static_cast<std::uint8_t>(status[candidates[k]] | aliveBit);
		}
	}

	/// @brief 旧シグネチャ (ヒステリシス・優先度・予算なし)。既存呼び出しはそのまま動く。
	/// activate == deactivate、予算無制限で新シグネチャへ転送する。
	void update(const sgc::Vec3f& viewerPos, float activationRange) noexcept
	{
		update(viewerPos, activationRange, activationRange, 0);
	}

	/// @brief i 番目を倒す/消費する (★1-3)。Alive を落とし Shutdown を立てる。noRestart=true なら
	/// 以後 restart(i) (force なし) を拒否する (旧 kill() の「二度と戻らない」はこれで表す)。
	/// 範囲外は no-op。restartAt は予約なしへ戻す (呼び出し側が scheduleRestart で明示するまで
	/// tickRestarts() は触らない)。
	void shutdown(std::uint32_t i, bool noRestart = false) noexcept
	{
		if (i >= count) { return; }
		const auto shutdownBit = static_cast<std::uint8_t>(WorldObjectBit::Shutdown);
		const auto aliveBit    = static_cast<std::uint8_t>(WorldObjectBit::Alive);
		status[i] = static_cast<std::uint8_t>(status[i] | shutdownBit);
		status[i] = static_cast<std::uint8_t>(status[i] & ~aliveBit);
		if (noRestart)
		{
			status[i] = static_cast<std::uint8_t>(status[i] | static_cast<std::uint8_t>(WorldObjectBit::NoRestart));
		}
		restartAt[i] = kNoRestartScheduled;
	}

	/// @brief i 番目の Shutdown を解く。NoRestart が立っている場合、force=false なら拒否して
	/// false を返す (「時間が経ったら戻る」と「スイッチ扱いで完全に殺す」を区別する)。
	/// 復帰後は Enabled 次第で update() の候補に戻るだけで、Alive を直接立てはしない
	/// (距離判定を経由させる。復帰した瞬間に視界外でも見える、という事故を防ぐ)。
	bool restart(std::uint32_t i, bool force = false) noexcept
	{
		if (i >= count) { return false; }
		if ((status[i] & static_cast<std::uint8_t>(WorldObjectBit::NoRestart)) && !force) { return false; }
		status[i]    = static_cast<std::uint8_t>(status[i] & ~static_cast<std::uint8_t>(WorldObjectBit::Shutdown));
		restartAt[i] = kNoRestartScheduled;
		return true;
	}

	/// @brief i 番目を afterFrames フレーム後 (currentFrame 基準) に自動 restart する予約を積む。
	/// 時計を挟まずフレーム番号だけで決めるので、決定論リプレイでも同じ結果になる。
	void scheduleRestart(std::uint32_t i, std::uint32_t currentFrame, std::uint32_t afterFrames) noexcept
	{
		if (i >= count) { return; }
		restartAt[i] = currentFrame + afterFrames;
	}

	/// @brief 毎フレーム呼ぶと、予約が来ていて Shutdown かつ NoRestart でないスロットを restart する。
	/// update() の距離判定とは独立 (距離を跨がず「時間で戻る」だけを扱う)。
	void tickRestarts(std::uint32_t currentFrame) noexcept
	{
		const auto shutdownBit  = static_cast<std::uint8_t>(WorldObjectBit::Shutdown);
		const auto noRestartBit = static_cast<std::uint8_t>(WorldObjectBit::NoRestart);
		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (restartAt[i] == kNoRestartScheduled) { continue; }
			if (!(status[i] & shutdownBit) || (status[i] & noRestartBit)) { continue; }
			if (currentFrame < restartAt[i]) { continue; }
			restart(i);
		}
	}

	/// @brief スイッチ (★1-3)。false にすると Alive も落ち、update() の候補からも外れる
	/// (「範囲内でも出ない」を表す。Shutdown とは独立なビットなので復活可否には影響しない)。
	void setEnabled(std::uint32_t i, bool enabled) noexcept
	{
		if (i >= count) { return; }
		const auto enabledBit = static_cast<std::uint8_t>(WorldObjectBit::Enabled);
		if (enabled) { status[i] = static_cast<std::uint8_t>(status[i] | enabledBit); }
		else
		{
			status[i] = static_cast<std::uint8_t>(status[i] & ~enabledBit);
			status[i] = static_cast<std::uint8_t>(status[i] & ~static_cast<std::uint8_t>(WorldObjectBit::Alive));
		}
	}

	[[nodiscard]] std::uint16_t objectState(std::uint32_t i) const noexcept
	{
		return (i < count) ? objState[i] : 0;
	}

	void setObjectState(std::uint32_t i, std::uint16_t value) noexcept
	{
		if (i < count) { objState[i] = value; }
	}

	/// @brief 旧 API 互換 (3 値 enum 時代の kill)。「倒したら二度と戻らない」= shutdown + NoRestart。
	void kill(std::uint32_t i) noexcept { shutdown(i, /*noRestart=*/true); }

	/// @brief 旧 API 互換。Shutdown を最優先で Dead に潰す (NoRestart の有無は問わない)。
	[[nodiscard]] WorldObjectStatus statusOf(std::uint32_t i) const noexcept
	{
		if (i >= count) { return WorldObjectStatus::Dead; }
		if (status[i] & static_cast<std::uint8_t>(WorldObjectBit::Shutdown)) { return WorldObjectStatus::Dead; }
		if (status[i] & static_cast<std::uint8_t>(WorldObjectBit::Alive)) { return WorldObjectStatus::Alive; }
		return WorldObjectStatus::Dormant;
	}
};

}  // namespace mitiru::scene
