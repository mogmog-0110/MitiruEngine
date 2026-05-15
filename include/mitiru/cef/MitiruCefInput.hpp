#pragma once

/// @file MitiruCefInput.hpp
/// @brief mitiru::InputState → CefBrowserHost イベント変換

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "include/cef_browser.h"

#include <mitiru/input/InputState.hpp>

namespace mitiru::cef
{

/// @brief 入力変換ユーティリティ
/// @details mitiru の InputState を毎フレーム処理し、差分を CefBrowserHost に転送する。
///          インスタンスをシーンごとに持つことで、シーン遷移時に状態がリセットされる。
class MitiruCefInput
{
public:
    MitiruCefInput() = default;

    /// @brief 毎フレーム呼ぶ — 前フレームとの差分イベントを送信する
    /// @param host   対象ブラウザホスト (null なら no-op)
    /// @param input  現在の入力状態
    void update(CefBrowserHost* host, const InputState& input)
    {
        if (!host)
        {
            return;
        }

        sendMouse(host, input);
        sendKeys(host, input);
    }

private:
    // ── マウス ────────────────────────────────────────────────
    void sendMouse(CefBrowserHost* host, const InputState& input)
    {
        const auto [mx, my] = input.mousePosition();

        // マウス移動
        if (mx != m_prevMx || my != m_prevMy)
        {
            CefMouseEvent ev;
            ev.x = mx;
            ev.y = my;
            ev.modifiers = buildModifiers(input);
            host->SendMouseMoveEvent(ev, false);
            m_prevMx = mx;
            m_prevMy = my;
        }

        // 左ボタン
        const bool ldown = input.isMouseButtonDown(MouseButton::Left);
        if (ldown != m_prevLDown)
        {
            CefMouseEvent ev;
            ev.x = mx;
            ev.y = my;
            ev.modifiers = buildModifiers(input);
            host->SendMouseClickEvent(ev, MBT_LEFT,
                ldown ? false : true,  // mouse_up = !ldown
                1);
            m_prevLDown = ldown;
        }

        // 右ボタン
        const bool rdown = input.isMouseButtonDown(MouseButton::Right);
        if (rdown != m_prevRDown)
        {
            CefMouseEvent ev;
            ev.x = mx;
            ev.y = my;
            ev.modifiers = buildModifiers(input);
            host->SendMouseClickEvent(ev, MBT_RIGHT, rdown ? false : true, 1);
            m_prevRDown = rdown;
        }

        // マウスホイール: InputState にホイールデルタ API が未実装のため、
        // 将来 InputState::mouseWheelDelta() が追加されたら実装する。
    }

    // ── キーボード ─────────────────────────────────────────────
    void sendKeys(CefBrowserHost* host, const InputState& input)
    {
        // InputState が押下イベントをバッファしている場合はそれを使う。
        // 簡易実装: 各スキャンコード 0x00–0xFF をポーリング
        // (高速実装が必要な場合は InputState 側にイベントキューを追加する)
        for (int vk = 0x08; vk < 0xFF; ++vk)
        {
            const bool now  = input.isKeyDown(static_cast<uint8_t>(vk));
            const bool prev = m_prevKeys[vk];
            if (now == prev)
            {
                continue;
            }
            m_prevKeys[vk] = now;

            CefKeyEvent ev;
            ev.windows_key_code = vk;
            ev.native_key_code  = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
            ev.modifiers        = buildModifiers(input);
            ev.is_system_key    = false;
            ev.type             = now ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;
            host->SendKeyEvent(ev);

            // キー押下時に文字イベントも送る (表示可能文字のみ)
            if (now && vk >= 0x20 && vk < 0x7F)
            {
                CefKeyEvent charEv   = ev;
                charEv.type          = KEYEVENT_CHAR;
                charEv.character     = static_cast<char16_t>(vk);
                charEv.unmodified_character = static_cast<char16_t>(vk);
                host->SendKeyEvent(charEv);
            }
        }
    }

    // ── モディファイアビット構築 ──────────────────────────────
    /// @brief CefMouseEvent.modifiers / CefKeyEvent.modifiers 用ビット集合
    /// @details マウスボタン状態を含めるのが重要。CEF は SendMouseMoveEvent
    ///          側で押下中ボタンを推測しないため、modifiers に
    ///          EVENTFLAG_*_MOUSE_BUTTON が立っていないと renderer は
    ///          drag 中の move を "hover (buttons=0)" として配信する。
    ///          結果 PointerEvent.buttons が 0 になり、HTML 側ドラッグエンジン
    ///          (mitiru.drag 等) がドロップを取り逃す。
    static uint32_t buildModifiers(const InputState& input)
    {
        uint32_t mod = 0;
        if (input.isKeyDown(VK_SHIFT))   mod |= EVENTFLAG_SHIFT_DOWN;
        if (input.isKeyDown(VK_CONTROL)) mod |= EVENTFLAG_CONTROL_DOWN;
        if (input.isKeyDown(VK_MENU))    mod |= EVENTFLAG_ALT_DOWN;
        if (input.isMouseButtonDown(MouseButton::Left))   { mod |= EVENTFLAG_LEFT_MOUSE_BUTTON; }
        if (input.isMouseButtonDown(MouseButton::Middle)) { mod |= EVENTFLAG_MIDDLE_MOUSE_BUTTON; }
        if (input.isMouseButtonDown(MouseButton::Right))  { mod |= EVENTFLAG_RIGHT_MOUSE_BUTTON; }
        return mod;
    }

    // ── 前フレーム状態 ────────────────────────────────────────
    int  m_prevMx     = 0;
    int  m_prevMy     = 0;
    bool m_prevLDown  = false;
    bool m_prevRDown  = false;
    bool m_prevKeys[256] = {};
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
