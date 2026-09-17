#pragma once

/// @file FrameArena.hpp
/// @brief bump アロケータ。frame / scene 単位で一括 reset する一時バッファの土台
/// @details hot path で allocation しない哲学 (`.claude/rules/mitiru-engine.md`) を
///          resource/ render/ の一時バッファへ広げるための下地となる型。
///          std::pmr::memory_resource を継承するため std::pmr::vector 等の
///          既存コンテナからそのまま利用できる。

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <new>
#include <vector>

#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru
{

/// @brief 固定容量のバンプアロケータ
/// @details コンストラクタで一度だけ確保し、以後は alloc() で先頭からオフセットを
///          進めるだけとする (個別解放は持たず reset() で丸ごと巻き戻す)。容量超過時は
///          alloc() が nullptr を返し warnOnce で 1 回だけ通知する (hot path での
///          例外コストを避けるため noexcept)。std::pmr::memory_resource 経由
///          (do_allocate) だけは pmr の契約に従い std::bad_alloc を投げる。
class Arena final : public std::pmr::memory_resource
{
public:
	/// @param capacityBytes 一度だけ確保する総バイト数
	explicit Arena(std::size_t capacityBytes)
		: m_buffer(capacityBytes)
	{
	}

	/// @brief bump 確保。align は 2 の冪であること
	/// @return 確保先頭ポインタ。容量超過なら nullptr を返す (warnOnce により
	///         最初の 1 回だけ stderr に通知される)
	[[nodiscard]] void* alloc(std::size_t bytes, std::size_t align = alignof(std::max_align_t)) noexcept
	{
		const std::size_t aligned = alignUp(m_offset, align);
		if (aligned + bytes > m_buffer.size())
		{
			debug::warnOnceFix("core.arena.overflow", "Arena capacity exceeded, allocation skipped",
				"requested capacityBytes was too small for this frame's total allocations",
				"increase the Arena's capacityBytes at construction, or reduce per-frame allocation size");
			return nullptr;
		}
		m_offset = aligned + bytes;
		return m_buffer.data() + aligned;
	}

	/// @brief 確保済み分を丸ごと巻き戻す (frame 末尾 / scene pop で呼ぶ想定)
	void reset() noexcept { m_offset = 0; }

	/// @brief 現在使用中のバイト数
	[[nodiscard]] std::size_t used() const noexcept { return m_offset; }

	/// @brief 総容量 (バイト数)
	[[nodiscard]] std::size_t capacity() const noexcept { return m_buffer.size(); }

private:
	static std::size_t alignUp(std::size_t offset, std::size_t align) noexcept
	{
		return (offset + (align - 1)) & ~(align - 1);
	}

	/// @brief std::pmr コンテナから使われた場合の経路。alloc() の noexcept 契約とは別に、
	///        pmr の契約に従い失敗時は例外を投げる
	void* do_allocate(std::size_t bytes, std::size_t align) override
	{
		void* p = alloc(bytes, align);
		if (!p) { throw std::bad_alloc{}; }
		return p;
	}

	/// @brief bump アロケータは個別解放を持たないため no-op (reset() が唯一の解放手段)
	void do_deallocate(void*, std::size_t, std::size_t) override
	{
	}

	[[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
	{
		return this == &other;
	}

	std::vector<std::byte> m_buffer;
	std::size_t            m_offset = 0;
};

/// @brief 毎フレーム reset() する一時バッファ。Arena と同型で用途のみ異なる
using FrameArena = Arena;

/// @brief scene pop 時に reset() する一時バッファ。Arena と同型で用途のみ異なる
using SceneArena = Arena;

} // namespace mitiru
