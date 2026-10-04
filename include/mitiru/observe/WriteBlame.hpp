#pragma once

/// @file WriteBlame.hpp
/// @brief frame 内で「どの GameMemory byte をどの phase が最後に書いたか」を in-DLL で追跡する。
/// @details
/// `mitiru why`(分岐の根本原因特定) の causal 層 (a)。分岐が field X で出たとき「X を最後に
/// 書いたのはどの phase か」を答えるための write-blame マップを作る。
///
/// DLL は host pointer を一切持たない。ゲームが自分の GameMemory bytes を渡し、
/// phase 境界の snapshot-delta(memcpy + byte 比較)で blame を **DLL 内だけ**で構築する。
/// determinism/replay は壊さない。マーカーは観測専用で GameMemory には何も書かない。
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
///。分岐 = 値の差 = 変化した byte なので、分岐 byte は必ずその書き手に帰属される。field 単位で
/// 問うときは `whoWrote`(単一 byte) でなく `whoWroteRange`(field の byte 範囲) を使う。
///
/// 「同値書き込み」の扱い(§5-1): byte diff は値が変わらない限り原理的に何も記録しない。だが
/// `mitiru why` にとって「一度も触られていない」と「触ったが同じ値だった」は別の事実であり、
/// 前者は分岐の原因になり得ないが後者は「なぜ変えなかったか」の手がかりになる。この区別は
/// diff だけでは付かない(bytes が同じなら痕跡が残らない)ため、書く側が `setIfNeq` を経由して
/// 明示的に申告する。`setIfNeq` は Bevy の `set_if_neq` と同じ比較して代入するパターンで、
/// 変更が無かった呼び出しだけ `noteSameValueTouch` で current phase を記録する。
///
/// site 付き帰属(§5-2): `noteSite` で offset ごとに「最後に変更した file:line」を持てる。
/// `MITIRU_WRITE_BLAME(blame, offset, field, value)` マクロは `setIfNeq` と同じ比較代入をした上で、
/// 実際に変更があった時だけ `__FILE__`/`__LINE__` を記録する(同値書き込みに site は付かない=
/// 「変えていない」のだから当然)。DLL 境界を越えられるよう `WriteSite` は固定長 POD にしてある。
///
/// 順序未定義の衝突検出(§5-3): `whoWrote`/`everWrote` は「最後に書いた者」か「累積で書きうる者」
/// のどちらかで、「**同じフレームで**複数 phase が書いたか」には答えない(1 phase が 2 回書いても
/// 1 phase が 1 回書いても最後の値しか見えない)。`conflictingPhases` はフレームごとにリセットする
/// bitmask (`m_frameWriters`) を別に持ち、そのフレームで実際に値を変えた **異なる phase の数** を
/// 数える。2 phase 以上ならどちらが先に走ったかで結果が変わりうる「順序未定義の衝突」。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru::observe
{

/// @brief byte offset ごとに「最後に変更した場所」を持つ固定長 POD (§5-2)。DLL 境界を跨げる。
struct WriteSite
{
	char          file[64]{};  // 呼び出し側が詰める短縮パス(basename 推奨)。空 = 未設定
	std::uint16_t line = 0;
};

/// @brief byte が「未書込 / 同値書き込みのみ / 値が変化」のどれかを表す (§5-1)。
enum class TouchState : std::uint8_t { Untouched, SameValue, Changed };

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
		std::fill(m_blame.begin(), m_blame.end(), std::uint16_t{0});      // 0 = 未書込
		std::fill(m_sameValue.begin(), m_sameValue.end(), std::uint16_t{0});  // frame ごとにリセット (§5-1)
		std::fill(m_frameWriters.begin(), m_frameWriters.end(), std::uint32_t{0});  // frame ごとにリセット (§5-3)
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

	/// @brief インスタンス生成以降(フレーム跨ぎ、beginFrame でリセットしない)にその byte を
	/// 一度でも書いた phase の bitmask。phase は最大 32 個まで(それ以降は無視)。whoWrote() が
	/// 「今フレームの最後の書き手」なのに対し、こちらは「書きうる者」の集合を答える。
	/// phase id は各フレームで phase() を呼ぶ順に振られるため、フレームごとに同じ順で
	/// phase() を呼ぶ使い方 (通常の固定パイプライン) でのみ意味のある集合になる。
	[[nodiscard]] std::uint32_t everWrote(std::uint32_t offset) const noexcept
	{
		return (offset < m_everWritten.size()) ? m_everWritten[offset] : 0u;
	}

	/// @brief everWrote() のビットマスクを phase 名の一覧に変換する ((none) は含めない)。
	[[nodiscard]] std::vector<std::string> everWrotePhases(std::uint32_t offset) const
	{
		std::vector<std::string> out;
		const std::uint32_t mask = everWrote(offset);
		const std::size_t   idLimit = std::min<std::size_t>(m_phases.size(), 32);
		for (std::size_t id = 1; id < idLimit; ++id)  // id=0 は "(none)" なので除外
		{
			if ((mask & (1u << id)) != 0) { out.push_back(m_phases[id]); }
		}
		return out;
	}

	/// @brief 追跡中の GameMemory バイト数。
	[[nodiscard]] std::uint32_t frameSize() const noexcept { return m_size; }

	/// @brief 「触ったが値は変えなかった」ことを current phase に記録する (§5-1)。
	/// byte diff では原理的に検出できない事実なので、`setIfNeq` (下記自由関数) から明示的に呼ぶ。
	/// 同フレーム内で既に値が変化している byte には上書きしない (Changed が SameValue に劣化しない)。
	void noteSameValueTouch(std::uint32_t offset, std::uint32_t count) noexcept
	{
		if (m_size == 0 || offset >= m_size) { return; }
		const std::uint32_t end = (count > m_size - offset) ? m_size : offset + count;
		for (std::uint32_t i = offset; i < end; ++i)
		{
			if (m_blame[i] == 0) { m_sameValue[i] = m_cur; }
		}
	}

	/// @brief offset の byte が今フレームどう触られたか。
	[[nodiscard]] TouchState touchState(std::uint32_t offset) const noexcept
	{
		if (offset >= m_size) { return TouchState::Untouched; }
		if (m_blame[offset] != 0) { return TouchState::Changed; }
		if (m_sameValue[offset] != 0) { return TouchState::SameValue; }
		return TouchState::Untouched;
	}

	/// @brief `touchState(offset) == SameValue` のときにそれを行った phase 名。それ以外は "(none)"。
	[[nodiscard]] const std::string& whoTouchedSameValue(std::uint32_t offset) const
	{
		if (offset >= m_size || m_sameValue[offset] == 0) { return kNone; }
		const std::uint16_t id = m_sameValue[offset];
		return (id < m_phases.size()) ? m_phases[id] : m_phases[0];
	}

	/// @brief offset に「最後に変更した場所」を記録する (§5-2)。実際に値が変わった呼び出しからのみ
	/// 呼ぶ想定 (`MITIRU_WRITE_BLAME` マクロ参照)。同値書き込みに site は付けない。
	void noteSite(std::uint32_t offset, const WriteSite& site) noexcept
	{
		if (m_size == 0 || offset >= m_size) { return; }
		configureSites();
		m_sites[offset] = site;
	}

	/// @brief offset の最後の書き込み site。未記録なら nullptr。
	[[nodiscard]] const WriteSite* siteOf(std::uint32_t offset) const noexcept
	{
		if (m_sites.empty() || offset >= m_size) { return nullptr; }
		const WriteSite& s = m_sites[offset];
		return (s.file[0] != '\0') ? &s : nullptr;
	}

	/// @brief このフレーム中に offset を変更した異なる phase が 2 つ以上あったか(§5-3)。
	/// 「どちらが先に走るか」で結果が変わる = 順序未定義の衝突。
	[[nodiscard]] bool hasConflict(std::uint32_t offset) const noexcept
	{
		return popcount(frameWriterMask(offset)) >= 2;
	}

	/// @brief このフレーム中に offset を変更した phase の一覧(出現順)。`hasConflict` と対にして使う。
	[[nodiscard]] std::vector<std::string> conflictingPhases(std::uint32_t offset) const
	{
		std::vector<std::string> out;
		const std::uint32_t mask = frameWriterMask(offset);
		const std::size_t   idLimit = std::min<std::size_t>(m_phases.size(), 32);
		for (std::size_t id = 1; id < idLimit; ++id)
		{
			if ((mask & (1u << id)) != 0) { out.push_back(m_phases[id]); }
		}
		return out;
	}

private:
	void configure(std::uint32_t size)
	{
		if (size == m_size) { return; }  // 既に同形 (frame 跨ぎ再利用) — バッファは流用
		m_size = size;
		m_prev.assign(size, std::uint8_t{0});
		m_blame.assign(size, std::uint16_t{0});
		m_sameValue.assign(size, std::uint16_t{0});
		m_everWritten.assign(size, std::uint32_t{0});    // everWrote はフレーム跨ぎで蓄積 (beginFrame でリセットしない)
		m_frameWriters.assign(size, std::uint32_t{0});   // conflict 判定はフレームごとにリセットする (§5-3)
		if (!m_sites.empty()) { m_sites.assign(size, WriteSite{}); }  // site 追跡は使われていれば形も追随
		// 内容リセット (m_phases / m_cur / blame の中身) は beginFrame が毎フレーム行う。
	}

	/// @brief site 追跡バッファを初回利用時にだけ確保する (使わないゲームでコストを払わせない)。
	void configureSites()
	{
		if (m_sites.size() != m_size) { m_sites.assign(m_size, WriteSite{}); }
	}

	/// @brief m_prev と mem の差分 byte を current phase に帰属する(最後の書き手が勝つ)。
	void attributeDelta(const std::uint8_t* mem)
	{
		for (std::uint32_t i = 0; i < m_size; ++i)
		{
			if (mem[i] != m_prev[i])
			{
				m_blame[i] = m_cur;
				if (m_cur < 32)
				{
					m_everWritten[i]   |= (std::uint32_t{1} << m_cur);
					m_frameWriters[i]  |= (std::uint32_t{1} << m_cur);
				}
				else
				{
					mitiru::debug::warnOnce("writeblame.phase.limit",
						"1 フレームの phase が 32 個を超えたので、33 個目からは conflict と everWrote に数えません (whoWrote は使えます)。");
				}
			}
		}
	}

	/// @brief offset のフレーム内 writer bitmask (範囲外は 0)。
	[[nodiscard]] std::uint32_t frameWriterMask(std::uint32_t offset) const noexcept
	{
		return (offset < m_frameWriters.size()) ? m_frameWriters[offset] : 0u;
	}

	[[nodiscard]] static int popcount(std::uint32_t mask) noexcept
	{
		int n = 0;
		while (mask != 0) { mask &= (mask - 1); ++n; }
		return n;
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
	std::vector<std::uint8_t>  m_prev;        ///< 直前 phase 境界での GameMemory snapshot
	std::vector<std::uint16_t> m_blame;       ///< byte→phase id (0=未書込、フレームごとにリセット)
	std::vector<std::uint16_t> m_sameValue;   ///< byte→同値書き込みをした phase id (§5-1、フレームごとにリセット)
	std::vector<std::uint32_t> m_everWritten; ///< byte→書いた phase id の bitmask (フレーム跨ぎで蓄積)
	std::vector<std::uint32_t> m_frameWriters;///< byte→今フレーム書いた phase id の bitmask (§5-3、フレームごとにリセット)
	std::vector<std::string>   m_phases;      ///< phase id→名前 ([0]="(none)")
	std::vector<WriteSite>     m_sites;       ///< byte→最後の書き込み site (§5-2、未使用なら空のまま)
	std::uint16_t              m_cur = 0;     ///< 現在 phase id
};

/// @brief 比較して代入する (Bevy の `set_if_neq` 相当)。値が変わらない呼び出しは代入自体を
/// 省く代わりに `blame` へ「触ったが同値」を申告する。変化した呼び出しは byte diff が自然に
/// 拾うので blame への明示申告は不要 (attributeDelta が phase 境界でまとめて処理する)。
/// @return 実際に値を変更したら true。
template <class T>
bool setIfNeq(WriteBlame& blame, std::uint32_t offset, T& field, const T& value) noexcept
{
	static_assert(std::is_trivially_copyable_v<T>, "setIfNeq: T は GameMemory に置ける flat POD であること");
	if (field == value) { blame.noteSameValueTouch(offset, static_cast<std::uint32_t>(sizeof(T))); return false; }
	field = value;
	return true;
}

}  // namespace mitiru::observe

/// @brief `setIfNeq` と同じ比較代入をした上で、実際に変更があった時だけ現在の呼び出し site
/// (__FILE__/__LINE__) を `blame` に記録するマクロ (§5-2)。ヘッダの都合で TU ごとに `__FILE__`
/// が長くなりがちなため、`WriteSite::file` に収まるよう末尾から切り詰める。
#define MITIRU_WRITE_BLAME(blame, offset, field, value)                                     \
	do                                                                                        \
	{                                                                                          \
		if (!((field) == (value)))                                                            \
		{                                                                                       \
			(field) = (value);                                                                  \
			::mitiru::observe::WriteSite _mitiru_write_site{};                                  \
			const char* _mitiru_file = __FILE__;                                                \
			const std::size_t _mitiru_flen = std::char_traits<char>::length(_mitiru_file);      \
			const std::size_t _mitiru_cap = sizeof(_mitiru_write_site.file) - 1;                \
			const char* _mitiru_src = (_mitiru_flen > _mitiru_cap) ? (_mitiru_file + _mitiru_flen - _mitiru_cap) : _mitiru_file; \
			std::strncpy(_mitiru_write_site.file, _mitiru_src, _mitiru_cap);                    \
			_mitiru_write_site.line = static_cast<std::uint16_t>(__LINE__);                     \
			(blame).noteSite((offset), _mitiru_write_site);                                     \
		}                                                                                       \
	} while (0)
