/// @file xxhash_impl.cpp
/// @brief xxHash の実装を 1 つの翻訳単位だけに置く
/// @details util/Hash.hpp は宣言だけを include する。XXH_NAMESPACE は Hash.hpp と揃える
///          (ゲーム側が別の xxHash をリンクしてもシンボルが衝突しないように)。

#define XXH_NAMESPACE mitiru_
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include <xxhash/xxhash.h>
