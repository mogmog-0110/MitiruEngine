#pragma once

/// @file Quad2D.hpp
/// @brief 軸に平行な四角形 1 枚を 1 レコード (48 バイト) で持つ形
/// @details 画像・矩形・文字は 1 枚 4 頂点 + 6 添字 (152 バイト) で積むと、積む側も GPU へ写す側も 1 枚の費用が大きい。
///          4 隅の選び方は決まっているので、レコードに左上と右下の値を 1 組だけ持たせ、
///          頂点シェーダーが SV_VertexID で 4 隅を作る (DirectXTK の SpriteBatch、bgfx の instancing sample と同じ考え方)。
///          隅の値は CPU が頂点を作るときの float と同じ値をそのまま持つので、GPU が作る頂点は今までと同じになる。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

#include <sgc/types/Color.hpp>

namespace mitiru::render
{

struct Quad2D
{
	float x0, y0;   ///< 左上の位置
	float x1, y1;   ///< 右下の位置
	float u0, v0;   ///< 左上の UV
	float u1, v1;   ///< 右下の UV
	sgc::Colorf color;
};

static_assert(sizeof(Quad2D) == 48, "Quad2D は入力レイアウトの 48 バイトと同じ");

/// Quad2D の置き場。std::vector は 1 枚積むたびに容量の確認と構築の呼び出しを挟み、画像を 10 万枚積む場面でその分が
/// 1 枚 4 ナノ秒前後かかるので、確認 1 回で書き込み先の番地を返す形にした (OpenSiv3D の Vertex2DBatch も同じく、配列の番地へ直接書く)
class QuadStore
{
public:
	QuadStore() = default;
	QuadStore(const QuadStore& other) { copyFrom(other); }
	QuadStore(QuadStore&& other) noexcept
		: m_data(std::exchange(other.m_data, nullptr)), m_size(std::exchange(other.m_size, 0u)), m_capacity(std::exchange(other.m_capacity, 0u))
	{
	}
	QuadStore& operator=(const QuadStore& other)
	{
		if (this != &other) { m_size = 0; copyFrom(other); }
		return *this;
	}
	QuadStore& operator=(QuadStore&& other) noexcept
	{
		if (this != &other)
		{
			release();
			m_data = std::exchange(other.m_data, nullptr);
			m_size = std::exchange(other.m_size, 0u);
			m_capacity = std::exchange(other.m_capacity, 0u);
		}
		return *this;
	}
	~QuadStore() { release(); }

	/// 1 枚ぶんの書き込み先を返す (サイズを 1 増やす)。足りなければ倍に広げる
	[[nodiscard]] Quad2D* push()
	{
		if (m_size == m_capacity) { grow(m_capacity == 0 ? 256u : m_capacity * 2u); }
		return m_data + m_size++;
	}

	void reserve(std::size_t count)
	{
		if (count > m_capacity) { grow(static_cast<std::uint32_t>(count)); }
	}
	void clear() noexcept { m_size = 0; }
	[[nodiscard]] bool empty() const noexcept { return m_size == 0; }
	[[nodiscard]] std::size_t size() const noexcept { return m_size; }
	[[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }
	[[nodiscard]] const Quad2D* data() const noexcept { return m_data; }
	[[nodiscard]] const Quad2D& operator[](std::size_t i) const noexcept { return m_data[i]; }

private:
	void grow(std::uint32_t capacity)
	{
		auto* next = static_cast<Quad2D*>(::operator new(sizeof(Quad2D) * capacity));
		if (m_size != 0) { std::memcpy(next, m_data, sizeof(Quad2D) * m_size); }
		::operator delete(m_data);
		m_data = next;
		m_capacity = capacity;
	}
	void copyFrom(const QuadStore& other)
	{
		reserve(other.m_size);
		if (other.m_size != 0) { std::memcpy(m_data, other.m_data, sizeof(Quad2D) * other.m_size); }
		m_size = other.m_size;
	}
	void release() noexcept
	{
		::operator delete(m_data);
		m_data = nullptr;
		m_size = 0;
		m_capacity = 0;
	}

	Quad2D* m_data = nullptr;
	std::uint32_t m_size = 0;
	std::uint32_t m_capacity = 0;
};

/// 1 回の提出の中で、四角形のレコードと従来の三角形 (円・線・グラデーション) を描いた順に並べるための区間の始まり。
/// 区間の終わりは次の区間の始まり (最後は積んだ数)
struct BatchSegment
{
	enum class Kind : std::uint32_t { Quads, Triangles };
	Kind kind = Kind::Quads;
	std::uint32_t quadFirst = 0;     ///< この区間が始まる前に積んだレコードの数
	std::uint32_t indexFirst = 0;    ///< この区間が始まる前に積んだ添字の数
	std::uint32_t vertexFirst = 0;   ///< この区間が始まる前に積んだ頂点の数
};

} // namespace mitiru::render
