// save_slots。セーブを 3 つのスロットに分けて持つ (一覧・章の名前・消す)
// 実行すると: 右の花畑に花を植え、左の一覧でスロットにセーブ・読み込み・消去ができる。一覧は host に頼んで次のフレームに届く。
// 関連 API: hud.saveSlot / hud.load / hud.deleteSlot / hud.listSlots / in.slot / in.slotCount (UI は assets/ui/main.rml)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

constexpr int kCols = 10, kRows = 6, kSlots = 3;
constexpr Color kFlowers[4] = {theme::kPink, theme::kAmber, theme::kBlue, theme::kRed};
constexpr Rect kBed{440.0f, 110.0f, 800.0f, 480.0f};   // 花畑 (左の一覧を避けて右に置く)

// UI が送ってくる付随データ (例: {"id":2}) から id を取り出す。届くのは自分の main.rml が送る形だけ
int payloadId(const char* json)
{
	const char* p = (json != nullptr) ? std::strstr(json, "\"id\":") : nullptr;
	return (p != nullptr) ? std::atoi(p + 5) : 0;
}

struct SaveSlots
{
	std::uint8_t bed[kRows][kCols]{};   // 0 = 何も無い、1..4 = 花の色
	std::int32_t cursorX = 4, cursorY = 2;
	std::int32_t flowers = 0;           // 植えた本数 (セーブの章の名前に使う)
	std::int32_t planted = 0;           // 次に植える色を決める通し番号
	std::uint8_t listed = 0;            // 一覧を一度頼んだか
	std::uint8_t pad[3]{};

	void update(Input in, Hud hud, float)
	{
		if (listed == 0) { hud.listSlots(); listed = 1; }   // 起動したら一覧を頼む。答えは次のフレームの in.slot(i)

		if (in.pressed(Key::Left) && cursorX > 0) { --cursorX; }
		if (in.pressed(Key::Right) && cursorX < kCols - 1) { ++cursorX; }
		if (in.pressed(Key::Up) && cursorY > 0) { --cursorY; }
		if (in.pressed(Key::Down) && cursorY < kRows - 1) { ++cursorY; }
		if (in.pressed(Key::Space)) { togglePlant(); }

		// 一覧のボタン。スロットの名前は slot1..slot3。セーブと消去のあとは一覧を頼み直す
		char name[16];
		if (in.action("slot.save"))
		{
			std::snprintf(name, sizeof name, "slot%d", payloadId(in.actionPayload("slot.save")));
			char chapter[32];
			std::snprintf(chapter, sizeof chapter, "花 %d 本", flowers);
			hud.saveSlot(name, chapter);   // 章の名前は一覧の chapter に出る
			hud.listSlots();
		}
		if (in.action("slot.load"))
		{
			std::snprintf(name, sizeof name, "slot%d", payloadId(in.actionPayload("slot.load")));
			hud.load(name);   // 次のフレームから、この struct がセーブした時の中身に戻る
		}
		if (in.action("slot.delete"))
		{
			std::snprintf(name, sizeof name, "slot%d", payloadId(in.actionPayload("slot.delete")));
			hud.deleteSlot(name);
			hud.listSlots();
		}
		pushSlots(in, hud);
	}

	void togglePlant()
	{
		std::uint8_t& cell = bed[cursorY][cursorX];
		if (cell != 0) { cell = 0; --flowers; return; }
		cell = static_cast<std::uint8_t>(1 + planted % 4);
		++planted;
		++flowers;
	}

	// host が届けた一覧 (新しい順) から、slot1..slot3 の行を作って UI へ送る
	static void pushSlots(Input in, Hud hud)
	{
		char json[1024];
		int n = std::snprintf(json, sizeof json, "[");
		for (int id = 1; id <= kSlots; ++id)
		{
			char name[16];
			std::snprintf(name, sizeof name, "slot%d", id);
			const module::SlotSummary* found = nullptr;
			int order = -1;
			for (int i = 0; i < in.slotCount(); ++i)
			{
				if (std::strcmp(in.slot(i)->slot, name) == 0) { found = in.slot(i); order = i; }
			}
			const int sec = (found != nullptr) ? static_cast<int>(found->playtimeSec) : 0;
			n += std::snprintf(json + n, sizeof json - static_cast<std::size_t>(n),
			                   "%s{\"id\":%d,\"used\":%s,\"newest\":%s,\"broken\":%s,\"chapter\":\"%s\",\"play\":\"%d:%02d\"}",
			                   id > 1 ? "," : "", id, found ? "true" : "false", order == 0 ? "true" : "false",
			                   (found && found->status == 4) ? "true" : "false", found ? found->chapter : "", sec / 60, sec % 60);
		}
		std::snprintf(json + n, sizeof json - static_cast<std::size_t>(n), "]");
		hud.set("view.slots", json);
	}

	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);
		s.drawRoundedRect(kBed, hex(0xDCEBD5), 18.0f);
		const float cw = kBed.width() / kCols, ch = kBed.height() / kRows;
		for (int y = 0; y < kRows; ++y)
		{
			for (int x = 0; x < kCols; ++x)
			{
				const float cx = kBed.x() + cw * (static_cast<float>(x) + 0.5f);
				const float cy = kBed.y() + ch * (static_cast<float>(y) + 0.5f);
				s.fillCircle(cx, cy, 5.0f, hex(0xB9D2AE));   // 植えられる場所
				if (bed[y][x] != 0)
				{
					s.fillCircle(cx, cy, 20.0f, kFlowers[bed[y][x] - 1]);
					s.fillCircle(cx, cy, 7.0f, hex(0xFFF4C2));
				}
				if (x == cursorX && y == cursorY)
				{
					s.drawRoundedRectFrame(Rect{cx - cw * 0.45f, cy - ch * 0.45f, cw * 0.9f, ch * 0.9f}, theme::kInk, 10.0f, 3.0f);
				}
			}
		}
		chapterTitle(s, "セーブスロット");
		chapterControls(s, "矢印: えらぶ　Space: 花を植える / 抜く　左の一覧: セーブ・読み込み・消す");
	}
};

// 実行:  mitiru_host.exe save_slots/save_slots.dll   (セーブは作業フォルダの save/slot1.mslot などに書かれる)
MITIRU_ASSERT_NO_PADDING(SaveSlots);
MITIRU_GAME(SaveSlots);
