#pragma once

/// @file DxLib.h
/// @brief DxLibスタブ（テスト用）
///
/// DxLibの関数と定数をスタブとして定義する。
/// 描画コールを記録し、入力状態をモックできる。

#include <cstring>
#include <vector>

// ── DxLib定数 ────────────────────────────────────────────

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define DX_BLENDMODE_ALPHA   1
#define DX_BLENDMODE_NOBLEND 0
#define DX_BLENDMODE_ADD     2
#define DX_BLENDMODE_MULA    4

#define MOUSE_INPUT_LEFT   0x0001
#define MOUSE_INPUT_RIGHT  0x0002
#define MOUSE_INPUT_MIDDLE 0x0004

#define KEY_INPUT_SPACE 57
#define KEY_INPUT_A     30
#define KEY_INPUT_W     17
#define KEY_INPUT_S     31
#define KEY_INPUT_D     32

#define DX_FONTTYPE_ANTIALIASING_EDGE_4X4 23

#define DX_PLAYTYPE_LOOP 3
#define DX_PLAYTYPE_BACK 2

// ── スタブ記録・モック状態 ──────────────────────────────

namespace dxlib_stub
{

/// @brief 描画コールの種別
enum class DrawType
{
	BoxAA,
	Box,
	CircleAA,
	LineAA,
	TriangleAA,
	BlendMode,
	StringToHandle,
	SoundPlay,
	SoundStop,
	SoundVolume,
	SoundLoad,
	SoundDelete,
	SoundCheck,
	GraphLoad,
	GraphDelete,
	RectExtendGraph,
	ExtendGraph,
	RectRotaGraph3,
	DrawBright,
	Line3D,
	Sphere3D,
	LightAmbColor
};

/// @brief 描画コール記録
struct DrawCall
{
	DrawType type;
	float params[8]{};       ///< 関数パラメータ（可変長）
	unsigned int color{0};   ///< カラー値
	int fillFlag{0};         ///< 塗りつぶしフラグ
};

/// @brief 描画コール記録リストを取得する
inline std::vector<DrawCall>& drawCalls()
{
	static std::vector<DrawCall> calls;
	return calls;
}

/// @brief モック用キー状態バッファを取得する
inline char* keyState()
{
	static char state[256] = {};
	return state;
}

/// @brief モック用マウスX座標
inline int& mouseX()
{
	static int x = 0;
	return x;
}

/// @brief モック用マウスY座標
inline int& mouseY()
{
	static int y = 0;
	return y;
}

/// @brief モック用マウスボタン状態
inline int& mouseInput()
{
	static int input = 0;
	return input;
}

/// @brief モック用前フレームキー状態バッファを取得する
inline char* prevKeyState()
{
	static char state[256] = {};
	return state;
}

/// @brief フォント作成記録
struct FontRecord
{
	int handle;       ///< フォントハンドル
	int size;         ///< フォントサイズ
	int fontType;     ///< フォントタイプ
	bool deleted;     ///< 削除済みか
};

/// @brief フォント記録リストを取得する
inline std::vector<FontRecord>& fontRecords()
{
	static std::vector<FontRecord> records;
	return records;
}

/// @brief 次のフォントハンドルカウンタ
inline int& nextFontHandle()
{
	static int handle = 1;
	return handle;
}

/// @brief ProcessMessage()の戻り値を制御する
inline int& processMessageResult()
{
	static int r = 0;
	return r;
}

/// @brief ProcessMessage()の呼び出し回数を取得する
inline int& processMessageCallCount()
{
	static int c = 0;
	return c;
}

/// @brief サウンド記録
struct SoundRecord
{
	int handle;        ///< サウンドハンドル
	int volume;        ///< ボリューム（0-255）
	bool playing;      ///< 再生中か
	bool deleted;      ///< 削除済みか
};

/// @brief サウンド記録リストを取得する
inline std::vector<SoundRecord>& soundRecords()
{
	static std::vector<SoundRecord> records;
	return records;
}

/// @brief 次のサウンドハンドルカウンタ
inline int& nextSoundHandle()
{
	static int handle = 1;
	return handle;
}

/// @brief グラフィック（テクスチャ）記録
struct GraphRecord
{
	int handle;       ///< グラフィックハンドル
	int width;        ///< 幅
	int height;       ///< 高さ
	bool deleted;     ///< 削除済みか
};

/// @brief グラフィック記録リストを取得する
inline std::vector<GraphRecord>& graphRecords()
{
	static std::vector<GraphRecord> records;
	return records;
}

/// @brief 次のグラフィックハンドルカウンタ
inline int& nextGraphHandle()
{
	static int handle = 1;
	return handle;
}

/// @brief XInput ボタンインデックス定数
constexpr int BUTTON_COUNT = 16;

/// @brief XInput パッド状態
struct XInputPadState
{
	bool connected = false;
	unsigned char Buttons[16]{};  ///< ボタン配列
	short ThumbLX = 0;
	short ThumbLY = 0;
	short ThumbRX = 0;
	short ThumbRY = 0;
	unsigned char LeftTrigger = 0;
	unsigned char RightTrigger = 0;
};

/// @brief XInput パッド状態配列を取得する
inline XInputPadState* xinputStates()
{
	static XInputPadState states[4]{};
	return states;
}

/// @brief バイブレーション記録
struct VibrationRecord
{
	int padId;
	int power;
	int time;
};

/// @brief バイブレーション記録リストを取得する
inline std::vector<VibrationRecord>& vibrationRecords()
{
	static std::vector<VibrationRecord> records;
	return records;
}

/// @brief 全スタブ状態をリセットする
inline void reset()
{
	drawCalls().clear();
	std::memset(keyState(), 0, 256);
	std::memset(prevKeyState(), 0, 256);
	mouseX() = 0;
	mouseY() = 0;
	mouseInput() = 0;
	fontRecords().clear();
	nextFontHandle() = 1;
	soundRecords().clear();
	nextSoundHandle() = 1;
	graphRecords().clear();
	nextGraphHandle() = 1;
	vibrationRecords().clear();
	for (int i = 0; i < 4; ++i)
	{
		xinputStates()[i] = XInputPadState{};
	}
	processMessageResult() = 0;
	processMessageCallCount() = 0;
}

} // namespace dxlib_stub

// ── DxLib関数スタブ ──────────────────────────────────────

inline void DrawBoxAA(float x1, float y1, float x2, float y2,
	unsigned int color, int fillFlag)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::BoxAA,
		{x1, y1, x2, y2},
		color, fillFlag
	});
}

inline void DrawCircleAA(float x, float y, float r, int segments,
	unsigned int color, int fillFlag)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::CircleAA,
		{x, y, r, static_cast<float>(segments)},
		color, fillFlag
	});
}

inline void DrawLineAA(float x1, float y1, float x2, float y2,
	unsigned int color, float thickness)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::LineAA,
		{x1, y1, x2, y2, thickness},
		color, 0
	});
}

inline void DrawTriangleAA(float x1, float y1, float x2, float y2,
	float x3, float y3, unsigned int color, int fillFlag)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::TriangleAA,
		{x1, y1, x2, y2, x3, y3},
		color, fillFlag
	});
}

inline void SetDrawBlendMode(int mode, int param)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::BlendMode,
		{static_cast<float>(mode), static_cast<float>(param)},
		0, 0
	});
}

inline void DrawBox(int x1, int y1, int x2, int y2,
	unsigned int color, int fillFlag)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::Box,
		{static_cast<float>(x1), static_cast<float>(y1),
		 static_cast<float>(x2), static_cast<float>(y2)},
		color, fillFlag
	});
}

inline int GetHitKeyStateAll(char* keyStateBuf)
{
	std::memcpy(keyStateBuf, dxlib_stub::keyState(), 256);
	return 0;
}

inline int GetMousePoint(int* x, int* y)
{
	*x = dxlib_stub::mouseX();
	*y = dxlib_stub::mouseY();
	return 0;
}

inline int GetMouseInput()
{
	return dxlib_stub::mouseInput();
}

// ── フォント関数スタブ ──────────────────────────────────

inline int CreateFontToHandle(const char* /*fontName*/, int size,
	int /*thickness*/, int fontType = -1)
{
	const int handle = dxlib_stub::nextFontHandle()++;
	dxlib_stub::fontRecords().push_back({handle, size, fontType, false});
	return handle;
}

inline int DeleteFontToHandle(int handle)
{
	for (auto& rec : dxlib_stub::fontRecords())
	{
		if (rec.handle == handle)
		{
			rec.deleted = true;
			return 0;
		}
	}
	return -1;
}

inline int DrawStringToHandle(int x, int y, const char* /*text*/,
	unsigned int color, int handle)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::StringToHandle,
		{static_cast<float>(x), static_cast<float>(y),
		 static_cast<float>(handle)},
		color, 0
	});
	return 0;
}

inline int GetDrawStringWidthToHandle(const char* /*text*/, int strLen, int /*handle*/)
{
	return strLen * 12;  // 近似値: 1文字あたり12ピクセル
}

// ── ゲームループ関数スタブ ──────────────────────────────

inline int ProcessMessage()
{
	++dxlib_stub::processMessageCallCount();
	return dxlib_stub::processMessageResult();
}

inline void ClearDrawScreen() { /* no-op */ }

inline void ScreenFlip() { /* no-op */ }

// ── サウンド関数スタブ ──────────────────────────────────

inline int LoadSoundMem(const char* /*path*/)
{
	const int handle = dxlib_stub::nextSoundHandle()++;
	dxlib_stub::soundRecords().push_back({handle, 255, false, false});
	return handle;
}

inline int DeleteSoundMem(int handle)
{
	for (auto& rec : dxlib_stub::soundRecords())
	{
		if (rec.handle == handle)
		{
			rec.deleted = true;
			rec.playing = false;
			return 0;
		}
	}
	return -1;
}

inline int PlaySoundMem(int handle, int /*playType*/)
{
	for (auto& rec : dxlib_stub::soundRecords())
	{
		if (rec.handle == handle)
		{
			rec.playing = true;
			return 0;
		}
	}
	return -1;
}

inline int StopSoundMem(int handle)
{
	for (auto& rec : dxlib_stub::soundRecords())
	{
		if (rec.handle == handle)
		{
			rec.playing = false;
			return 0;
		}
	}
	return -1;
}

inline int ChangeVolumeSoundMem(int volume, int handle)
{
	for (auto& rec : dxlib_stub::soundRecords())
	{
		if (rec.handle == handle)
		{
			rec.volume = volume;
			return 0;
		}
	}
	return -1;
}

inline int CheckSoundMem(int handle)
{
	for (const auto& rec : dxlib_stub::soundRecords())
	{
		if (rec.handle == handle)
		{
			return rec.playing ? 1 : 0;
		}
	}
	return 0;
}

// ── グラフィック（テクスチャ）関数スタブ ─────────────────

inline int LoadGraph(const char* /*path*/)
{
	const int handle = dxlib_stub::nextGraphHandle()++;
	dxlib_stub::graphRecords().push_back({handle, 64, 64, false});
	return handle;
}

inline int DeleteGraph(int handle)
{
	for (auto& rec : dxlib_stub::graphRecords())
	{
		if (rec.handle == handle)
		{
			rec.deleted = true;
			return 0;
		}
	}
	return -1;
}

inline int GetGraphSize(int handle, int* width, int* height)
{
	for (const auto& rec : dxlib_stub::graphRecords())
	{
		if (rec.handle == handle && !rec.deleted)
		{
			*width = rec.width;
			*height = rec.height;
			return 0;
		}
	}
	*width = 0;
	*height = 0;
	return -1;
}

inline int DrawRectExtendGraph(int destX1, int destY1, int destX2, int destY2,
	int srcX, int srcY, int srcW, int srcH, int /*graphHandle*/, int /*transflag*/)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::RectExtendGraph,
		{static_cast<float>(destX1), static_cast<float>(destY1),
		 static_cast<float>(destX2), static_cast<float>(destY2),
		 static_cast<float>(srcX), static_cast<float>(srcY),
		 static_cast<float>(srcW), static_cast<float>(srcH)},
		0, 0
	});
	return 0;
}

inline int DrawExtendGraph(int x1, int y1, int x2, int y2,
	int /*graphHandle*/, int /*transflag*/)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::ExtendGraph,
		{static_cast<float>(x1), static_cast<float>(y1),
		 static_cast<float>(x2), static_cast<float>(y2)},
		0, 0
	});
	return 0;
}

inline int DrawRectRotaGraph3(int x, int y, int srcX, int srcY,
	int srcW, int srcH, int origX, int origY,
	double /*scaleX*/, double /*scaleY*/, double /*angle*/,
	int /*graphHandle*/, int /*transflag*/)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::RectRotaGraph3,
		{static_cast<float>(x), static_cast<float>(y),
		 static_cast<float>(srcX), static_cast<float>(srcY),
		 static_cast<float>(srcW), static_cast<float>(srcH),
		 static_cast<float>(origX), static_cast<float>(origY)},
		0, 0
	});
	return 0;
}

inline int SetDrawBright(int r, int g, int b)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::DrawBright,
		{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)},
		0, 0
	});
	return 0;
}

// ── XInput関数スタブ ──────────────────────────────────────

/// @brief XInputボタンインデックス定数
#define XINPUT_BUTTON_DPAD_UP        0
#define XINPUT_BUTTON_DPAD_DOWN      1
#define XINPUT_BUTTON_DPAD_LEFT      2
#define XINPUT_BUTTON_DPAD_RIGHT     3
#define XINPUT_BUTTON_START          4
#define XINPUT_BUTTON_BACK           5
#define XINPUT_BUTTON_LEFT_THUMB     6
#define XINPUT_BUTTON_RIGHT_THUMB    7
#define XINPUT_BUTTON_LEFT_SHOULDER  8
#define XINPUT_BUTTON_RIGHT_SHOULDER 9
#define XINPUT_BUTTON_A             12
#define XINPUT_BUTTON_B             13
#define XINPUT_BUTTON_X             14
#define XINPUT_BUTTON_Y             15

/// @brief DxLib XInput状態構造体
struct XINPUT_STATE
{
	unsigned char Buttons[16]{};  ///< ボタン配列
	short ThumbLX = 0;            ///< 左スティックX
	short ThumbLY = 0;            ///< 左スティックY
	short ThumbRX = 0;            ///< 右スティックX
	short ThumbRY = 0;            ///< 右スティックY
	unsigned char LeftTrigger = 0;  ///< 左トリガー
	unsigned char RightTrigger = 0; ///< 右トリガー
};

inline int GetJoypadXInputState(int padId, XINPUT_STATE* state)
{
	const int idx = padId - 1;
	if (idx < 0 || idx >= 4) return -1;
	const auto& pad = dxlib_stub::xinputStates()[idx];
	if (!pad.connected) return -1;

	for (int i = 0; i < 16; ++i)
	{
		state->Buttons[i] = pad.Buttons[i];
	}
	state->ThumbLX = pad.ThumbLX;
	state->ThumbLY = pad.ThumbLY;
	state->ThumbRX = pad.ThumbRX;
	state->ThumbRY = pad.ThumbRY;
	state->LeftTrigger = pad.LeftTrigger;
	state->RightTrigger = pad.RightTrigger;
	return 0;
}

inline int StartJoypadVibration(int padId, int power, int time)
{
	dxlib_stub::vibrationRecords().push_back({padId, power, time});
	return 0;
}

// ── 3D描画関数スタブ ──────────────────────────────────────

/// @brief DxLib 3Dベクトル
struct VECTOR
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

/// @brief DxLib 浮動小数点カラー
struct COLOR_F
{
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	float a = 1.0f;
};

inline int SetLightAmbColor(COLOR_F /*color*/)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::LightAmbColor,
		{}, 0, 0
	});
	return 0;
}

inline int DrawLine3D(VECTOR from, VECTOR to, unsigned int color)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::Line3D,
		{from.x, from.y, from.z, to.x, to.y, to.z},
		color, 0
	});
	return 0;
}

inline int DrawSphere3D(VECTOR center, float radius, int /*divNum*/,
	unsigned int diffuseColor, unsigned int /*specularColor*/, int /*fillFlag*/)
{
	dxlib_stub::drawCalls().push_back({
		dxlib_stub::DrawType::Sphere3D,
		{center.x, center.y, center.z, radius},
		diffuseColor, 0
	});
	return 0;
}
