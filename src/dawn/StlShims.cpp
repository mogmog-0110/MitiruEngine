// MSVC 14.51 の STL でビルドした Dawn を MSVC 14.44 でリンクするための、欠けているベクトル化アルゴリズム入口の単純な実装。
// Dawn の Tint / SPIRV-Tools が std::rotate / min_element / max_element / unique の置き換えとして呼ぶ。
// MSVC 14.51 以降では MitiruDawn.cmake がこのファイルを外す。

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace
{

template <class T>
const void* minElement(const void* first, const void* last) noexcept
{
	const T* best = static_cast<const T*>(first);
	for (const T* it = best + 1; it < static_cast<const T*>(last); ++it)
	{
		if (*it < *best) { best = it; }
	}
	return best;
}

template <class T>
const void* maxElement(const void* first, const void* last) noexcept
{
	const T* best = static_cast<const T*>(first);
	for (const T* it = best + 1; it < static_cast<const T*>(last); ++it)
	{
		if (*best < *it) { best = it; }
	}
	return best;
}

template <class T>
void* uniqueElements(void* first, void* last) noexcept
{
	T* const begin = static_cast<T*>(first);
	T* const end = static_cast<T*>(last);
	if (begin == end) { return end; }
	T* out = begin;
	for (T* it = begin + 1; it != end; ++it)
	{
		if (*it != *out) { *++out = *it; }
	}
	return out + 1;
}

} // namespace

extern "C"
{

__declspec(noalias) void __stdcall __std_rotate(void* first, void* mid, void* last) noexcept
{
	std::rotate(static_cast<char*>(first), static_cast<char*>(mid), static_cast<char*>(last));
}

const void* __stdcall __std_min_element_4u(const void* first, const void* last) noexcept
{
	return first == last ? last : minElement<std::uint32_t>(first, last);
}

const void* __stdcall __std_min_element_8u(const void* first, const void* last) noexcept
{
	return first == last ? last : minElement<std::uint64_t>(first, last);
}

const void* __stdcall __std_max_element_4u(const void* first, const void* last) noexcept
{
	return first == last ? last : maxElement<std::uint32_t>(first, last);
}

void* __stdcall __std_unique_4(void* first, void* last) noexcept
{
	return uniqueElements<std::uint32_t>(first, last);
}

void* __stdcall __std_unique_8(void* first, void* last) noexcept
{
	return uniqueElements<std::uint64_t>(first, last);
}

} // extern "C"
