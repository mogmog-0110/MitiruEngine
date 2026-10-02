#pragma once

/// @file mitiru.hpp
/// @brief ゲーム作者向けの入口ヘッダ。これ 1 つを include すれば、ゲームを書くのに
///        必要なもの（Game / Input / Hud / Screen / Color / Rect、乱数 Random、
///        MITIRU_GAME）がまとめて入る。
/// @details
/// @code
///   #include <mitiru.hpp>
///   using namespace mitiru;
///
///   struct MyGame {
///       float x = 600;
///       void update(Input in, Hud hud, float dt) { x += in.move().x * 400 * dt; }
///       void draw(Screen& s) { s.drawRect(x, 300, 40, 40, color::White); }
///   };
///   MITIRU_GAME(MyGame)
/// @endcode
///
/// 個別ヘッダ（<mitiru/module/Game.hpp> など）を直接 include しても結果は同じ。
/// このヘッダは「作者が 1 行で始められる」ための簡素な入口。エンジン内部では
/// 従来どおり、使うものだけを明示的に include する。

#include <mitiru/module/Game.hpp>   // ゲームの枠組み (Input / Hud / Screen / Key / Pad / MITIRU_GAME)
#include <mitiru/module/PodLayout.hpp>  // MITIRU_ASSERT_NO_PADDING
#include <mitiru/core/Random.hpp>   // 乱数 mitiru::Random (seed 固定でリプレイ決定論)
