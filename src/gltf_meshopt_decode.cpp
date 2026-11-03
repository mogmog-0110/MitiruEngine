/// @file gltf_meshopt_decode.cpp
/// @brief EXT_meshopt_compression の復号 (GltfMeshoptDecode.hpp の実体)

#include <mitiru/render/GltfMeshoptDecode.hpp>

#include <meshoptimizer.h>

namespace mitiru::render
{

namespace
{

int decodeOne(const cgltf_meshopt_compression& mc, const unsigned char* source, void* result)
{
	switch (mc.mode)
	{
	case cgltf_meshopt_compression_mode_attributes:
		return meshopt_decodeVertexBuffer(result, mc.count, mc.stride, source, mc.size);
	case cgltf_meshopt_compression_mode_triangles:
		return meshopt_decodeIndexBuffer(result, mc.count, mc.stride, source, mc.size);
	case cgltf_meshopt_compression_mode_indices:
		return meshopt_decodeIndexSequence(result, mc.count, mc.stride, source, mc.size);
	default:
		return -1;
	}
}

void applyFilter(const cgltf_meshopt_compression& mc, void* result)
{
	switch (mc.filter)
	{
	case cgltf_meshopt_compression_filter_octahedral:
		meshopt_decodeFilterOct(result, mc.count, mc.stride);
		break;
	case cgltf_meshopt_compression_filter_quaternion:
		meshopt_decodeFilterQuat(result, mc.count, mc.stride);
		break;
	case cgltf_meshopt_compression_filter_exponential:
		meshopt_decodeFilterExp(result, mc.count, mc.stride);
		break;
	case cgltf_meshopt_compression_filter_color:
		meshopt_decodeFilterColor(result, mc.count, mc.stride);
		break;
	default:
		break;
	}
}

} // namespace

cgltf_result decodeMeshoptBufferViews(cgltf_data* data)
{
	if (data == nullptr) { return cgltf_result_invalid_options; }

	for (cgltf_size i = 0; i < data->buffer_views_count; ++i)
	{
		cgltf_buffer_view& view = data->buffer_views[i];
		if (!view.has_meshopt_compression || view.data != nullptr) { continue; }

		const cgltf_meshopt_compression& mc = view.meshopt_compression;
		if (mc.buffer == nullptr || mc.buffer->data == nullptr || mc.stride == 0) { return cgltf_result_invalid_gltf; }
		// 和と積が折り返さない形で範囲を見る (壊れた glTF が巨大な値を書いてくる)
		if (mc.size > mc.buffer->size || mc.offset > mc.buffer->size - mc.size) { return cgltf_result_data_too_short; }
		if (mc.count > (static_cast<cgltf_size>(-1) / 4) / mc.stride) { return cgltf_result_invalid_gltf; }
		// accessor は view.size まで読むので、戻した領域がそれより小さい glTF は範囲外を読ませる (仕様は size == count * stride)
		if (view.size > mc.count * mc.stride) { return cgltf_result_invalid_gltf; }

		// cgltf_free が buffer_view.data を memory.free_func で解放するので、確保も同じ組で行う (cgltf_parse が既定を埋める)
		void* result = data->memory.alloc_func(data->memory.user_data, mc.count * mc.stride);
		if (result == nullptr) { return cgltf_result_out_of_memory; }

		const auto* source = static_cast<const unsigned char*>(mc.buffer->data) + mc.offset;
		if (decodeOne(mc, source, result) != 0)
		{
			data->memory.free_func(data->memory.user_data, result);
			return cgltf_result_invalid_gltf;
		}
		applyFilter(mc, result);
		view.data = result;
	}
	return cgltf_result_success;
}

} // namespace mitiru::render
