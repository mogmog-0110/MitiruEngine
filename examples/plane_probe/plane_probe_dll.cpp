// plane_probe ― #56 の再現プローブ。同じフレームに cube と "plane" を並べ、固定カメラで
// 1 枚撮る。--capture-dir で PNG を吐けば、どの形が画素になったかを数えで判定できる ―
// 「plane が描画されない」の再現と修正の効き目を、目ではなく数字で見るための章である。
// あそびかた: 何も操作しない。左が参照の立方体、中央が疑惑の plane、右が薄い立方体。

#include <mitiru.hpp>

using namespace mitiru;

struct PlaneProbe
{
	void init() {}
	void update(Input, float) {}

	void draw(Screen& s) const
	{
		s.clear(hex(0x101018));
		s.camera3D({0.0f, 6.0f, -10.0f}, {0.0f, 0.0f, 0.0f}, 50.0f);
		s.light3D({-0.6f, -0.95f, -0.45f}, hex(0xFFFFFF));

		// 左: 参照の cube (見えるはず)。中央: 疑惑の plane (大きく、色は緑)。
		// 右: plane の代役に使われてきた薄い cube。中央だけ欠ければ #56 の再現である。
		s.drawMesh("plane", { 0.0f, 0.5f, 0.0f}, {5.0f, 1.0f, 5.0f}, {}, hex(0x44C034));
		s.drawMesh("plane", {-2.5f, 2.0f, 0.0f}, {2.0f, 1.0f, 2.0f},
		           {-90.0f, 0.0f, 0.0f}, hex(0xC0C034));
		s.drawMesh("cube",  {-4.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {}, hex(0xC04434));
		s.drawMesh("cube",  { 4.0f, 0.5f, 0.0f}, {2.0f, 0.05f, 2.0f}, {}, hex(0x3488C0));
	}
};

// 実行:  mitiru_host.exe plane_probe/plane_probe.dll --max-frames 60 --capture-dir . ^
//        --capture-every 50
MITIRU_GAME(PlaneProbe);
