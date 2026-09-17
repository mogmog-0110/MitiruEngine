/// @file CefIncludeGuardEnd.hpp
/// @brief CEF ヘッダの include 後に CHECK/DCHECK 系マクロを元の定義 (Catch2 の CHECK や未定義) へ復元する End 側。
///
/// CefIncludeGuardBegin.hpp と対で使う。

#pragma pop_macro("CHECK")
#pragma pop_macro("CHECK_OP")
#pragma pop_macro("CHECK_EQ")
#pragma pop_macro("CHECK_NE")
#pragma pop_macro("CHECK_LE")
#pragma pop_macro("CHECK_LT")
#pragma pop_macro("CHECK_GE")
#pragma pop_macro("CHECK_GT")
#pragma pop_macro("PCHECK")
#pragma pop_macro("DCHECK")
#pragma pop_macro("DCHECK_OP")
#pragma pop_macro("DCHECK_EQ")
#pragma pop_macro("DCHECK_NE")
#pragma pop_macro("DCHECK_LE")
#pragma pop_macro("DCHECK_LT")
#pragma pop_macro("DCHECK_GE")
#pragma pop_macro("DCHECK_GT")

// 最外の対を閉じた後も CEF 側の定義が残る場合、この対より前に CEF ヘッダがガードなしで include されている。
// Catch2 の CHECK が no-op になるため、ビルドエラーにする。
// 内側では外側の状態を一時保存するだけなので検査しない。
#ifndef MITIRU_CEF_GUARD_NESTED
#undef MITIRU_CEF_GUARD_ACTIVE
#if defined(DCHECK) || defined(CHECK_EQ) || defined(PCHECK)
#error "CEF の CHECK/DCHECK 系マクロが CefIncludeGuardBegin/End の外で define されている。生 CEF ヘッダの include を CefIncludeGuardBegin/End.hpp で挟むこと"
#endif
#endif
#pragma pop_macro("MITIRU_CEF_GUARD_NESTED")
