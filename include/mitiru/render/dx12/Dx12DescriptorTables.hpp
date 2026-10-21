#pragma once

/// @file Dx12DescriptorTables.hpp
/// @brief 記述子の組が写している資源を世代で覚え、作り替えられていれば書き込み直す (ADR 0088)
/// @details 資源を作り替える側は記述子を知らず、記述子を持つ側が「何を写しているか」を宣言する。
///          組ごとに { 名前、写す物の一覧を返す関数、書き込む関数 } を登録し、フレームの頭と提出の直前に
///          登録簿が世代を比べて、違う組だけを書き込み直す。共有の表を書き換える前には GPU の完了を 1 回待つ。
///          Dawn の bind group も同じ形 (写す物の世代が違えば作り替える) で置き換えられる。

#ifdef _WIN32

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <d3d12.h>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/gfx/dx12/Dx12ResourceEpoch.hpp>

namespace mitiru::render::dx12
{

/// @brief 記述子の組が写している物 (資源と、記述子を置いたヒープ) を名前つきで並べる
class DescriptorSources
{
public:
	static constexpr std::size_t kCapacity = 16;

	struct Entry
	{
		const char* name = "";
		const ID3D12Object* object = nullptr;
	};

	/// @brief 写す物を足す。null は「まだ無い」の印として世代 0 で数える。容量を超えた分は無視する
	void add(const char* name, const ID3D12Object* object) noexcept
	{
		if (m_count >= kCapacity)
		{
			return;
		}
		m_entries[m_count++] = Entry{name, object};
	}

	void clear() noexcept { m_count = 0; }
	[[nodiscard]] std::size_t size() const noexcept { return m_count; }
	[[nodiscard]] const Entry& operator[](std::size_t index) const noexcept { return m_entries[index]; }

private:
	std::array<Entry, kCapacity> m_entries{};
	std::size_t m_count = 0;
};

/// @brief 記述子を書いた時点の世代の控え
class WrittenEpochs
{
public:
	/// @brief 今の写す物が控えと違う最初の名前。同じなら nullptr。一度も書いていなければ kUnwritten
	[[nodiscard]] const char* firstChange(const DescriptorSources& now) const noexcept
	{
		if (!m_valid)
		{
			return kUnwritten;
		}
		if (now.size() != m_count)
		{
			return "(数)";
		}
		for (std::size_t i = 0; i < m_count; ++i)
		{
			if (gfx::objectEpoch(now[i].object) != m_epochs[i])
			{
				return now[i].name;
			}
		}
		return nullptr;
	}

	/// @brief 今の世代を控える。記述子を書き終えた直後に呼ぶ
	void commit(const DescriptorSources& now) noexcept
	{
		m_count = now.size();
		for (std::size_t i = 0; i < m_count; ++i)
		{
			m_epochs[i] = gfx::objectEpoch(now[i].object);
		}
		m_valid = true;
	}

	/// @brief 控えを捨てる。次の比較は必ず「違う」になる
	void forget() noexcept { m_valid = false; }

	[[nodiscard]] bool written() const noexcept { return m_valid; }

	static constexpr const char* kUnwritten = "(未記入)";

private:
	std::array<std::uint64_t, DescriptorSources::kCapacity> m_epochs{};
	std::size_t m_count = 0;
	bool m_valid = false;
};

/// @brief 登録簿を見る時点。FrameHead は beginFrame の頭、Lazy は資源を遅れて作った直後、PreSubmit は提出の直前
enum class RefreshPhase : std::uint8_t
{
	FrameHead,
	Lazy,
	PreSubmit,
};

/// @brief 記述子の組の登録簿
class DescriptorTableRegistry
{
public:
	using Collect = std::function<void(DescriptorSources&)>;
	using Write = std::function<void()>;
	using Idle = std::function<void()>;

	struct Stats
	{
		std::uint64_t sweeps = 0;
		std::uint64_t rewrites = 0;       ///< 書き替えた組の延べ数 (最初の書き込みを含む)
		std::uint64_t lateRewrites = 0;   ///< 提出の直前に初めて違いが見つかった組の延べ数
	};

	/// @brief 組を登録する。collect は今の写す物を返し、write はその物へ記述子を書く。登録した順に見る
	std::size_t add(const char* name, Collect collect, Write write)
	{
		m_tables.push_back(Table{name, std::move(collect), std::move(write), {}, 0});
		return m_tables.size() - 1;
	}

	/// @brief 写す物の世代が違う組を書き込み直す。最初の書き込みの前に idle を 1 回だけ呼ぶ (GPU の完了待ち)
	/// @return 書き替えた組の数
	std::size_t refresh(RefreshPhase phase, const Idle& idle)
	{
		++m_stats.sweeps;
		std::size_t rewritten = 0;
		bool waited = false;
		DescriptorSources now;
		for (Table& table : m_tables)
		{
			now.clear();
			table.collect(now);
			const char* changed = table.written.firstChange(now);
			if (changed == nullptr)
			{
				continue;
			}
			if (!waited)
			{
				if (idle) { idle(); }
				waited = true;
			}
			if (phase == RefreshPhase::PreSubmit && table.written.written())
			{
				noteLate(table.name, changed);
			}
			table.write();
			table.written.commit(now);
			++table.rewrites;
			++rewritten;
			++m_stats.rewrites;
		}
		return rewritten;
	}

	[[nodiscard]] const Stats& stats() const noexcept { return m_stats; }
	[[nodiscard]] std::size_t tableCount() const noexcept { return m_tables.size(); }
	[[nodiscard]] const char* tableName(std::size_t index) const noexcept { return m_tables[index].name; }

	/// @brief 名前の組を書き替えた回数。無ければ 0
	[[nodiscard]] std::uint64_t rewritesOf(std::string_view name) const noexcept
	{
		for (const Table& table : m_tables)
		{
			if (name == table.name) { return table.rewrites; }
		}
		return 0;
	}

	/// @brief 提出の直前に見つかった最後の違いの、組の名前と写す物の名前。無ければ空
	[[nodiscard]] const std::string& lastLateTable() const noexcept { return m_lastLateTable; }
	[[nodiscard]] const std::string& lastLateSource() const noexcept { return m_lastLateSource; }

private:
	struct Table
	{
		const char* name;
		Collect collect;
		Write write;
		WrittenEpochs written;
		std::uint64_t rewrites;
	};

	void noteLate(const char* table, const char* source)
	{
		++m_stats.lateRewrites;
		m_lastLateTable = table;
		m_lastLateSource = source;
#ifndef NDEBUG
		debug::warnOnce(std::string("dx12.tables.late.") + table + "." + source,
		                std::string("記述子の組 '") + table + "' が写す '" + source +
		                    "' を途中で作り替えたので、作り替えた直後に refresh を呼んでください");
#endif
	}

	std::vector<Table> m_tables;
	Stats m_stats;
	std::string m_lastLateTable;
	std::string m_lastLateSource;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
