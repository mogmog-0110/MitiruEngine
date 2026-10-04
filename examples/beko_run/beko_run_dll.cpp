// beko_run。赤べこの横スクロールアクション（会津のみち）。
//   level.hpp  … ステージのデータ（寸法・足場・敵・古銭）
//   sprites.hpp… 画像の読み込みと石畳の描画
//   game.hpp   … ゲームの状態（GameMemory）と入口の宣言
//   game.cpp   … ゲーム本体（update・draw の中身）
// このファイルは DLL の入口を作るだけ。
// 実行:  mitiru_host.exe beko_run/beko_run.dll
#include <mitiru/module/AutoReflect.hpp>

#include "game.hpp"

// inspector（別窓）に表示する状態のフィールドを宣言する。GameMemory (BekoRun) が
// aggregate なので、列挙せずに全フィールドを自動反射する。
MITIRU_REFLECT_AUTO(BekoRun);

MITIRU_ASSERT_NO_PADDING(BekoRun);
// 巻き戻し（time-travel）用に、毎フレームの状態を 600 フレーム分（約 10 秒）記録する。
MITIRU_REWIND_BUFFER(600);

MITIRU_GAME(BekoRun);
