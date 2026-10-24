// MSVC 14.51 の STL でビルドした Dawn を、それより古い STL (14.44 と 14.50) でリンクするための、ベクトル化アルゴリズム入口の単純な実装。
// Dawn の Tint / SPIRV-Tools が std::rotate / min_element / max_element / unique の置き換えとして呼ぶ。
// 14.50 の msvcprt は __std_rotate と __std_unique_4/8 だけ持ち、min/max は持たない。入口ごとに有る無しが違うので、
// 本物と衝突しないよう /alternatename で「どこにも定義が無いときだけ」この実装へ解く。

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

__declspec(noalias) void __stdcall mitiru_shim__std_rotate(void* first, void* mid, void* last) noexcept
{
	std::rotate(static_cast<char*>(first), static_cast<char*>(mid), static_cast<char*>(last));
}

const void* __stdcall mitiru_shim__std_min_element_4u(const void* first, const void* last) noexcept
{
	return first == last ? last : minElement<std::uint32_t>(first, last);
}

const void* __stdcall mitiru_shim__std_min_element_8u(const void* first, const void* last) noexcept
{
	return first == last ? last : minElement<std::uint64_t>(first, last);
}

const void* __stdcall mitiru_shim__std_max_element_4u(const void* first, const void* last) noexcept
{
	return first == last ? last : maxElement<std::uint32_t>(first, last);
}

void* __stdcall mitiru_shim__std_unique_4(void* first, void* last) noexcept
{
	return uniqueElements<std::uint32_t>(first, last);
}

void* __stdcall mitiru_shim__std_unique_8(void* first, void* last) noexcept
{
	return uniqueElements<std::uint64_t>(first, last);
}

// /alternatename だけでは、この object を誰も参照せず lib から取り込まれない。MitiruDawn.cmake が /INCLUDE でこの記号を要求する
int mitiru_stl_shim_anchor = 0;

} // extern "C"

#pragma comment(linker, "/alternatename:__std_rotate=mitiru_shim__std_rotate")
#pragma comment(linker, "/alternatename:__std_min_element_4u=mitiru_shim__std_min_element_4u")
#pragma comment(linker, "/alternatename:__std_min_element_8u=mitiru_shim__std_min_element_8u")
#pragma comment(linker, "/alternatename:__std_max_element_4u=mitiru_shim__std_max_element_4u")
#pragma comment(linker, "/alternatename:__std_unique_4=mitiru_shim__std_unique_4")
#pragma comment(linker, "/alternatename:__std_unique_8=mitiru_shim__std_unique_8")
