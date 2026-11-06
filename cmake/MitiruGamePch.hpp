#pragma once

// ゲーム DLL の事前コンパイル済みヘッダー (mitiru_game_pch が付ける)。
// ゲームの .cpp に #include を足す必要はなく、足してあっても害はない。
// 標準ヘッダーは Game.hpp が既に引く物と、ゲームの .cpp が最もよく引く物。
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <mitiru/module/Game.hpp>
