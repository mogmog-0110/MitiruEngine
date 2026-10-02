#pragma once

/// @file SideStateHost.hpp
/// @brief host が game の窓口 (SideStateChannel、ADR 0054) を呼んで、1 フレーム分の image を作る・戻す。
/// @details 窓口の表は DLL を読むたびに取り直す (ホットリロードで関数も ctx も変わる)。image の形は
/// observe/SideStateImage.hpp。capture は毎フレーム呼ぶので、出力先の vector と窓口ごとの見込み
/// 容量を使い回し、大きくなったときだけ確保する。

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/observe/SideStateImage.hpp>

namespace mitiru::module
{

class SideStateHost
{
public:
	/// @brief 窓口の表を取り込む。名前の重複・関数の欠けは申告の誤りなので、その窓口を外して理由を返す。
	/// @return 外した窓口があれば、その名前の並び (無ければ空)
	std::string bind(const SideStateChannel* channels, std::int32_t count)
	{
		m_count = 0;
		m_capHint.fill(0);
		std::string problems;
		for (std::int32_t i = 0; channels != nullptr && i < count && i < kMaxSideStateChannels; ++i)
		{
			const SideStateChannel& c = channels[i];
			const std::string_view name(c.name, ::strnlen(c.name, sizeof(c.name)));
			if (name.empty() || c.save == nullptr || c.restore == nullptr || find(name) >= 0)
			{
				problems += problems.empty() ? "" : ", ";
				problems += name.empty() ? std::string("(名前なし)") : std::string(name);
				continue;
			}
			m_channels[m_count++] = c;
		}
		return problems;
	}

	void unbind() noexcept { m_count = 0; }

	[[nodiscard]] bool          empty() const noexcept { return m_count == 0; }
	[[nodiscard]] std::int32_t  count() const noexcept { return m_count; }
	[[nodiscard]] const SideStateChannel& channel(std::int32_t i) const noexcept { return m_channels[i]; }

	/// @brief 場面の中身を丸ごと持つ窓口があるか (部分状態の game でも全状態になる、kSideStateCoversScene)。
	[[nodiscard]] bool coversScene() const noexcept
	{
		for (std::int32_t i = 0; i < m_count; ++i)
		{
			if ((m_channels[i].flags & kSideStateCoversScene) != 0) { return true; }
		}
		return false;
	}

	/// @brief 全窓口を save して image を out に作る。withBytes = false なら hash だけを持つ形にする。
	/// @return 全窓口を保存できたら true。false のとき error に窓口名と理由。
	bool capture(const void* memory, bool withBytes, std::vector<std::uint8_t>& out, std::string* error)
	{
		out.resize(sizeof(observe::SideImageHeader));
		const observe::SideImageHeader h{observe::kSideImageMagic, static_cast<std::uint32_t>(m_count)};
		std::memcpy(out.data(), &h, sizeof(h));
		for (std::int32_t i = 0; i < m_count; ++i)
		{
			if (!captureOne(i, memory, withBytes, out, error)) { return false; }
		}
		return true;
	}

	/// @brief image の bytes で全窓口を戻す。名前・形の番号・数が今の表と食い違えば何も戻さずに断る。
	bool restore(void* memory, const std::uint8_t* image, std::size_t n, std::string* error)
	{
		observe::SideImageView view;
		if (!observe::parseSideImage(image, n, view))
		{
			return fail(error, "記録の形が壊れている");
		}
		if (std::string why = mismatch(view); !why.empty()) { return fail(error, std::move(why)); }
		for (std::int32_t i = 0; i < m_count; ++i)
		{
			const SideStateChannel& c = m_channels[i];
			const observe::SideImageItem* item = view.find(std::string_view(c.name, ::strnlen(c.name, sizeof(c.name))));
			if (c.restore(c.ctx, memory, item->bytes, item->entry.size) == 0)
			{
				return fail(error, std::string(c.name) + " の restore が失敗を返した");
			}
		}
		return true;
	}

	/// @brief image を今の表へ戻せるか (名前・形の番号・bytes の有無)。戻せなければ理由。
	[[nodiscard]] std::string mismatch(const observe::SideImageView& view) const
	{
		if (static_cast<std::int32_t>(view.count) != m_count)
		{
			return "窓口の数が違う (記録 " + std::to_string(view.count) + " / 今 " + std::to_string(m_count) + ")";
		}
		for (std::int32_t i = 0; i < m_count; ++i)
		{
			const SideStateChannel& c = m_channels[i];
			const std::string name(c.name, ::strnlen(c.name, sizeof(c.name)));
			const observe::SideImageItem* item = view.find(name);
			if (item == nullptr) { return name + " の記録が無い"; }
			if (item->entry.version != c.version)
			{
				return name + " の形の番号が違う (記録 " + std::to_string(item->entry.version)
				     + " / 今 " + std::to_string(c.version) + ")";
			}
			if (item->bytes == nullptr) { return name + " の記録は hash だけで戻せない"; }
		}
		return {};
	}

private:
	[[nodiscard]] int find(std::string_view name) const noexcept
	{
		for (std::int32_t i = 0; i < m_count; ++i)
		{
			if (name == std::string_view(m_channels[i].name, ::strnlen(m_channels[i].name, sizeof(m_channels[i].name)))) { return i; }
		}
		return -1;
	}

	static bool fail(std::string* error, std::string why)
	{
		if (error != nullptr) { *error = std::move(why); }
		return false;
	}

	bool captureOne(std::int32_t i, const void* memory, bool withBytes, std::vector<std::uint8_t>& out,
	                std::string* error)
	{
		const SideStateChannel& c = m_channels[i];
		const std::size_t entryAt = out.size();
		const std::size_t bytesAt = entryAt + sizeof(observe::SideImageEntry);
		// 窓口ごとの見込み容量で先に場所を取り、足りなければ広げて 1 回だけ呼び直す。
		std::uint64_t cap = m_capHint[static_cast<std::size_t>(i)];
		out.resize(bytesAt + static_cast<std::size_t>(cap));
		std::uint64_t need = c.save(c.ctx, memory, out.data() + bytesAt, cap);
		if (need > cap)
		{
			cap = need + need / 4;
			out.resize(bytesAt + static_cast<std::size_t>(cap));
			need = c.save(c.ctx, memory, out.data() + bytesAt, cap);
			if (need > cap) { return fail(error, std::string(c.name) + " の save が要る大きさを毎回変える"); }
		}
		if (need == 0) { return fail(error, std::string(c.name) + " の save が何も書かなかった"); }
		m_capHint[static_cast<std::size_t>(i)] = cap;

		observe::SideImageEntry e{};
		std::memcpy(e.name, c.name, sizeof(e.name));
		e.version = c.version;
		e.size    = need;
		e.hash    = (c.hash != nullptr) ? c.hash(c.ctx, memory)
		                                : observe::fnv1a64(out.data() + bytesAt, static_cast<std::size_t>(need));
		e.flags   = withBytes ? observe::kSideEntryHasBytes : 0u;
		out.resize(withBytes ? bytesAt + static_cast<std::size_t>(need) : bytesAt);
		std::memcpy(out.data() + entryAt, &e, sizeof(e));
		return true;
	}

	SideStateChannel m_channels[kMaxSideStateChannels]{};
	std::int32_t     m_count = 0;
	std::array<std::uint64_t, kMaxSideStateChannels> m_capHint{};
};

}  // namespace mitiru::module
