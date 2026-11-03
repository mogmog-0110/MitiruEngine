#pragma once

/// @file SignalsmithInclude.hpp
/// @brief Signalsmith Stretch (external/signalsmith-stretch、MIT) を、エンジンの警告の設定で止まらないように include する

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4305 4456 4457 4458 4459 4702 4100 4127 4189 4324 4245 4018 4389 4800)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <signalsmith-stretch/signalsmith-stretch.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
