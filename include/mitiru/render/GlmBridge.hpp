#pragma once
/// @file GlmBridge.hpp
/// @brief sgc <-> glm 型変換ブリッジ

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Mat4.hpp>

#include <cstring>

namespace mitiru::render {

/// @brief sgc -> glm 変換
inline glm::vec3 toGlm(const sgc::Vec3f& v) { return {v.x, v.y, v.z}; }
inline glm::vec2 toGlm(const sgc::Vec2f& v) { return {v.x, v.y}; }

/// @brief sgc::Mat4f -> glm::mat4 変換
/// sgc::Mat4f.m[row][col] -> glm::mat4 (column-major)
inline glm::mat4 toGlm(const sgc::Mat4f& m) {
	glm::mat4 result;
	// sgc stores m[row][col], glm stores column-major
	// glm[col][row] = sgc.m[row][col]
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			result[c][r] = m.m[r][c];
	return result;
}

/// @brief glm::mat4 -> float[4][4] (HLSL row-major constant buffer用)
/// HLSL expects row-major when using mul(vector, matrix)
inline void toHLSL(float dst[4][4], const glm::mat4& m) {
	// glm is column-major: m[col][row]
	// HLSL row-major: dst[row][col]
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			dst[r][c] = m[c][r];
}

/// @brief glm::mat4 をfloat[4][4]にそのままコピー（column-major）
inline void toColumnMajor(float dst[4][4], const glm::mat4& m) {
	std::memcpy(dst, glm::value_ptr(m), sizeof(float) * 16);
}

/// @brief glmでLookAt行列を作成（左手座標系）
inline glm::mat4 lookAt(const sgc::Vec3f& eye, const sgc::Vec3f& target, const sgc::Vec3f& up) {
	return glm::lookAtLH(toGlm(eye), toGlm(target), toGlm(up));
}

/// @brief glmで透視投影行列を作成（左手座標系、DX深度範囲[0,1]）
inline glm::mat4 perspective(float fovRadians, float aspect, float nearZ, float farZ) {
	return glm::perspectiveLH_ZO(fovRadians, aspect, nearZ, farZ);
}

/// @brief glmでモデル行列を作成（Translation * RotationYXZ * Scale）
inline glm::mat4 modelMatrix(const sgc::Vec3f& pos, const sgc::Vec3f& rot, const sgc::Vec3f& scale) {
	glm::mat4 m = glm::mat4(1.0f);
	m = glm::translate(m, toGlm(pos));
	m = glm::rotate(m, rot.y, glm::vec3(0, 1, 0));
	m = glm::rotate(m, rot.x, glm::vec3(1, 0, 0));
	m = glm::rotate(m, rot.z, glm::vec3(0, 0, 1));
	m = glm::scale(m, toGlm(scale));
	return m;
}

/// @brief glmで正射影行列を作成
inline glm::mat4 orthographic(float left, float right, float bottom, float top, float nearZ, float farZ) {
	return glm::orthoLH(left, right, bottom, top, nearZ, farZ);
}

} // namespace mitiru::render
