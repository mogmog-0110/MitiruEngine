// mitiru_padcheck: つないだパッドをエンジンと同じ経路 (SdlGamepadInput) で読み、1 行ずつ表示するコンソールツール。
// ゲームからはまだ触れない機能 (機種、電池、ジャイロ、タッチパッド、ライトバー、振動、アダプティブトリガー) を実機で確かめる。
// 操作: A=振動 0.5 秒、B=右トリガーに抵抗、X=右トリガーを振動、Y=トリガー効果を消す。Start と Back の同時押しか --seconds で終わる。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <mitiru/input/SdlGamepadInput.hpp>

namespace
{

namespace gp = mitiru::module::gamepad;
using mitiru::input::PadKind;
using mitiru::input::SdlGamepadInput;
using mitiru::input::TriggerEffect;
using mitiru::input::TriggerSide;

const char* kindName(PadKind k)
{
	static const char* const kNames[] = {"Unknown", "Standard", "Xbox360", "XboxOne", "PS3", "PS4", "PS5",
		"SwitchPro", "JoyConL", "JoyConR", "JoyConPair", "GameCube"};
	return kNames[static_cast<int>(k)];
}

/// 枠ごとに色を変え、どのパッドがどの枠に入ったかをライトバーで見分ける
void lightSlot(SdlGamepadInput& pads, int slot)
{
	static const std::uint8_t kColors[4][3] = {{0, 0, 255}, {255, 0, 0}, {0, 255, 0}, {255, 0, 255}};
	pads.setLightbar(slot, kColors[slot][0], kColors[slot][1], kColors[slot][2]);
	pads.setMotionEnabled(slot, true);
}

void act(SdlGamepadInput& pads, int slot)
{
	const std::uint32_t p = pads.buttonsJustPressed(slot);
	const auto trigger = [&](const char* what, const TriggerEffect& e) {
		std::printf("  slot %d trigger %s -> %d\n", slot, what, pads.setTriggerEffect(slot, TriggerSide::Right, e));
	};
	if (p & gp::A) { std::printf("  slot %d rumble -> %d\n", slot, pads.rumble(slot, 1.0f, 0.5f, 500)); }
	if (p & gp::B) { trigger("resistance", TriggerEffect::resistance(0, 200)); }
	if (p & gp::X) { trigger("vibration", TriggerEffect::vibration(64, 63, 20)); }
	if (p & gp::Y) { trigger("off", TriggerEffect::off()); }
}

void print(const SdlGamepadInput& pads, int slot)
{
	const auto info = pads.info(slot);
	const auto m = pads.motion(slot);
	const auto f = pads.touchFinger(slot, 0, 0);
	std::printf("slot %d %-10s %04x:%04x bat=%3d%% btn=%04x L=(%+.2f,%+.2f) R=(%+.2f,%+.2f) T=(%.2f,%.2f)"
	            " gyro=(%+.2f,%+.2f,%+.2f) touch=%d(%.2f,%.2f)%s\n",
		slot, kindName(info.kind), info.vendorId, info.productId, info.batteryPercent, pads.buttonsDown(slot),
		pads.axis(slot, gp::LeftStickX), pads.axis(slot, gp::LeftStickY),
		pads.axis(slot, gp::RightStickX), pads.axis(slot, gp::RightStickY),
		pads.axis(slot, gp::LeftTrigger), pads.axis(slot, gp::RightTrigger),
		m.gyro[0], m.gyro[1], m.gyro[2], f.down ? 1 : 0, f.x, f.y, pads.touchpadPressed(slot) ? " [pad-click]" : "");
}

}  // namespace

int main(int argc, char** argv)
{
	double seconds = 0.0;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) { seconds = std::atof(argv[++i]); }
		else { std::fprintf(stderr, "usage: mitiru_padcheck [--seconds n]\n"); return 2; }
	}
	SdlGamepadInput pads;
	if (!pads.init()) { std::fprintf(stderr, "mitiru_padcheck: SDL3 のパッドの初期化に失敗しました。上に出た警告を見てください。\n"); return 1; }

	const auto start = std::chrono::steady_clock::now();
	bool lit[SdlGamepadInput::kMaxPads] = {};
	for (int frame = 0;; ++frame)
	{
		pads.update();
		bool quit = false;
		for (int s = 0; s < SdlGamepadInput::kMaxPads; ++s)
		{
			if (!pads.connected(s)) { lit[s] = false; continue; }
			if (!lit[s]) { lightSlot(pads, s); lit[s] = true; }
			act(pads, s);
			if (frame % 10 == 0) { print(pads, s); }
			quit |= (pads.buttonsDown(s) & (gp::Start | gp::Back)) == (gp::Start | gp::Back);
		}
		pads.endTick();
		const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		if (quit || (seconds > 0.0 && t >= seconds)) { break; }
		std::this_thread::sleep_for(std::chrono::milliseconds(33));
	}
	return 0;
}
