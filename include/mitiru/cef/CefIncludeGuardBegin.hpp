/// @file CefIncludeGuardBegin.hpp
/// @brief CEF ヘッダの CHECK/DCHECK 系マクロから呼び出し元を守る Begin 側。
///
/// CEF の include/base/cef_logging.h は CHECK/DCHECK 系マクロを無条件に定義し、Catch2 の同名マクロを上書きする。
/// 上書きされた TU では CHECK(...) が Catch2 に登録されない no-op になり、テストが常に緑になる
/// (tests/mitiru/TestEngineBranchCandidates.cpp で実証)。
/// 生 CEF ヘッダの直前でこのヘッダを、直後で CefIncludeGuardEnd.hpp を include する。
/// include のたびに push/pop するため、#pragma once は付けない。
/// push_macro/pop_macro のスタックにより、何段でもネストできる。

// End 側で最外か内側かを判定する。最外では ACTIVE、内側では NESTED を立てる。
#pragma push_macro("MITIRU_CEF_GUARD_NESTED")
#undef MITIRU_CEF_GUARD_NESTED
#ifdef MITIRU_CEF_GUARD_ACTIVE
#define MITIRU_CEF_GUARD_NESTED 1
#else
#define MITIRU_CEF_GUARD_ACTIVE 1
#endif

#pragma push_macro("CHECK")
#pragma push_macro("CHECK_OP")
#pragma push_macro("CHECK_EQ")
#pragma push_macro("CHECK_NE")
#pragma push_macro("CHECK_LE")
#pragma push_macro("CHECK_LT")
#pragma push_macro("CHECK_GE")
#pragma push_macro("CHECK_GT")
#pragma push_macro("PCHECK")
#pragma push_macro("DCHECK")
#pragma push_macro("DCHECK_OP")
#pragma push_macro("DCHECK_EQ")
#pragma push_macro("DCHECK_NE")
#pragma push_macro("DCHECK_LE")
#pragma push_macro("DCHECK_LT")
#pragma push_macro("DCHECK_GE")
#pragma push_macro("DCHECK_GT")
