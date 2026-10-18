#pragma once

/// @file RegistrySideState.hpp
/// @brief EnTT の registry を、部品の型の並びごと MITIRU_SIDE_STATE (ADR 0054) で host へ預ける。
/// @details 物を static に置き、MITIRU_SIDE_STATE("world", 1, 物) と書く。実体の表は空き番号の列と世代ごと、部品は
/// storage の並びのまま書くので、戻した後に作る実体の番号も view を回す順も、戻す前と同じになる。並びに無い部品は
/// 保存されず、戻すと消える。
/// 保存のたびに registry を調べ、並びに無い部品を持つ実体があれば 1 度だけ型の名前を知らせ、その間は窓口の
/// kSideStateExact を落とす (host は遊んでいる状態を写して戻す検査を止める)。

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

#include <entt/entt.hpp>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/module/SideState.hpp>

namespace mitiru::ecs
{

namespace registry_state_detail
{

/// @brief cap を超えても書いたつもりで数え続け、要る byte 数を返せるようにする
struct Writer
{
	std::uint8_t* dst;
	std::uint64_t cap;
	std::uint64_t size = 0;

	void bytes(const void* p, std::uint64_t n)
	{
		if (size + n <= cap) { std::memcpy(dst + size, p, n); }
		size += n;
	}
	template <class T>
	void value(const T& v) { bytes(&v, sizeof(T)); }
};

struct Reader
{
	const std::uint8_t* src;
	std::uint64_t       size;
	std::uint64_t       at = 0;

	bool bytes(void* p, std::uint64_t n)
	{
		if (at + n > size) { return false; }
		std::memcpy(p, src + at, n);
		at += n;
		return true;
	}
	template <class T>
	bool value(T& v) { return bytes(&v, sizeof(T)); }
};

} // namespace registry_state_detail

/// @brief 部品の型の並び Components... を保存する registry
template <class... Components>
class RegistrySideState
{
	static_assert((std::is_trivially_copyable_v<Components> && ...), "部品は POD にする");

public:
	/// 並びに無い部品を見つけるまでは、保存と復元で registry が全部戻る
	static constexpr bool kSideStateExact = true;

	entt::registry registry;

	/// @brief dst へ書いて要る byte 数を返す。cap が足りなければ要る量だけ返す
	std::uint64_t saveState(void* dst, std::uint64_t cap) const
	{
		noteUnlistedComponents();
		registry_state_detail::Writer w{static_cast<std::uint8_t*>(dst), cap};
		(w.value(static_cast<std::uint32_t>(sizeof(Components))), ...);
		saveEntities(w);
		(saveComponent<Components>(w), ...);
		return w.size;
	}

	/// @brief saveState の bytes から作り直す。形が違えば何もせず false
	bool restoreState(const void* src, std::uint64_t size)
	{
		registry_state_detail::Reader r{static_cast<const std::uint8_t*>(src), size};
		if (!(checkSize<Components>(r) && ...)) { return false; }
		entt::registry next;
		if (!loadEntities(r, next) || !(loadComponent<Components>(r, next) && ...)) { return false; }
		registry = std::move(next);
		return true;
	}

	/// @brief 並びに無い部品を持つ実体がある型の名前 (", " 区切り)。無ければ空
	[[nodiscard]] std::string unlistedComponentNames() const
	{
		std::string names;
		for (const auto [id, storage] : registry.storage())
		{
			(void)id;
			if (storage.empty() || listed(storage.type())) { continue; }
			names += names.empty() ? "" : ", ";
			names += std::string(storage.type().name());
		}
		return names;
	}

private:
	[[nodiscard]] static bool listed(const entt::type_info& type) noexcept
	{
		return type == entt::type_id<entt::entity>() || ((type == entt::type_id<Components>()) || ...);
	}

	/// @brief 毎フレームの保存で呼ぶので、名前の文字列は知らせる時だけ作る
	void noteUnlistedComponents() const
	{
		bool unlisted = false;
		for (const auto [id, storage] : registry.storage())
		{
			(void)id;
			unlisted = unlisted || (!storage.empty() && !listed(storage.type()));
		}
		module::detail::setSideStateExact(this, !unlisted);
		if (!unlisted || m_warned) { return; }
		m_warned = true;
		const std::string names = unlistedComponentNames();
		console::verbosef("MITIRU_SIDE_STATE に預けた registry に、型の並びに無い部品 (%s) を持つ実体があります。"
		                 "この部品は保存されず、巻き戻し・ロード・ホットリロードで消えます。残したい部品は型の並びに入れてください。",
		                 names.c_str());
	}

	/// @brief 実体の表 (生きている実体の数、空き番号の列を含む全部の実体の値)
	void saveEntities(registry_state_detail::Writer& w) const
	{
		const auto* entities = registry.template storage<entt::entity>();
		const auto  size = entities != nullptr ? static_cast<std::uint32_t>(entities->size()) : 0u;
		w.value(size);
		w.value(entities != nullptr ? static_cast<std::uint32_t>(entities->free_list()) : 0u);
		for (std::uint32_t i = 0; i < size; ++i) { w.value(entt::to_integral(entities->data()[i])); }
	}

	static bool loadEntities(registry_state_detail::Reader& r, entt::registry& reg)
	{
		std::uint32_t size = 0;
		std::uint32_t alive = 0;
		if (!r.value(size) || !r.value(alive) || alive > size) { return false; }
		auto& entities = reg.storage<entt::entity>();
		for (std::uint32_t i = 0; i < size; ++i)
		{
			std::uint32_t value = 0;
			if (!r.value(value)) { return false; }
			entities.emplace(static_cast<entt::entity>(value));
		}
		entities.free_list(alive);
		return true;
	}

	/// @brief storage の並び (data の順) のまま書く。戻す時も同じ順に足すので、view を回す順が変わらない
	template <class T>
	void saveComponent(registry_state_detail::Writer& w) const
	{
		const auto* storage = registry.template storage<T>();
		const auto  size = storage != nullptr ? static_cast<std::uint32_t>(storage->size()) : 0u;
		w.value(size);
		for (std::uint32_t i = 0; i < size; ++i)
		{
			const entt::entity e = storage->data()[i];
			w.value(entt::to_integral(e));
			w.value(storage->get(e));
		}
	}

	template <class T>
	static bool checkSize(registry_state_detail::Reader& r)
	{
		std::uint32_t size = 0;
		return r.value(size) && size == sizeof(T);
	}

	template <class T>
	static bool loadComponent(registry_state_detail::Reader& r, entt::registry& reg)
	{
		std::uint32_t count = 0;
		if (!r.value(count)) { return false; }
		for (std::uint32_t i = 0; i < count; ++i)
		{
			std::uint32_t id = 0;
			T             value{};
			if (!r.value(id) || !r.value(value)) { return false; }
			const auto e = static_cast<entt::entity>(id);
			if (!reg.valid(e)) { return false; }
			reg.emplace<T>(e, value);
		}
		return true;
	}

	mutable bool m_warned = false;
};

} // namespace mitiru::ecs
