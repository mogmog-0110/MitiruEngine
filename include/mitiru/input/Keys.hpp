#pragma once

/// @file Keys.hpp
/// @brief Game DLL から `InputSnapshot::keys*` を index する時の名前付き定数
/// @details
/// `input->keysDown[mitiru::keys::Left]` のように使う。16 進数の VK code を
/// 直接書かなくて済む。
///
/// Windows Virtual-Key code に揃えている (Win32 native input から engine が
/// 拾った値そのもの)。`include/mitiru/input/KeyCode.hpp` の enum class と
/// 1:1 対応するが、こちらは plain `constexpr int` なので array index に
/// 直接使える。
///
/// @code
///   #include <mitiru/input/Keys.hpp>
///   using namespace mitiru::keys;
///
///   if (input->keysJustPressed[Escape]) { intents->requestStop = 1; }
///   if (input->keysDown[Left])  { dx -= 1.0f; }
///   if (input->keysDown[Right]) { dx += 1.0f; }
/// @endcode

namespace mitiru::keys
{

// ── 制御 / 修飾子 ──────────────────────────────────────
constexpr int Backspace  = 0x08;
constexpr int Tab        = 0x09;
constexpr int Enter      = 0x0D;
constexpr int Shift      = 0x10;
constexpr int Ctrl       = 0x11;
constexpr int Alt        = 0x12;
constexpr int CapsLock   = 0x14;
constexpr int Escape     = 0x1B;
constexpr int Space      = 0x20;

// ── ナビゲーション ────────────────────────────────────
constexpr int PageUp     = 0x21;
constexpr int PageDown   = 0x22;
constexpr int End        = 0x23;
constexpr int Home       = 0x24;
constexpr int Left       = 0x25;
constexpr int Up         = 0x26;
constexpr int Right      = 0x27;
constexpr int Down       = 0x28;
constexpr int Insert     = 0x2D;
constexpr int Delete     = 0x2E;

// ── 数字 (top row) ─────────────────────────────────────
constexpr int Num0 = 0x30;  constexpr int Num1 = 0x31;
constexpr int Num2 = 0x32;  constexpr int Num3 = 0x33;
constexpr int Num4 = 0x34;  constexpr int Num5 = 0x35;
constexpr int Num6 = 0x36;  constexpr int Num7 = 0x37;
constexpr int Num8 = 0x38;  constexpr int Num9 = 0x39;

// ── アルファベット ───────────────────────────────────
constexpr int A = 0x41;  constexpr int B = 0x42;  constexpr int C = 0x43;
constexpr int D = 0x44;  constexpr int E = 0x45;  constexpr int F = 0x46;
constexpr int G = 0x47;  constexpr int H = 0x48;  constexpr int I = 0x49;
constexpr int J = 0x4A;  constexpr int K = 0x4B;  constexpr int L = 0x4C;
constexpr int M = 0x4D;  constexpr int N = 0x4E;  constexpr int O = 0x4F;
constexpr int P = 0x50;  constexpr int Q = 0x51;  constexpr int R = 0x52;
constexpr int S = 0x53;  constexpr int T = 0x54;  constexpr int U = 0x55;
constexpr int V = 0x56;  constexpr int W = 0x57;  constexpr int X = 0x58;
constexpr int Y = 0x59;  constexpr int Z = 0x5A;

// ── ファンクションキー ────────────────────────────────
constexpr int F1  = 0x70;  constexpr int F2  = 0x71;
constexpr int F3  = 0x72;  constexpr int F4  = 0x73;
constexpr int F5  = 0x74;  constexpr int F6  = 0x75;
constexpr int F7  = 0x76;  constexpr int F8  = 0x77;
constexpr int F9  = 0x78;  constexpr int F10 = 0x79;
constexpr int F11 = 0x7A;  constexpr int F12 = 0x7B;

// ── 記号 (US 配列基準) ────────────────────────────────
constexpr int Semicolon   = 0xBA;  // ;:
constexpr int Equal       = 0xBB;  // =+
constexpr int Comma       = 0xBC;  // ,<
constexpr int Minus       = 0xBD;  // -_
constexpr int Period      = 0xBE;  // .>
constexpr int Slash       = 0xBF;  // /?
constexpr int Backquote   = 0xC0;  // `~
constexpr int LeftBracket = 0xDB;  // [{
constexpr int Backslash   = 0xDC;  // \|
constexpr int RightBracket= 0xDD;  // ]}
constexpr int Quote       = 0xDE;  // '"

}  // namespace mitiru::keys
