#pragma once

/// @file WriteBlame.hpp
/// @brief frame 内で「どの GameMemory byte をどの phase が最後に書いたか」を in-DLL で追跡する。
/// @details
/// `mitiru why`(分岐の根本原因特定) の causal 層 (a)。分岐が field X で出たとき「X を最後に
/// 書いたのはどの phase か」を答えるための write-blame マップを作る。
///
/// ADR 0005 準拠: DLL は host pointer を一切持たない。ゲームが自分の GameMemory bytes を渡し、
/// phase 境界の snapshot-delta(memcpy + byte 比較)で blame を **DLL 内だけ**で構築する。
/// determinism/replay は壊さない — マーカーは観測専用で GameMemory には何も書かない。
/// per-phase の memcpy/比較コストがあるため **debug 専用**(hot path に出さない)。
///
/// 使い方 (on_update 内、phase の頭で名前を付ける):
///   static mitiru::observe::WriteBlame blame;   // 1 フレーム分の状態 (frame 跨ぎで再利用)
///   blame.beginFrame(mem, size);
///   blame.phase(mem, size, "input");   /* input の処理 */
///   blame.phase(mem, size, "physics"); /* physics の処理 */
///   blame.phase(mem, size, "ai");      /* ai の処理 */
///   blame.endFrame(mem, size);
///   // 分岐 byte offset O について → blame.whoWrote(O) が phase 名を返す
///
/// 帰属の規約: `phase(name)` を「これから name の処理をする」頭で呼ぶ。各 phase()/endFrame() は
/// 「前回の境界から今までに変化した byte」を **その時点の current phase** に帰属する。同じ byte を
/// 複数 phase が書いたら **最後** に書いた phase が勝つ(= 分岐に直結する最後の書き手)。
///
/// 重要な性質: snapshot-delta なので帰属は「**値が変化した byte**」単位(同じ値で上書きしても記録
/// しない)。また float のような multi-byte field は **変化した byte だけ** が帰属される(field 先頭
/// byte が偶然 baseline と同値なら、その byte は未書込のまま)。分岐の根本原因特定にはこれで正しい
/// — 分岐 = 値の差 = 変化した byte なので、分岐 byte は必ずその書き手に帰属される。field 単位で
/// 問うときは `whoWrote`(単一 byte) でなく `whoWroteRange`(field の byte 範囲) を使う。

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mitiru::observe
{

/// @brief byte→最後に書いた phase の対応を frame 内で構築する観測器 (debug 専用)
class WriteBlame
{
public:
	/// @brief frame 開始。baseline を snapshot し blame をクリアする。
	/// @param mem  GameMemory 先頭 (DLL 所有)
	/// @param size GameMemory バイト数 (= sizeof(GameMemory))
	void beginFrame(const void* mem, std::uint32_t size)
	{
		if (mem == nullptr || size == 0) { return; }
		configure(size);
		std::memcpy(m_prev.data(), mem, size);
		std::fill(m_blame.begin(), m_blame.end(), std::uint16_t{0});  // 0 = 未書込
		m_phases.assign(1, kNone);       // frame ごとに名前表をリセット (跨ぎの無限蓄積を防ぐ)
		m_cur = internPhase("(begin)");  // 最初の phase() 前に書いた分の帰属先
	}

	/// @brief phase 境界。直前 phase の書込を帰属し、再 snapshot して current phase を name に。
	void phase(const void* mem, std::uint32_t size, const char* name)
	{
		if (mem == nullptr || size != m_size || m_size == 0) { return; }
		attributeDelta(static_cast<const std::uint8_t*>(mem));
		std::memcpy(m_prev.data(), mem, m_size);
		m_cur = internPhase(name != nullptr ? name : "(anon)");
	}

	/// @brief frame 終了。最後の phase の書込を帰属する。
	void endFrame(const void* mem, std::uint32_t size)
	{
		if (mem == nullptr || size != m_size || m_size == 0) { return; }
		attributeDelta(static_cast<const std::uint8_t*>(mem));
	}

	/// @brief offset の byte を最後に書いた phase 名。未書込/範囲外は "(none)"。
	[[nodiscard]] const std::string& whoWrote(std::uint32_t offset) const
	{
		if (offset >= m_size) { return m_phases.empty() ? kNone : m_phases[0]; }
		const std::uint16_t id = m_blame[offset];
		return (id < m_phases.size()) ? m_phases[id] : m_phases[0];
	}

	/// @brief [offset, offset+count) を書いた phase 群(重複除去・出現順)。
	/// @details field は複数 byte に跨る(例 float=4B)。その field を触った phase を集める。
	[[nodiscard]] std::vector<std::string> whoWroteRange(std::uint32_t offset, std::uint32_t count) const
	{
		std::vector<std::string> out;
		if (m_size == 0 || offset >= m_size) { return out; }
		const std::uint32_t end = (count > m_size - offset) ? m_size : offset + count;
		std::uint16_t lastId = 0xFFFF;
		for (std::uint32_t i = offset; i < end; ++i)
		{
			const std::uint16_t id = m_blame[i];
			if (id == 0 || id == lastId) { lastId = id; continue; }  // 未書込 or 連続同一は飛ばす
			const std::string& name = (id < m_phases.size()) ? m_phases[id] : m_phases[0];
			bool dup = false;
			for (const auto& s : out) { if (s == name) { dup = true; break; } }
			if (!dup) { out.push_back(name); }
			lastId = id;
		}
		return out;
	}

	/// @brief このフレームで一度でも byte を書いた phase の一覧(出現順、(none)/(begin) 含む)。
	[[nodiscard]] const std::vector<std::string>& phases() const noexcept { return m_phases; }

	/// @brief 追跡中の GameMemory バイト数。
	[[nodiscard]] std::uint32_t frameSize() const noexcept { return m_size; }

private:
	void configure(std::uint32_t size)
	{
		if (size == m_size) { return; }  // 既に同形 (frame 跨ぎ再利用) — バッファは流用
		m_size = size;
		m_prev.assign(size, std::uint8_t{0});
		m_blame.assign(size, std::uint16_t{0});
		// 内容リセット (m_phases / m_cur / blame の中身) は beginFrame が毎フレーム行う。
	}

	/// @brief m_prev と mem の差分 byte を current phase に帰属する(最後の書き手が勝つ)。
	void attributeDelta(const std::uint8_t* mem)
	{
		for (std::uint32_t i = 0; i < m_size; ++i)
		{
			if (mem[i] != m_prev[i]) { m_blame[i] = m_cur; }
		}
	}

	/// @brief phase 名→id。既存なら再利用、無ければ追加(debug 専用なので線形で十分)。
	std::uint16_t internPhase(const std::string& name)
	{
		for (std::size_t i = 0; i < m_phases.size(); ++i)
		{
			if (m_phases[i] == name) { return static_cast<std::uint16_t>(i); }
		}
		m_phases.push_back(name);
		return static_cast<std::uint16_t>(m_phases.size() - 1);
	}

	inline static const std::string kNone = "(none)";

	std::uint32_t              m_size = 0;
	std::vector<std::uint8_t>  m_prev;     ///< 直前 phase 境界での GameMemory snapshot
	std::vector<std::uint16_t> m_blame;    ///< byte→phase id (0=未書込)
	std::vector<std::string>   m_phases;   ///< phase id→名前 ([0]="(none)")
	std::uint16_t              m_cur = 0;  ///< 現在 phase id
};

}  // namespace mitiru::observe
