// mitiru_host に glb のパスを渡すと選ばれる確認用の DLL。並べ方とクリップは環境変数 MITIRU_VIEW で渡される

#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <mitiru.hpp>
#include <mitiru/animation/AnimRuntime.hpp>
#include <mitiru/animation/ModelView.hpp>

using namespace mitiru;
namespace anim = mitiru::animation;

namespace
{

constexpr float kFovDeg = 38.0f;

anim::ModelViewConfig readConfig()
{
	const char* text = std::getenv(anim::kModelViewEnv);
	return (text != nullptr ? anim::parseModelViewConfig(text) : std::nullopt).value_or(anim::ModelViewConfig{});
}

// 起動時に 1 度だけ読む。エンジンの規約に従い、draw() の中では遅延して作らない
const anim::ModelViewConfig kConfig = readConfig();

std::string labelOf(const anim::ModelViewItem& m)
{
	return std::filesystem::u8path(m.path).stem().string();
}

} // namespace

struct ModelView
{
	float t = 0.0f;
	std::int32_t toon = 0;

	void init()
	{
		*this = ModelView{};
		toon = kConfig.toon ? 1 : 0;
	}

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		if (in.pressed(Key::T)) { toon = toon ? 0 : 1; }
		t += dt;
	}

	static void setLook(Screen& s, bool toonOn)
	{
		s.light3D({0.35f, -0.7f, -0.6f}, hex(0xFFF4E0));
		s.skybox3D(hex(0x6FA8E4), hex(0xF2F6FA));
		render::SceneLook look;
		look.ambient[0] = 0.42f; look.ambient[1] = 0.44f; look.ambient[2] = 0.5f;
		look.shadingModel = toonOn ? 2 : 1;
		look.toonBands = 3;
		look.shadow = true;
		look.shadowDirection[0] = 0.35f; look.shadowDirection[1] = -0.7f; look.shadowDirection[2] = -0.6f;
		s.sceneLook3D(look);
	}

	void drawModels(Screen& s, const anim::ModelViewLayout& layout) const
	{
		for (std::size_t i = 0; i < kConfig.models.size(); ++i)
		{
			const auto& m = kConfig.models[i];
			anim::AnimPoseParams pose;
			if (m.clip >= 0)
			{
				const float local = m.durationSec > 0.0f ? std::fmod(t, m.durationSec) : 0.0f;
				anim::pushLayer(pose, anim::AnimLayer::clipAt(m.clip, local));
			}
			const sgc::Mat4f world = sgc::Mat4f::translation({layout.offsetX[i], 0.0f, layout.offsetZ[i]});
			s.drawModelPose(m.path.c_str(), world, m.clip >= 0 ? &pose : nullptr, nullptr, 0);
		}
	}

	void drawLabels(Screen& s) const
	{
		float y = 16.0f;
		for (const auto& m : kConfig.models)
		{
			const std::string text = labelOf(m) + (m.clip >= 0 ? "  " + m.clipName : "  (クリップなし)");
			s.drawTextInRect(Rect{16.0f, y, 900.0f, 28.0f}, text.c_str(), hex(0x203040), 20.0f, Screen::TextAlignH::Left,
			                 Screen::TextAlignV::Middle);
			y += 28.0f;
		}
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xDCE9F5));
		if (kConfig.models.empty())
		{
			s.drawTextInRect(Rect{16.0f, 16.0f, 1200.0f, 32.0f}, "MITIRU_VIEW が無い (mitiru_host <model.glb> で起動する)",
			                 hex(0xB02020), 22.0f, Screen::TextAlignH::Left, Screen::TextAlignV::Middle);
			return;
		}
		const float aspect = static_cast<float>(s.width()) / static_cast<float>(s.height() > 0 ? s.height() : 1);
		const auto layout = anim::layoutModelView(kConfig, kFovDeg, aspect);
		const sgc::Vec3f eye = layout.eye, target = layout.target;
		s.camera3D(eye, target, Deg{kFovDeg});
		setLook(s, toon != 0);
		s.drawMesh("cube", {0.0f, layout.groundTop - layout.groundThickness * 0.5f, 0.0f},
		           {layout.groundHalfX * 2.0f, layout.groundThickness, layout.groundHalfZ * 2.0f}, {0, 0, 0}, hex(0x9CC69B));
		drawModels(s, layout);
		drawLabels(s);
	}
};

MITIRU_REFLECT(ModelView, t, toon);
MITIRU_ASSERT_NO_PADDING(ModelView);
MITIRU_GAME(ModelView);
