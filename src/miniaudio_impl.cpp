/// @file miniaudio_impl.cpp
/// @brief miniaudio 実装定義 (単一翻訳単位)

// miniaudio は stb_vorbis の宣言が先に見えている時だけ Vorbis デコーダを組み込む。
// 実装側は miniaudio の実装より後に置く (miniaudio の README の順序)。
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
