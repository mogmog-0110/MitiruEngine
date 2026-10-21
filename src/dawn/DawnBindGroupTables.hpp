#pragma once

// WebGPU の bind group を、参照する GPU 資源の変更に合わせて作り直す仕組みの宣言。
// ADR 0091 に基づく。DX12 版は ADR 0088 の Dx12DescriptorTables.hpp。
// GPU 資源を作り替える側は bind group を知らず、bind group を持つ側が参照する資源を登録する。
// DX12 版は世代番号を比べる。WebGPU 版は資源の参照を控え、ハンドルの比較で変更を見分ける。
// 控えが参照を持つ間は、同じハンドルが別の資源に再利用されない。
// 名前と構成は DX12 版に揃えている。
// bind group は作成後に変わらず、使用中の古い資源は提出済みのコマンドが参照を保つため、書き替えの前に GPU を待たない。
// PreSubmit は記録を始める前に行う。bind group は SetBindGroup の時点で決まるため、記録前に揃える。

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru::render::dawn_detail
{

/// bind group が参照する資源を名前付きで並べる。参照は WrittenRefs が持つ。
class DescriptorSources
{
public:
	static constexpr std::size_t kCapacity = 16;

	enum class Kind : std::uint8_t { Texture, TextureView, Buffer, Sampler };

	struct Entry
	{
		const char* name = "";
		Kind kind = Kind::Texture;
		void* handle = nullptr;
	};

	/// 参照する資源を追加する。null は未作成として数え、容量を超えた分は無視する。
	void add(const char* name, const wgpu::Texture& object) noexcept { push(name, Kind::Texture, object.Get()); }
	void add(const char* name, const wgpu::TextureView& object) noexcept { push(name, Kind::TextureView, object.Get()); }
	void add(const char* name, const wgpu::Buffer& object) noexcept { push(name, Kind::Buffer, object.Get()); }
	void add(const char* name, const wgpu::Sampler& object) noexcept { push(name, Kind::Sampler, object.Get()); }

	void clear() noexcept { m_count = 0; }
	[[nodiscard]] std::size_t size() const noexcept { return m_count; }
	[[nodiscard]] const Entry& operator[](std::size_t index) const noexcept { return m_entries[index]; }

	/// null を含む bind group は作れないため、write で作成を見送る判断に使う。
	[[nodiscard]] bool anyMissing() const noexcept
	{
		for (std::size_t i = 0; i < m_count; ++i)
		{
			if (m_entries[i].handle == nullptr) { return true; }
		}
		return false;
	}

private:
	void push(const char* name, Kind kind, void* handle) noexcept
	{
		if (m_count >= kCapacity) { return; }
		m_entries[m_count++] = Entry{name, kind, handle};
	}

	std::array<Entry, kCapacity> m_entries{};
	std::size_t m_count = 0;
};

/// bind group を作成した時点の資源と参照を控える。DX12 版の WrittenEpochs に当たる。
class WrittenRefs
{
public:
	WrittenRefs() = default;
	WrittenRefs(const WrittenRefs&) = delete;
	WrittenRefs& operator=(const WrittenRefs&) = delete;
	WrittenRefs(WrittenRefs&& other) noexcept { takeFrom(other); }
	WrittenRefs& operator=(WrittenRefs&& other) noexcept
	{
		if (this != &other)
		{
			releaseAll();
			takeFrom(other);
		}
		return *this;
	}
	~WrittenRefs() { releaseAll(); }

	/// 現在の資源が控えと異なる最初の名前。同じなら nullptr、まだ控えていなければ kUnwritten。
	[[nodiscard]] const char* firstChange(const DescriptorSources& now) const noexcept
	{
		if (!m_valid) { return kUnwritten; }
		if (now.size() != m_count) { return "(数)"; }
		for (std::size_t i = 0; i < m_count; ++i)
		{
			if (now[i].handle != m_entries[i].handle) { return now[i].name; }
		}
		return nullptr;
	}

	/// 現在の資源と参照を控える。bind group の作成直後に呼ぶ。
	void commit(const DescriptorSources& now) noexcept
	{
		std::array<DescriptorSources::Entry, DescriptorSources::kCapacity> next{};
		const std::size_t count = now.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			next[i] = now[i];
			retain(next[i]);
		}
		releaseAll();
		m_entries = next;
		m_count = count;
		m_valid = true;
	}

	/// 控えを捨てる。次の比較は必ず変更ありになる。
	void forget() noexcept
	{
		releaseAll();
		m_valid = false;
	}

	[[nodiscard]] bool written() const noexcept { return m_valid; }

	static constexpr const char* kUnwritten = "(未記入)";

private:
	using Kind = DescriptorSources::Kind;

	static void retain(const DescriptorSources::Entry& e) noexcept
	{
		if (e.handle == nullptr) { return; }
		switch (e.kind)
		{
		case Kind::Texture: wgpuTextureAddRef(static_cast<WGPUTexture>(e.handle)); break;
		case Kind::TextureView: wgpuTextureViewAddRef(static_cast<WGPUTextureView>(e.handle)); break;
		case Kind::Buffer: wgpuBufferAddRef(static_cast<WGPUBuffer>(e.handle)); break;
		case Kind::Sampler: wgpuSamplerAddRef(static_cast<WGPUSampler>(e.handle)); break;
		}
	}

	static void release(const DescriptorSources::Entry& e) noexcept
	{
		if (e.handle == nullptr) { return; }
		switch (e.kind)
		{
		case Kind::Texture: wgpuTextureRelease(static_cast<WGPUTexture>(e.handle)); break;
		case Kind::TextureView: wgpuTextureViewRelease(static_cast<WGPUTextureView>(e.handle)); break;
		case Kind::Buffer: wgpuBufferRelease(static_cast<WGPUBuffer>(e.handle)); break;
		case Kind::Sampler: wgpuSamplerRelease(static_cast<WGPUSampler>(e.handle)); break;
		}
	}

	void releaseAll() noexcept
	{
		for (std::size_t i = 0; i < m_count; ++i) { release(m_entries[i]); }
		m_count = 0;
	}

	void takeFrom(WrittenRefs& other) noexcept
	{
		m_entries = other.m_entries;
		m_count = other.m_count;
		m_valid = other.m_valid;
		other.m_count = 0;
		other.m_valid = false;
	}

	std::array<DescriptorSources::Entry, DescriptorSources::kCapacity> m_entries{};
	std::size_t m_count = 0;
	bool m_valid = false;
};

/// 登録簿を調べる時点。FrameHead は beginFrame の最初、Lazy は資源を遅れて作成した直後、PreSubmit は記録を始める前。
enum class RefreshPhase : std::uint8_t
{
	FrameHead,
	Lazy,
	PreSubmit,
};

/// bind group の組の登録簿。
class DescriptorTableRegistry
{
public:
	using Collect = std::function<void(DescriptorSources&)>;
	using Write = std::function<void(const DescriptorSources&)>;

	struct Stats
	{
		std::uint64_t sweeps = 0;
		std::uint64_t rewrites = 0;       ///< 書き替えた組の延べ数 (最初の書き込みを含む)
		std::uint64_t lateRewrites = 0;   ///< 記録の前に初めて違いが見つかった組の延べ数
	};

	/// 組を登録する。collect は現在の資源を返し、write はその資源から bind group を作る。登録順に調べる。
	/// write には collect の結果が渡るため、null を含むときは作成せず空のままにできる。
	std::size_t add(const char* name, Collect collect, Write write)
	{
		m_tables.push_back(Table{name, std::move(collect), std::move(write), {}, 0});
		return m_tables.size() - 1;
	}

	/// 資源が変わった組を作り直す。使用中の古い資源は参照が保たれるため、GPU は待たない。
	/// @return 書き替えた組の数
	std::size_t refresh(RefreshPhase phase)
	{
		++m_stats.sweeps;
		std::size_t rewritten = 0;
		DescriptorSources now;
		for (Table& table : m_tables)
		{
			now.clear();
			table.collect(now);
			const char* changed = table.written.firstChange(now);
			if (changed == nullptr) { continue; }
			if (phase == RefreshPhase::PreSubmit && table.written.written()) { noteLate(table.name, changed); }
			table.write(now);
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

	/// 名前が一致する組を書き替えた回数。見つからなければ 0。
	[[nodiscard]] std::uint64_t rewritesOf(std::string_view name) const noexcept
	{
		for (const Table& table : m_tables)
		{
			if (name == table.name) { return table.rewrites; }
		}
		return 0;
	}

	/// 記録前に見つかった最後の変更について、組と資源の名前を返す。見つからなければ空。
	[[nodiscard]] const std::string& lastLateTable() const noexcept { return m_lastLateTable; }
	[[nodiscard]] const std::string& lastLateSource() const noexcept { return m_lastLateSource; }

private:
	struct Table
	{
		const char* name;
		Collect collect;
		Write write;
		WrittenRefs written;
		std::uint64_t rewrites;
	};

	void noteLate(const char* table, const char* source)
	{
		++m_stats.lateRewrites;
		m_lastLateTable = table;
		m_lastLateSource = source;
#ifndef NDEBUG
		debug::warnOnce(std::string("dawn.tables.late.") + table + "." + source,
		                std::string("bind group の組 '") + table + "' が写す '" + source +
		                    "' を途中で作り替えたので、作り替えた直後に refresh を呼んでください");
#endif
	}

	std::vector<Table> m_tables;
	Stats m_stats;
	std::string m_lastLateTable;
	std::string m_lastLateSource;
};

} // namespace mitiru::render::dawn_detail
