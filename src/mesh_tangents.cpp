/// @file mesh_tangents.cpp
/// @brief MikkTSpace の呼び出し (MeshTangents.hpp の実体)

#include <mitiru/render/MeshTangents.hpp>

#include <mikktspace.h>

namespace mitiru::render
{

namespace
{

struct MeshView
{
	const std::vector<Vertex3D>& vertices;
	const std::vector<std::uint32_t>& indices;
	std::vector<sgc::Vec4f>& tangents;

	[[nodiscard]] const Vertex3D& corner(int face, int vert) const
	{
		return vertices[indices[static_cast<std::size_t>(face) * 3 + static_cast<std::size_t>(vert)]];
	}
};

const MeshView& viewOf(const SMikkTSpaceContext* ctx) { return *static_cast<const MeshView*>(ctx->m_pUserData); }

int getNumFaces(const SMikkTSpaceContext* ctx) { return static_cast<int>(viewOf(ctx).indices.size() / 3); }

int getNumVerticesOfFace(const SMikkTSpaceContext*, int) { return 3; }

void getPosition(const SMikkTSpaceContext* ctx, float out[], int face, int vert)
{
	const auto& p = viewOf(ctx).corner(face, vert).position;
	out[0] = p.x; out[1] = p.y; out[2] = p.z;
}

void getNormal(const SMikkTSpaceContext* ctx, float out[], int face, int vert)
{
	const auto& n = viewOf(ctx).corner(face, vert).normal;
	out[0] = n.x; out[1] = n.y; out[2] = n.z;
}

void getTexCoord(const SMikkTSpaceContext* ctx, float out[], int face, int vert)
{
	const auto& t = viewOf(ctx).corner(face, vert).texCoord;
	out[0] = t.x; out[1] = t.y;
}

void setTSpaceBasic(const SMikkTSpaceContext* ctx, const float tangent[], float sign, int face, int vert)
{
	const auto& view = viewOf(ctx);
	const std::size_t index = view.indices[static_cast<std::size_t>(face) * 3 + static_cast<std::size_t>(vert)];
	view.tangents[index] = sgc::Vec4f{tangent[0], tangent[1], tangent[2], sign};
}

} // namespace

std::vector<sgc::Vec4f> computeMikkTangents(const std::vector<Vertex3D>& vertices, const std::vector<std::uint32_t>& indices)
{
	if (vertices.empty() || indices.size() < 3 || indices.size() % 3 != 0) { return {}; }
	for (const std::uint32_t i : indices)
	{
		if (i >= vertices.size()) { return {}; }
	}

	std::vector<sgc::Vec4f> tangents(vertices.size(), sgc::Vec4f{0.0f, 0.0f, 0.0f, 0.0f});
	MeshView view{vertices, indices, tangents};
	SMikkTSpaceInterface iface{};
	iface.m_getNumFaces = &getNumFaces;
	iface.m_getNumVerticesOfFace = &getNumVerticesOfFace;
	iface.m_getPosition = &getPosition;
	iface.m_getNormal = &getNormal;
	iface.m_getTexCoord = &getTexCoord;
	iface.m_setTSpaceBasic = &setTSpaceBasic;
	SMikkTSpaceContext ctx{&iface, &view};
	if (!genTangSpaceDefault(&ctx)) { return {}; }
	return tangents;
}

} // namespace mitiru::render
