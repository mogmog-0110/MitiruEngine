#pragma once

/// @file NullRenderer3D.hpp
/// @brief headless / 未対応 backend 向けの 3D レンダラー実装
/// @details Vulkan / OpenGL / NullDevice では 3D 描画パスが未実装なので、
///          呼び出し側 (Engine/Screen) に nullptr を返すと分岐が増える。
///          「何も描かないが drawCallCount だけは数える」実体を返し、
///          IRenderer3D の呼び出し側コードを backend 分岐なしで保てるようにする。

#include <mitiru/debug/ConsoleOut.hpp>
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

/// @brief `[render] needs` に no_3d を宣言したゲームの 3D の窓口。3D の層を何も作らず、3D の依頼は最初の 1 回だけ知らせて捨てる。
/// @details NullRenderer3D と違い、フレームを 3D で使った印 (isFrameActive) を立てない。立てると host が 3D のフレームと見て、
///          2D の絵を重ねる経路へ回してしまう。
class NoWorld3DRenderer final : public IRenderer3D
{
public:
	[[nodiscard]] bool isInitialized() const noexcept override { return true; }

	void beginFrame(const sgc::Colorf& /*clearColor*/) override {}
	void endFrame() override {}

	void setCamera(const Camera3D& /*camera*/) override {}
	void setLight(const Light& /*light*/) override {}

	void drawMesh(const Mesh& /*mesh*/, const sgc::Mat4f& /*worldTransform*/, const Material& /*material*/) override
	{
		told();
	}
	void drawModel(const char* /*path*/, const sgc::Vec3f& /*position*/, float /*rotYDeg*/, float /*scale*/) override { told(); }
	void drawModelRot(const char* /*path*/, const sgc::Vec3f& /*position*/, const sgc::Vec3f& /*rotDeg*/, float /*scale*/) override
	{
		told();
	}

	void resetFrameActive() noexcept override {}
	[[nodiscard]] bool isFrameActive() const noexcept override { return false; }
	[[nodiscard]] int drawCallCount() const noexcept override { return 0; }

	/// 3D の依頼を捨てた回数 (試験用)
	[[nodiscard]] int droppedCount() const noexcept { return m_dropped; }

private:
	void told()
	{
		++m_dropped;
		if (m_told) { return; }
		m_told = true;
		mitiru::console::notice("no_3d を宣言したので、3D の描画は出ません");
	}

	bool m_told = false;
	int m_dropped = 0;
};

} // namespace mitiru::render
