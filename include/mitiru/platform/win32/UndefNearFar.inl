// windows.h (minwindef.h) は near と far を空のマクロにするので、ゲームのコードが near / far を名前に使えない。
// windows.h を読んだ後でこのファイルを読んで消す。SDK のヘッダが使う NEAR / FAR は同じ空の展開で残す。
// windows.h は 2 回目からは何も定義しないので、最初に読んだ所の後で 1 回消せば足りる。何度読んでもよいように
// #pragma once は付けない。min / max はエンジンが NOMINMAX を付けて windows.h を読むので出ない。

#if defined(_WIN32) && defined(_MINWINDEF_)
#undef near
#undef far
#undef NEAR
#undef FAR
#define NEAR
#define FAR
#endif
