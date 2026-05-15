#pragma once

/// @file InlineMacro.hpp
/// @brief MITIRU_INLINE マクロ — header-only / 静的ライブラリ両モード対応
/// @details MITIRU_HEADER_ONLY=1 のとき inline、それ以外で空定義。
///          各 .hpp の関数定義に冠することで、将来 .cpp に分離する候補を
///          機械的にマークしつつ、現状は header-only の挙動を維持する。
///
/// 使用例 (Phase 2 以降):
/// @code
/// // include/mitiru/network/ReliableUDP.hpp
/// MITIRU_INLINE void ReliableUDP::send(std::span<const std::byte> data) {
///     // ...
/// }
/// @endcode
///
/// Phase 1 では本マクロを既存ヘッダーへ適用しない。
/// Phase 2 パイロットモジュール分離時に初めて使用する。

#if defined(MITIRU_HEADER_ONLY)
    #define MITIRU_INLINE inline
#else
    #define MITIRU_INLINE
#endif

namespace mitiru::core
{
// このヘッダーは MITIRU_INLINE マクロのみを提供する。
// 名前空間自体は意図的に空 — マクロは名前空間に属さないが、
// プロジェクト規約 (mitiru/<subdir>/ → namespace mitiru::<subdir>) を満たすため宣言だけ置く。
} // namespace mitiru::core
