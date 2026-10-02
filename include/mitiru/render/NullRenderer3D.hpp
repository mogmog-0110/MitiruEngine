#pragma once

/// @file NullRenderer3D.hpp
/// @brief headless / 未対応 backend 向けの 3D レンダラー実装
/// @details Vulkan / OpenGL / NullDevice では 3D 描画パスが未実装なので、
///          呼び出し側 (Engine/Screen) に nullptr を返すと分岐が増える。
///          「何も描かないが drawCallCount だけは数える」実体を返し、
///          IRenderer3D の呼び出し側コードを backend 分岐なしで保てるようにする。

#include <mitiru/render/IRenderer3D.hpp>

namespace mitiru::render
{

/// @brief 3D 描画を行わない no-op レンダラー。drawMesh 呼び出し回数だけ数える。
class NullRenderer3D final : public IRenderer3D
{
public:
	[[nodiscard]] bool isInitialized() const noexcept override { return true; }

	void beginFrame(const sgc::Colorf& /*clearColor*/) override { m_frameActive = false; }
	void endFrame() override {}

	void setCamera(const Camera3D& /*camera*/) override {}
	void setLight(const Light& /*light*/) override {}

	void drawMesh(const Mesh& /*mesh*/,
	              const sgc::Mat4f& /*worldTransform*/,
	              const Material& /*material*/) override
	{
		m_frameActive = true;
		++m_drawCallCount;
	}

	void resetFrameActive() noexcept override { m_frameActive = false; }
	[[nodiscard]] bool isFrameActive() const noexcept override { return m_frameActive; }
	[[nodiscard]] int drawCallCount() const noexcept override { return m_drawCallCount; }

private:
	bool m_frameActive = false;
	int m_drawCallCount = 0;
};

} // namespace mitiru::render
