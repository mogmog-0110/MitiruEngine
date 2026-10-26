#pragma once
// mitiru::Screen の ABI v62 (ADR 0099) の入口。直接 include しない。core/Screen.hpp 経由。
//
// どれも入口だけ。3D の物は IRenderer3D の v62 の virtual を呼び (既定の本体が 1 度だけ知らせる)、2D の物は状態を Screen に置いて
// 印を立てるだけにする。印は host がフレームの終わりに takePendingV62() で読んで知らせる (DLL が inline で持つ warnOnce の表は DLL ごとに別なので、
// 知らせるのは host の側に寄せる)。

#include <mitiru/module/detail/HostBoundary62.hpp>
#include <mitiru/render/IRenderer3D.hpp>
#include <mitiru/video/VideoTypes.hpp>

namespace mitiru
{

inline int Screen::registerMaterial3D(const char* name)
{
	if (m_renderer3D == nullptr || name == nullptr || name[0] == '\0') { return -1; }
	return m_renderer3D->registerMaterial3D(name);
}

inline void Screen::material3D(int id, const float* params, int count, float timeSec)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->setUserMaterial(id, params, params != nullptr && count > 0 ? count : 0, timeSec);
}

inline void Screen::drawModelPose(const char* path, const sgc::Mat4f& world, const animation::AnimPoseParams* pose,
                                  const animation::AnimIkRequest* ik, std::uint32_t ikCount, const render::DrawTint& tint,
                                  const float* morphWeights, int morphCount)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->drawModelMorph(path, world, pose, ik, static_cast<int>(ikCount), tint, morphWeights,
	                             morphWeights != nullptr && morphCount > 0 ? morphCount : 0);
}

inline void Screen::environmentHdr3D(const char* name, float intensity, Deg yaw)
{
	if (m_renderer3D != nullptr) { m_renderer3D->setEnvironmentHdr(name, intensity, yaw.degrees()); }
}

namespace screen_detail
{

// 光は描く順に効く。変える前に、積みかけの描画を今の光のままサブミットしておく。
// 3D と同じフレームの 2D は 3D の後にまとめてサブミットするので、途中で光を替えられない (当てずに知らせる)。
// CPU 描画 (観測用の画面バッファ) にも当てない。いずれも、光を掛ける先が DX12 / Dawn の GPU の 2D であることが前提
template <class CloseBatches, class Deferring>
inline bool prepareLighting2D(render::RenderPipeline2D* pipeline, CloseBatches&& closeBatches, Deferring&& deferring)
{
	if (pipeline == nullptr) { return false; }   // 描画先が無い実行 (Null) は、描くものも無い
	if (!pipeline->supportsLighting2D())
	{
		render::features::note(render::features::Feature::Light2d);
		debug::warnOnce("light2d.unsupported",
			"この描画先、または light_2d を宣言していない実行では、2D ライトは当たりません (対応は DX12 と Dawn で、needs に light_2d が要ります)");
		return false;
	}
	if (deferring())
	{
		debug::warnOnce("light2d.with3d", "3D と同じフレームの 2D には、2D ライトは当たりません");
		return false;
	}
	closeBatches();
	return true;
}

}  // namespace screen_detail

inline void Screen::light2D(const render::Light2DPod* lights, int count)
{
	if (lights == nullptr || count <= 0) { return; }
	render::features::use(render::features::Feature::Light2d);   // 宣言との突き合わせ (census) に、このゲームが光を使うと伝える
	const int first = m_lightCount2D;
	for (int i = 0; i < count && m_lightCount2D < render::kMaxLights2D; ++i) { m_lights2D[m_lightCount2D++] = lights[i]; }
	if (m_lightCount2D == first) { return; }
	if (!screen_detail::prepareLighting2D(m_pipeline, [this] { closeBatchesForStateChange(); }, [this] { return deferring3DOverlay(); }))
	{
		return;
	}

	// 灯の座標は、描画と同じく今の変換 (カメラなど) を通した論理座標で光らせる。半径と高さは変換の拡大率で、円錐の向きは線形部分で回す
	const render::Transform2D t = currentTransform();
	const float scale = std::sqrt(std::abs(t.a * t.d - t.b * t.c));
	for (int i = first; i < m_lightCount2D; ++i)
	{
		render::Light2DPod pod = m_lights2D[i];
		const sgc::Vec2f p = t.apply(pod.x, pod.y);
		pod.x = p.x;
		pod.y = p.y;
		pod.radius *= scale;
		pod.height *= scale;
		if (pod.coneDeg > 0.0f)
		{
			const float rad = pod.directionDeg * render::light2d::kDegToRad;
			const float dx = t.a * std::cos(rad) + t.c * std::sin(rad);
			const float dy = t.b * std::cos(rad) + t.d * std::sin(rad);
			pod.directionDeg = std::atan2(dy, dx) / render::light2d::kDegToRad;
		}
		m_pipeline->addLight2D(pod);
	}
}

inline void Screen::ambient2D(const sgc::Colorf& color, float intensity)
{
	m_ambient2D[0] = color.r; m_ambient2D[1] = color.g; m_ambient2D[2] = color.b; m_ambient2D[3] = intensity;
	m_ambient2DSet = 1;
	render::features::use(render::features::Feature::Light2d);
	if (!screen_detail::prepareLighting2D(m_pipeline, [this] { closeBatchesForStateChange(); }, [this] { return deferring3DOverlay(); }))
	{
		return;
	}
	m_pipeline->setAmbient2D(color.r * intensity, color.g * intensity, color.b * intensity);
}

inline void Screen::drawVideo(std::string_view name, const sgc::Rectf& dst, float timeSec, const sgc::Colorf& tint)
{
	if (m_spriteResolveFn == nullptr) { m_pendingV62 |= module::detail::kPendingV62Video; return; }
	// GPU (DX12) とソフトウェアの描画は drawSprite が色を掛ける。それ以外 (Dawn、DX11) は画素の列を渡すだけの口なので、host が画素に掛ける
	const bool tinted = (m_pipeline != nullptr && m_pipeline->supportsTexturedBatch()) || hasSoftwareFramebuffer();
	char id[video::kVideoIdMax];
	if (!video::formatVideoId(id, sizeof(id), name, timeSec, tinted ? video::kTintWhite : video::packTint(tint)))
	{
		m_pendingV62 |= module::detail::kPendingV62Video;
		return;
	}
	// 動画の host が知らせる (動画を使えない実行・ファイルが無い・名前が壊れている)。ここでは黙って描かない
	const render::Texture* frame = m_spriteResolveFn(m_spriteResolveCtx, id);
	if (frame == nullptr || !frame->valid()) { return; }
	if (tinted)
	{
		drawSprite(*frame, dst, tint);
		return;
	}
	drawPixelGrid(dst, reinterpret_cast<const std::uint32_t*>(frame->pixels().data()), frame->width(), frame->height());
}

inline void Screen::drawTextVertical(const sgc::Rectf& rect, std::string_view text, const sgc::Colorf& color, float fontSize,
                                     module::TextWriting writing, bool /*tateChuYoko*/, float padX, float padY)
{
	if (writing != module::TextWriting::Horizontal) { m_pendingV62 |= module::detail::kPendingV62VerticalText; }
	drawTextInRect(rect, text, color, fontSize, TextAlignH::Left, TextAlignV::Top, padX, padY);
}

inline std::int32_t Screen::invokeRendererExtension(std::uint32_t id, void* data, std::uint32_t size)
{
	return m_renderer3D != nullptr ? m_renderer3D->invokeExtension(id, data, size) : -1;
}

} // namespace mitiru
