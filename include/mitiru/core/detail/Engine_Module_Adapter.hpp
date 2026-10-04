// mitiru::Engine の detail header。直接 include しないこと。core/Engine.hpp 経由で include される
#pragma once

/// @file Engine_Module_Adapter.hpp
/// @brief Engine の module per-frame signal flow の out-of-class 定義 (v0.2.0 step 2-3)
/// @details
/// `Engine::runModule` (stack-local ModuleAdapter) と、host と game を
/// C の関数と生データだけでつなぐ per-frame signal flow。中身は次のとおり。
///   - InputSnapshot 構築 (host が input + action events を POD に詰める)
///   - FrameIntents drain (DLL の要求を host が解釈して engine 操作に変換)
///   - 必要なら StateStore + SharedSnapshot を遅延生成
/// load / unload / ring 記録は Engine_Module_Loader.hpp 側。

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#ifdef __EMSCRIPTEN__
#include <mitiru/audio/WebAudioEngine.hpp>
#endif

#ifdef _WIN32
#include <mitiru/platform/win32/Win32Window.hpp>
#endif

#include <mitiru/bridge/StateStore.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/core/detail/ModuleInputDevices.hpp>
#include <mitiru/core/detail/GpuPassTimes.hpp>
#include <mitiru/core/detail/ModuleTextInput.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/CrashReport.hpp>
#include <mitiru/debug/InspectorLauncher.hpp>
#include <mitiru/debug/DebugPrint.hpp>
#include <mitiru/debug/TracyZones.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/DrawCommands.hpp>
#include <mitiru/module/GameMemorySave.hpp>
#include <mitiru/module/ModuleHost.hpp>
#include <mitiru/module/SoundIntentRouter.hpp>
#include <mitiru/module/detail/HostBoundary50.hpp>
#include <mitiru/module/detail/LayerDt.hpp>
#include <mitiru/observe/GameMemoryRing.hpp>
#include <mitiru/observe/Reflect.hpp>
#include <mitiru/observe/SeriesMarkers.hpp>
#include <mitiru/observe/SharedSnapshot.hpp>
#include <mitiru/observe/SideStateInspect.hpp>
#include <mitiru/render/SaveScreenshotPng.hpp>

// ── Free helper 群 (file-local、Engine の method ではない) ──────────────────
namespace mitiru::module::detail
{

/// @brief `src` を固定長 buffer に copy する。source が buffer より長くても
///        UB を起こさず、切り詰めて null 終端する。
template <std::size_t N>
inline void copyBounded(char (&dst)[N], const std::string& src) noexcept
{
	const std::size_t cap = N > 0 ? N - 1 : 0;
	const std::size_t n   = src.size() < cap ? src.size() : cap;
	if (n > 0) { std::memcpy(dst, src.data(), n); }
	if (N > 0) { dst[n] = '\0'; }
}

/// @brief 固定長 buffer の bounded strlen。DLL からの wire buffer は null 終端を
///        信頼しない (exportedInspectables と同基準の境界防御)。
template <std::size_t N>
[[nodiscard]] inline std::size_t boundedLen(const char (&s)[N]) noexcept
{
	std::size_t l = 0;
	while (l < N && s[l] != '\0') { ++l; }
	return l;
}

/// @brief inspector の "gameMemory" セクション ({title,order,meta,state}) を組み立てる。
/// @details JSON object は key を並べ替えるので、窓がコード順 (MITIRU_REFLECT の並び) で出せるよう順序を
///          order に別に添える。meta (typeTag/elemType) は enum: / range: の field を select / スライダーにするために使う。
inline nlohmann::json buildGameMemoryJson(
	const mitiru::module::ModuleReflection& refl,
	const void*                             memory,
	std::uint32_t                           memorySize)
{
	nlohmann::json fieldOrder = nlohmann::json::array();
	nlohmann::json fieldMeta  = nlohmann::json::object();
	for (const auto& f : refl.fields)
	{
		fieldOrder.push_back(f.name);
		fieldMeta[f.name] = nlohmann::json{{"typeTag", f.typeTag}, {"elemType", f.elemType}};
	}
	return nlohmann::json{
		{"title", "Game memory"},
		{"order", std::move(fieldOrder)},
		{"meta", std::move(fieldMeta)},
		{"state", mitiru::observe::reflectToJson(
			static_cast<const std::uint8_t*>(memory), memorySize,
			refl.fieldsData(), refl.fieldCount(),
			refl.schemasData(), refl.schemaCount())}};
}

/// @brief `FrameIntents::debugDraws` の減衰保持 (v30、§9-1)。intent 自体は録画されない
///        (ModuleApi.hpp の DebugDrawIntent コメント参照) ので、durationSec > 0 のものを
///        複数フレーム描き続ける役目は host (この tracker) が担う。ModuleAdapter /
///        StaticAdapter がそれぞれ 1 個ずつ持つ (runModule 呼び出しの生存期間 = 対局全体)。
struct DebugDrawTracker
{
	struct Slot
	{
		mitiru::module::DebugDrawIntent draw;
		float remain = 0.0f;
		bool  active = false;
	};
	Slot slots[256];

	/// このフレームの intents を取り込む。既存 slot を dt 分だけ減衰させ、
	/// 期限切れを外してから新規分を空き slot へ入れる (満杯なら何も知らせずに捨てる)。
	void ingest(const mitiru::module::FrameIntents& intents, float dt) noexcept
	{
		for (auto& s : slots)
		{
			if (!s.active) { continue; }
			s.remain -= dt;
			if (s.remain <= 0.0f) { s.active = false; }
		}
		const int cap = static_cast<int>(sizeof(intents.debugDraws) / sizeof(intents.debugDraws[0]));
		const int n   = std::min<int>(intents.debugDrawCount, cap);
		for (int i = 0; i < n; ++i)
		{
			for (auto& s : slots)
			{
				if (s.active) { continue; }
				s.draw   = intents.debugDraws[i];
				// durationSec=0 は「このフレームのみ」の意味で、次の ingest で必ず期限切れにする。
				s.remain = (s.draw.durationSec > 0.0f) ? s.draw.durationSec : dt;
				s.active = true;
				break;
			}
		}
	}
};

/// @brief アクティブな DebugDrawTracker の内容を Screen へ重ねて描く。
/// @details 3D カメラは game の on_draw (camera3D 呼び出し) が設定済みという前提で
///          `Screen::projectToScreen` を使い、screen-space の線 / 円 / テキストへ変換する
///          (専用の 3D 線描画パスが無いための実用的な近似。球は半径方向に 1 点だけ
///          投影しておおよその画面半径を出す簡易法)。カメラ未設定 (projectToScreen が
///          false) の draw は何も知らせずにスキップする。
inline void drawDebugDraws(mitiru::Screen& screen, const DebugDrawTracker& tracker) noexcept
{
	for (const auto& s : tracker.slots)
	{
		if (!s.active) { continue; }
		const auto& d = s.draw;
		const sgc::Colorf color{d.color[0], d.color[1], d.color[2], d.color[3]};

		if (d.kind == 1)  // 線分
		{
			float x0, y0, x1, y1;
			const bool on0 = screen.projectToScreen({d.a[0], d.a[1], d.a[2]}, x0, y0);
			const bool on1 = screen.projectToScreen({d.b[0], d.b[1], d.b[2]}, x1, y1);
			if (on0 && on1) { screen.drawLine({x0, y0}, {x1, y1}, color, 1.5f); }
		}
		else if (d.kind == 2)  // 箱 (8 頂点を投影して 12 辺)
		{
			sgc::Vec2f corners[8];
			bool onScreen[8];
			for (int i = 0; i < 8; ++i)
			{
				const float sx = d.a[0] + ((i & 1) ? d.b[0] : -d.b[0]);
				const float sy = d.a[1] + ((i & 2) ? d.b[1] : -d.b[1]);
				const float sz = d.a[2] + ((i & 4) ? d.b[2] : -d.b[2]);
				float px, py;
				onScreen[i] = screen.projectToScreen({sx, sy, sz}, px, py);
				corners[i]  = {px, py};
			}
			static constexpr int kEdges[12][2] = {
				{0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7}};
			for (const auto& e : kEdges)
			{
				if (onScreen[e[0]] && onScreen[e[1]])
				{ screen.drawLine(corners[e[0]], corners[e[1]], color, 1.5f); }
			}
		}
		else if (d.kind == 3)  // 球 (中心 + 半径方向 1 点から画面半径を近似)
		{
			float cx, cy, ex, ey;
			const bool onC = screen.projectToScreen({d.a[0], d.a[1], d.a[2]}, cx, cy);
			const bool onE = screen.projectToScreen({d.a[0] + d.b[0], d.a[1], d.a[2]}, ex, ey);
			if (onC && onE)
			{
				const float dx = ex - cx, dy = ey - cy;
				const float r  = std::sqrt(dx * dx + dy * dy);
				screen.drawCircle({cx, cy}, r, color);
			}
		}
		else if (d.kind == 4)  // 文字
		{
			float px, py;
			if (screen.projectToScreen({d.a[0], d.a[1], d.a[2]}, px, py))
			{
				const int len = static_cast<int>(boundedLen(d.text));
				screen.drawTextClipped({px, py, 256.0f, 24.0f},
				                        std::string_view{d.text, static_cast<std::size_t>(len)}, color);
			}
		}
	}
}

/// @brief 分岐エディタのシーンビュー (ADR 0035 O2) が使う「画面上の枠」1 個。
/// `Canvas::beginObject(name)` でタグ付けされたコマンドの screen-space bbox を merge した結果。
struct SceneViewObject
{
	std::uint32_t sourceId{};  ///< `fnv1a32(name)`。host 側で反射記述子の名前と突き合わせる
	float x{}, y{}, w{}, h{};
};

/// @brief 現在フレームで `drainDrawCommands` が集めた `SceneViewObject` 列。draw() 直後に
/// クリアして詰め、6 フレームに 1 度 (drainModuleFrameIntents の perf/gameMemory と同じ cadence)
/// SharedSnapshot へ書き出す。Engine.hpp にメンバを増やさず、この header 内で完結させるための
/// アクセサ (単一 host プロセス・単一描画スレッドという既存の前提は他の thread_local と同じ)。
inline std::vector<SceneViewObject>& lastSceneViewObjects() noexcept
{
	static thread_local std::vector<SceneViewObject> objects;
	return objects;
}

/// @brief `Canvas::beginObject(name, fieldX, fieldY)` (ADR 0035 O2/O3 追記) が明示した
/// ドラッグ書き戻し先。`name+".x"/".y"` 規約を満たせない object (beko_run の px/py 等) 用の
/// 上書き情報で、reflect フィールド名との自動突き合わせより優先して使う。
struct SceneFieldMapping
{
	std::uint32_t sourceId{};
	std::string   fieldX;
	std::string   fieldY;
};

inline std::vector<SceneFieldMapping>& lastSceneFieldMappings() noexcept
{
	static thread_local std::vector<SceneFieldMapping> mappings;
	return mappings;
}

/// @brief `sourceId` の bbox を `sceneOut` へ merge する (同じ id が複数コマンドに乗っていれば
/// 外接矩形に広げる。通常は 1 object = 1 コマンドなので線形探索で十分)。
inline void mergeSceneViewObject(std::vector<SceneViewObject>& sceneOut,
                                  std::uint32_t sourceId, const sgc::Rectf& rect) noexcept
{
	for (auto& o : sceneOut)
	{
		if (o.sourceId != sourceId) { continue; }
		const float x0 = std::min(o.x, rect.x()),         y0 = std::min(o.y, rect.y());
		const float x1 = std::max(o.x + o.w, rect.x() + rect.width());
		const float y1 = std::max(o.y + o.h, rect.y() + rect.height());
		o.x = x0; o.y = y0; o.w = x1 - x0; o.h = y1 - y0;
		return;
	}
	sceneOut.push_back(SceneViewObject{sourceId, rect.x(), rect.y(), rect.width(), rect.height()});
}

/// @brief `DrawCommandBuffer` (ADR 0025、ABI v32) を既存の `Screen` API へ 1:1 再生する。
/// game 側で inline 展開された Screen 呼び出しが host のコンテナへ直接触るのが H-1/H-4 の
/// 実体だったので、その inline 展開を host 側のこの関数だけに閉じ込める。
/// @param sceneOut 非 null なら、`sourceId != 0` のコマンドの screen-space bbox を集める
///        (ADR 0035 O2)。実際の座標変換 (camera 等) は Screen 側スタックが持つため、ここで
///        読む `c.p[]` は「コマンド発行時点の値」であり push/popTransform を反映しない近似値。
inline void drainDrawCommands(mitiru::Screen& screen, const mitiru::module::DrawCommandBuffer& buf,
                               mitiru::render::SpriteCache& spriteCache,
                               std::vector<SceneViewObject>* sceneOut = nullptr) noexcept
{
	using mitiru::module::DrawCmdKind;

	if (buf.droppedCount > 0)
	{
		mitiru::debug::warnOnce("draw.list.overflow",
			"1 フレームの描画の命令が上限を超えたので、一部を描きませんでした。1 フレームに描く数を減らしてください。");
	}

	// textPool 参照を bounded に文字列化する (offset/len が改ざん・破損していても
	// out-of-range read をしない。v21 の bounded 読み統一と同型)。
	const auto textOf = [&buf](std::uint32_t offset, std::uint32_t len) -> std::string_view
	{
		if (offset >= mitiru::module::kTextPoolBytes) { return {}; }
		const std::uint32_t clampedLen = std::min(len, mitiru::module::kTextPoolBytes - offset);
		return std::string_view{buf.textPool + offset, clampedLen};
	};
	const auto rectOf = [](const mitiru::module::DrawCommand& c) noexcept
	{
		return sgc::Rectf{c.p[0], c.p[1], c.p[2], c.p[3]};
	};
	const auto colorOf = [](const float (&c)[4]) noexcept
	{
		return sgc::Colorf{c[0], c[1], c[2], c[3]};
	};

	// ADR 0035 O2: sceneOut 用に PushTransform/PopTransform (translate+scale のみ) を
	// 追いかける最小の累積変換。Screen 側の実変換 (std::stack<Transform2D>、回転込み) とは
	// 別物の近似で、`Canvas::pushRotation` は追わない (ピボット回転までは MVP の範囲外、
	// docs/BRANCH_EDITOR.md に明記)。beko_run の `applyCamera` のような
	// 「1 回 push して一括 pop」の定番パターンなら screen 座標に正しく一致する。
	struct Xform2D { float tx = 0.0f, ty = 0.0f, sx = 1.0f, sy = 1.0f; };
	std::vector<Xform2D> xformStack;
	xformStack.reserve(4);
	xformStack.push_back(Xform2D{});
	const auto applyXform = [](const Xform2D& t, const sgc::Rectf& r) noexcept
	{
		return sgc::Rectf{r.x() * t.sx + t.tx, r.y() * t.sy + t.ty, r.width() * t.sx, r.height() * t.sy};
	};

	// Screen の変換スタックはフレームで reset されないので、この drain が積んだ数を数えて末尾で
	// 同じ数だけ pop する (あふれで PopTransform が捨てられていても釣り合う)。game が積んでいない
	// 分 (host の camera 等) は pop しない。
	int pushedDepth = 0;
	const std::uint32_t n = std::min(buf.count, mitiru::module::kMaxDrawCommands);
	for (std::uint32_t i = 0; i < n; ++i)
	{
		const auto& c = buf.commands[i];

		if (sceneOut != nullptr)
		{
			// current(p) = outer(pushed(p)) の合成則で積む (Screen の M_new = M_old * T_push と同じ)。
			if (c.kind == DrawCmdKind::PushTransform)
			{
				const Xform2D& outer = xformStack.back();
				const Xform2D pushed{c.p[0], c.p[1], c.p[2], c.p[3]};
				xformStack.push_back(Xform2D{
					pushed.tx * outer.sx + outer.tx, pushed.ty * outer.sy + outer.ty,
					outer.sx * pushed.sx, outer.sy * pushed.sy});
			}
			else if (c.kind == DrawCmdKind::PopTransform && xformStack.size() > 1)
			{
				xformStack.pop_back();
			}
		}

		// ADR 0035 O2/O3 追記: 非描画マーカー。fieldX/fieldY 名を textPool から復元して控える
		// (bbox マージ対象ではないので、下の switch では default に入るだけでよい)。
		if (sceneOut != nullptr && c.kind == DrawCmdKind::SceneFieldMap)
		{
			const std::string_view joined = textOf(c.textOffset, c.textLen);
			const auto sep = joined.find('\0');
			if (sep != std::string_view::npos)
			{
				lastSceneFieldMappings().push_back(SceneFieldMapping{
					c.sourceId, std::string{joined.substr(0, sep)}, std::string{joined.substr(sep + 1)}});
			}
		}

		// ADR 0035 O2: タグ付き (sourceId != 0) コマンドの bbox をシーンビュー用に集める。
		// 対応する種のみ (矩形/テキスト/スプライト系は c.p[0..3] がそのまま dst rect、
		// 円系は中心+半径から外接矩形を作る)。未対応種はピッキング対象にならないだけで
		// 描画自体は通常どおり進む。上で追った累積変換を screen 座標化に適用する。
		if (sceneOut != nullptr && c.sourceId != 0)
		{
			const Xform2D& xf = xformStack.back();
			switch (c.kind)
			{
			case DrawCmdKind::Rect: case DrawCmdKind::RectFrame:
			case DrawCmdKind::RoundedRect: case DrawCmdKind::RoundedRectFrame:
			case DrawCmdKind::GradientRect: case DrawCmdKind::GradientRectH:
			case DrawCmdKind::TextInRect: case DrawCmdKind::TextClipped: case DrawCmdKind::TextWrapped:
			case DrawCmdKind::Sprite: case DrawCmdKind::SpriteRectById:
				mergeSceneViewObject(*sceneOut, c.sourceId, applyXform(xf, rectOf(c)));
				break;
			case DrawCmdKind::Circle: case DrawCmdKind::CircleFrame:
				mergeSceneViewObject(*sceneOut, c.sourceId, applyXform(xf,
					sgc::Rectf{c.p[0] - c.p[2], c.p[1] - c.p[2], c.p[2] * 2.0f, c.p[2] * 2.0f}));
				break;
			case DrawCmdKind::Ellipse:
				mergeSceneViewObject(*sceneOut, c.sourceId, applyXform(xf,
					sgc::Rectf{c.p[0] - c.p[2], c.p[1] - c.p[3], c.p[2] * 2.0f, c.p[3] * 2.0f}));
				break;
			case DrawCmdKind::Ring: case DrawCmdKind::GlowRing:
				mergeSceneViewObject(*sceneOut, c.sourceId, applyXform(xf,
					sgc::Rectf{c.p[0] - c.p[2], c.p[1] - c.p[2], c.p[2] * 2.0f, c.p[2] * 2.0f}));
				break;
			case DrawCmdKind::SpriteById:
				mergeSceneViewObject(*sceneOut, c.sourceId, applyXform(xf, sgc::Rectf{c.p[0], c.p[1], 1.0f, 1.0f}));
				break;
			default:
				break;  // Line/Triangle/Polygon/GlowLine 等は矩形化の意味が薄く未対応
			}
		}

		switch (c.kind)
		{
		case DrawCmdKind::PushTransform:
			screen.pushTransform(c.p[0], c.p[1], c.p[2], c.p[3]);
			++pushedDepth;
			break;
		case DrawCmdKind::PopTransform:
			if (pushedDepth > 0)
			{
				screen.popTransform();
				--pushedDepth;
			}
			break;
		case DrawCmdKind::PushRotation:
			screen.pushRotation(c.p[0], c.p[1], c.p[2]);
			++pushedDepth;
			break;
		case DrawCmdKind::SetBlendMode:
			screen.setBlendMode(static_cast<mitiru::gfx::BlendMode>(c.flags));
			break;
		case DrawCmdKind::Rect:
			screen.drawRect(rectOf(c), colorOf(c.colorA));
			break;
		case DrawCmdKind::RectFrame:
			screen.drawRectFrame(rectOf(c), colorOf(c.colorA), c.p[4]);
			break;
		case DrawCmdKind::RoundedRect:
			screen.drawRoundedRect(rectOf(c), colorOf(c.colorA), c.p[4]);
			break;
		case DrawCmdKind::RoundedRectFrame:
			screen.drawRoundedRectFrame(rectOf(c), colorOf(c.colorA), c.p[4], c.p[5]);
			break;
		case DrawCmdKind::GradientRect:
			screen.drawGradientRect(rectOf(c), colorOf(c.colorA), colorOf(c.colorB));
			break;
		case DrawCmdKind::GradientRectH:
			screen.drawGradientRectH(rectOf(c), colorOf(c.colorA), colorOf(c.colorB));
			break;
		case DrawCmdKind::Circle:
			screen.drawCircle(sgc::Vec2f{c.p[0], c.p[1]}, c.p[2], colorOf(c.colorA));
			break;
		case DrawCmdKind::CircleFrame:
			screen.drawCircleFrame(sgc::Vec2f{c.p[0], c.p[1]}, c.p[2], colorOf(c.colorA), c.p[3]);
			break;
		case DrawCmdKind::Ellipse:
			screen.drawEllipse(sgc::Vec2f{c.p[0], c.p[1]}, c.p[2], c.p[3], colorOf(c.colorA));
			break;
		case DrawCmdKind::Ring:
			screen.drawRing(sgc::Vec2f{c.p[0], c.p[1]}, c.p[2], c.p[3], colorOf(c.colorA));
			break;
		case DrawCmdKind::Line:
			screen.drawLine(sgc::Vec2f{c.p[0], c.p[1]}, sgc::Vec2f{c.p[2], c.p[3]}, colorOf(c.colorA), c.p[4]);
			break;
		case DrawCmdKind::DashedLine:
			screen.drawDashedLine(sgc::Vec2f{c.p[0], c.p[1]}, sgc::Vec2f{c.p[2], c.p[3]},
			                      c.p[4], c.p[5], c.p[6], colorOf(c.colorA));
			break;
		case DrawCmdKind::Triangle:
			screen.drawTriangle(sgc::Vec2f{c.p[0], c.p[1]}, sgc::Vec2f{c.p[2], c.p[3]},
			                    sgc::Vec2f{c.p[4], c.p[5]}, colorOf(c.colorA));
			break;
		case DrawCmdKind::Polygon:
			{
				if (c.pointOffset >= mitiru::module::kPointPoolFloats) { break; }
				const std::uint32_t maxPts = (mitiru::module::kPointPoolFloats - c.pointOffset) / 2;
				const std::uint32_t pts = std::min(c.pointCount, maxPts);
				// C2: 毎コマンド std::vector を作らず、drainDrawCommands 呼び出し間で
				// 容量を持ち越す scratch を再利用する (inline 関数の function-local static
				// は ODR で TU 間共有されるので Engine インスタンス単位にはならないが、
				// 単一 host プロセスの単一描画スレッドという前提は他の drain* 実装と同じ)。
				static thread_local std::vector<sgc::Vec2f> scratchPoly;
				scratchPoly.clear();
				scratchPoly.reserve(pts);
				for (std::uint32_t k = 0; k < pts; ++k)
				{
					scratchPoly.emplace_back(buf.pointPool[c.pointOffset + k * 2 + 0],
					                          buf.pointPool[c.pointOffset + k * 2 + 1]);
				}
				screen.drawPolygon(scratchPoly, colorOf(c.colorA));
			}
			break;
		case DrawCmdKind::GlowLine:
			screen.glowLine(c.p[0], c.p[1], c.p[2], c.p[3], colorOf(c.colorA), c.p[4], c.p[5]);
			break;
		case DrawCmdKind::GlowRing:
			screen.glowRing(c.p[0], c.p[1], c.p[2], colorOf(c.colorA), c.p[3], c.p[4],
			                static_cast<int>(c.flags));
			break;
		case DrawCmdKind::TextInRect:
			screen.drawTextInRect(rectOf(c), textOf(c.textOffset, c.textLen), colorOf(c.colorA), c.p[4],
			                      static_cast<mitiru::Screen::TextAlignH>(c.flags & 0x3u),
			                      static_cast<mitiru::Screen::TextAlignV>((c.flags >> 2) & 0x3u),
			                      c.p[5], c.p[6]);
			break;
		case DrawCmdKind::TextClipped:
			screen.drawTextClipped(rectOf(c), textOf(c.textOffset, c.textLen), colorOf(c.colorA),
			                       c.p[4], c.p[5], c.p[6]);
			break;
		case DrawCmdKind::TextWrapped:
			screen.drawTextWrapped(rectOf(c), textOf(c.textOffset, c.textLen), colorOf(c.colorA),
			                       c.p[4], c.p[5], c.p[6], c.p[7]);
			break;
		case DrawCmdKind::Sprite:
			// c.textureHandle は game DLL 側の `&texture` (DrawCommands.hpp の note 参照)。
			// build fingerprint が host/DLL 同一構成を保証している前提でのみ有効な読み戻し。
			if (c.textureHandle != 0)
			{
				const auto* tex = reinterpret_cast<const mitiru::render::Texture*>(
					static_cast<std::uintptr_t>(c.textureHandle));
				screen.drawSprite(*tex,
					sgc::Rectf{c.p[0], c.p[1], c.p[2], c.p[3]},
					sgc::Rectf{c.p[4], c.p[5], c.p[6], c.p[7]},
					colorOf(c.colorA),
					(c.flags & 1u) != 0);
			}
			break;
		case DrawCmdKind::SpriteById:
			{
				// SpriteCache::get は string_view 版 (透過ハッシュ、C1) を直接呼び、
				// Screen::sprite(const char*) 経由の resolver 呼び出しと毎フレームの std::string
				// 生成を避ける (SpriteRectById と同じ経路に統一)。
				const std::string_view id = textOf(c.textOffset, c.textLen);
				if (const auto* tex = spriteCache.get(id); tex != nullptr)
				{
					const float scale = c.p[2];
					screen.drawSprite(*tex, sgc::Rectf{c.p[0], c.p[1],
						static_cast<float>(tex->width()) * scale,
						static_cast<float>(tex->height()) * scale});
				}
			}
			break;
		case DrawCmdKind::SpriteRectById:
			{
				// `Canvas::registerTexture` で id 化された sprite。SpriteById と同じ
				// SpriteCache (id → host 所有 Texture) で解決するので、DLL の Texture
				// アドレスは一切乗らない (ADR 0025 H-1/H-4 の textureHandle 課題の解)。
				const std::string_view id = textOf(c.textOffset, c.textLen);
				if (const auto* tex = spriteCache.get(id); tex != nullptr)
				{
					screen.drawSprite(*tex,
						sgc::Rectf{c.p[0], c.p[1], c.p[2], c.p[3]},
						sgc::Rectf{c.p[4], c.p[5], c.p[6], c.p[7]},
						colorOf(c.colorA),
						(c.flags & 1u) != 0);
				}
			}
			break;
		default:
			break;
		}
	}
	for (; pushedDepth > 0; --pushedDepth) { screen.popTransform(); }
}

}  // namespace mitiru::module::detail


MITIRU_INLINE bool mitiru::Engine::runModule(
	const std::filesystem::path& modulePath, const EngineConfig& configIn)
{
	// loadModule は m_config.packPath (pack の mount) と m_config.collisionPath (物理問い合わせの地形) を
	// 読むが、configIn 全体が m_config に写るのは後の run()→initialize()。この 2 つだけ先に写す。
	// 空も写す (同じ Engine の再実行で前の値を引きずらない)。
	m_config.packPath      = configIn.packPath;
	m_config.collisionPath = configIn.collisionPath;
	if (!loadModule(modulePath))
	{
		// 何も出さずに return すると「窓が出ず exit 0」で原因不明になる (#hello-game)。
		// 理由を明示し false を返す → host は非ゼロ終了 → ランチャー .bat が pause する。
		const std::string reason = m_moduleHost ? m_moduleHost->lastError() : std::string{};
		const char* hint = "";
		if (reason.find("missing required export") != std::string::npos)
		{
			hint = "その DLL に MITIRU_GAME(型名) の入口があるか確かめてください。";
		}
		else if (reason.find("ABI の版") != std::string::npos)
		{
			hint = "game を今の host と同じ版・同じ構成でビルドし直してください。";
		}
		console::noticef("ゲームの DLL %s を読めません (%s)。%s", modulePath.string().c_str(), reason.c_str(), hint);
		return false;
	}

	// stack-local な Game adapter。既存の engine main loop を C-only の
	// signal flow へ橋渡しする:
	//   - update(): input を snapshot、intents を zero、on_update、intents を drain
	//   - draw():   Screen pointer をそのまま on_draw へ渡す
	class ModuleAdapter : public Game
	{
	public:
		explicit ModuleAdapter(Engine* engine) noexcept : m_engine(engine) {}

		void update(float dt) override
		{
			MITIRU_ZONE_NAMED("Engine::ModuleAdapter::update");
			m_engine->ensureModuleBindings();
			// 落ちた game は新しい DLL が来るまで呼ばない (停止の通知は Engine_Frame が描く)。
			if (m_engine->moduleFaulted()) { return; }
			debug::crashContext().frame.store(m_engine->frameNumber(), std::memory_order_relaxed);
			// 過去フレームで静止 (scrub-hold): 別窓のバーで過去を選んでいる間は、その
			// フレームを毎フレーム復元して止める。ゲームを前進させず記録もしない。
			if (m_engine->applyScrubHold())
			{
				// on_update を呼ばないので drain の中の書き出しも起きない。止めたバーの窓が自分の節を
				// 読み続けて ▶ を押せるよう、ここで書く。
				m_engine->publishToolSnapshot();
				return;
			}
			// 実効 dt (pause/hitStop gating) も snapshot 構築時に書き込む (v21、H-3)。
			m_engine->buildModuleInputSnapshot(dt);
			m_engine->applyResimInputOverride();  // resim 中は記録入力で上書き
			// dt は snapshot の値を渡す (v21、H-3)。live は build 時の実効値、replay / resim は override が
			// 再投入した記録値。dt gating も記録系の内側になり、GUI 録画 → headless 再生が bit-exact に成立する。
			// on_update 後の確定 GameMemory (と窓口) を rewind ring に記録する。replay の state slot と同一 bytes。
			if (!m_engine->runModuleFrameBody()) { return; }

			// デバッグ描画 intent の取り込み (v30、§9-1)。intent は録画されないので、
			// live 実行中だけこの tracker が減衰を持つ (replay 中は再現されない旨は
			// Hud::debugLine のコメント参照)。
			if (m_engine->m_moduleFrameIntents) { m_debugDraws.ingest(*m_engine->m_moduleFrameIntents, dt); }

			// Replay record hook (axis 4): このフレームの input + 結果の intents を
			// host に渡し、.mtrr へ追記できるようにする (mitiru run --record)。
			if (m_engine->m_config.onModuleFrameRecorded
			    && m_engine->m_moduleInputSnapshot && m_engine->m_moduleFrameIntents)
			{
				m_engine->m_config.onModuleFrameRecorded(
					*m_engine->m_moduleInputSnapshot,
					*m_engine->m_moduleFrameIntents);
			}
		}

		void draw(Screen& screen) override
		{
			MITIRU_ZONE_NAMED("Engine::ModuleAdapter::draw");
			const auto& fx = m_engine->m_moduleVisualFx;

			// Shake (kind=4): game 描画全体を frame index ベースの決定的オフセットで
			// 平行移動する (乱数なし。リプレイ bit-exact)。
			const bool shaking = fx.shakeActive();
			float shakeFracX = 0.0f;
			float shakeFracY = 0.0f;
			if (shaking)
			{
				const auto off = fx.shakeOffset(m_engine->frameNumber());
				screen.pushTransform(off.dx, off.dy);
				// 3D はカメラから描くので 2D の変換が掛からない。同じ量を画面に対する割合で渡す
				const auto* snap = m_engine->m_moduleInputSnapshot.get();
				if (snap != nullptr && snap->logicalW > 0 && snap->logicalH > 0)
				{
					shakeFracX = off.dx / static_cast<float>(snap->logicalW);
					shakeFracY = off.dy / static_cast<float>(snap->logicalH);
				}
			}
			if (!m_engine->m_config.cameraShake) { shakeFracX = 0.0f; shakeFracY = 0.0f; }
			if (m_engine->m_renderer3D) { m_engine->m_renderer3D->setCameraShake(shakeFracX, shakeFracY); }

			const auto& api = m_engine->moduleApi();
			if (api.on_draw_commands != nullptr)
			{
				module::DrawContext ctx{};
				if (m_engine->m_moduleInputSnapshot)
				{
					ctx.logicalW = m_engine->m_moduleInputSnapshot->logicalW;
					ctx.logicalH = m_engine->m_moduleInputSnapshot->logicalH;
				}
				ctx.net = screen.netView();
				ctx.netMode = screen.netModeView();
				// 328 KiB 級の buffer なのでスタックに積まず、フレームごとに count だけ
				// 初期化して使い回す (未使用分の古いコマンドは count 外なので無害)。
				static thread_local module::DrawCommandBuffer buf;
				buf.count = 0;
				buf.droppedCount = 0;
				// textPool/pointPool も毎フレーム reset する (count と同じく per-frame pool。
				// 未 reset だと used が単調増加し続け、いずれ枯渇して以後の文字列/頂点コマンドが
				// 全て drop される。beko_run の drawStone (id 文字列を毎タイル push) で実際に
				// 19 フレーム目前後から drop が発生することを確認した)。
				buf.textPoolUsed  = 0;
				buf.pointPoolUsed = 0;
				// 落ちて戻した版の途中までのコマンドは描かない (shake の pop まで続けるので return しない)。
				if (!m_engine->callModuleDrawCommands(&ctx, &buf)) { buf.count = 0; }
				auto& sceneObjs = module::detail::lastSceneViewObjects();
				sceneObjs.clear();
				module::detail::lastSceneFieldMappings().clear();
				module::detail::drainDrawCommands(screen, buf, m_engine->m_spriteCache, &sceneObjs);
			}
			else if (api.on_draw != nullptr)
			{
				(void)m_engine->callModuleDraw(&screen);
			}

			if (shaking) { screen.popTransform(); }

			// デバッグ描画の重ね描き (v30、§9-1)。game の on_draw が camera3D を設定済みの
			// 前提で screen-space へ投影する。shake の外 (デバッグ表示自体は揺らさない)。
			module::detail::drawDebugDraws(screen, m_debugDraws);

			// 分岐候補ゴースト (ADR 0035 O4)。live の draw() 直後に呼ぶ契約 (Engine::drawGhost と
			// 同型)。ModuleAdapter::draw が Screen& を直接持つのはここだけなので、host の描画
			// ループを経由せずこの場で呼べる (docs/BRANCH_EDITOR.md の「未配線」を解消)。slot が
			// 1 つも active でなければ内部の空ループのみで実質 no-op。
			m_engine->drawCandidateBranches(screen);

			// Letterbox (kind=6): 上下黒帯。transform の外なので shake の影響を受けない。
			// fade の覆いより先に描くので、帯の上に fade が乗る。
			const float lb = fx.letterboxAmount();
			if (lb > 0.0f)
			{
				const float w    = static_cast<float>(screen.width());
				const float h    = static_cast<float>(screen.height());
				const float band = h * 0.12f * lb;  // 上下それぞれの帯高さ (px)
				const sgc::Colorf black{0.0f, 0.0f, 0.0f, 1.0f};
				screen.drawRect(sgc::Rectf{0.0f, 0.0f, w, band}, black);
				screen.drawRect(sgc::Rectf{0.0f, h - band, w, band}, black);
			}

			// FadeOut/FadeIn (kind=2/3) の覆い。transform の外で描くので shake に
			// 影響されず、fadeIn が来るまで全画面を覆い続ける。
			const auto ov = fx.overlay();
			if (ov.a > 0.0f)
			{
				screen.drawRect(sgc::Rectf{0.0f, 0.0f,
				                           static_cast<float>(screen.width()),
				                           static_cast<float>(screen.height())},
				                sgc::Colorf{ov.r, ov.g, ov.b, ov.a});
			}
		}

		Size layout(int outsideW, int outsideH) override
		{
			return {outsideW, outsideH};
		}

	private:
		Engine* m_engine;
		module::detail::DebugDrawTracker m_debugDraws;
	};

	ModuleAdapter adapter(this);
	run(adapter, configIn);
	unloadModule();
	return true;
}

// ── runModuleStatic (DLL を経ない静的リンク経路) ───────────────────────────
//
// web (wasm) と単一 exe 向け。ModuleHost (LoadLibrary) を使わず、リンク済みの
// mitiru_module_load を直接呼んで ModuleApi と GameMemory を受け取る。
// 以降の毎フレームの信号フローは runModule とまったく同じ adapter を使う。

MITIRU_INLINE bool mitiru::Engine::runModuleStatic(
	module::ModuleLoadFn loadFn, const EngineConfig& configIn)
{
	if (loadFn == nullptr) { return false; }
	if (m_moduleHost && m_moduleHost->isLoaded())
	{
		return false;   // DLL module が生きている間の併用は不可
	}

	m_moduleApi = module::ModuleApi{};
	m_moduleApi.version = module::kWireApiVersion;
	// 前に同じプロセスで登録された game の反射を引き継がない (MITIRU_GAME を使わない game は登録しない)
	module::linkedReflectionExports() = {};
	if (!guardModuleCode("mitiru_module_load", nullptr, "start refused",
	                     [&] { loadFn(&m_moduleApi, &m_moduleMemory); }))
	{
		m_moduleApi    = module::ModuleApi{};
		m_moduleMemory = nullptr;
		return false;
	}
	m_moduleMemorySize = m_moduleApi.memorySize;
	// GetProcAddress で引く export が無いので、MITIRU_GAME の登録が置いた 3 関数から組む。
	// 組まないと inspector / AI state / セーブ照合が「反射なし」として動く
	m_moduleReflection = module::ModuleReflection::fromExports(module::linkedReflectionExports());
	// 静的リンクには GetProcAddress で引く DLL export が無いので、MITIRU_PAUSE_ALWAYS_LAYERS
	// 宣言は届かない (2-1)。mask=0 = pause は全 layer 共通のまま。
	m_pauseAlwaysLayersMask = 0;

	// 静的リンクでは game と engine が同じビルドなので、version の不一致は
	// ModuleApi.hpp の取り違え (include パスの混線) 以外では起きない。
	// 起きたらそのまま進めずに止める。混線したまま動くと ABI がずれておかしくなる。
	if (m_moduleApi.version != module::kWireApiVersion)
	{
		console::noticef("ゲームと engine の ABI の版が合わないので始められません (game=%u、engine=%u)。"
			"game と engine を同じ版のヘッダーでビルドし直してください。",
			m_moduleApi.version, module::kWireApiVersion);
		m_moduleApi = module::ModuleApi{};
		m_moduleMemory = nullptr;
		return false;
	}
	bindModuleSideState();

	// 毎フレームの signal バッファ。DLL 経路では loadModule が確保する。ここで
	// 確保しないと buildModuleInputSnapshot が何も知らせずに何もせず、on_update が一度も
	// 呼ばれない (絵は初回の draw のまま止まる。web で実際に起きた)。
	if (!m_moduleInputSnapshot) { m_moduleInputSnapshot = std::make_unique<module::InputSnapshot>(); }
	if (!m_moduleFrameIntents)  { m_moduleFrameIntents  = std::make_unique<module::FrameIntents>(); }

#ifdef __EMSCRIPTEN__
	// wasm には音を繋ぐ host がいない。ここで繋がないと、ゲームが出す SoundIntent は
	// 行き先が無いまま捨てられ、AudioContext すら作られない。
	if (!m_audioEngine && !configIn.audioDir.empty())
	{
		setAudioEngine(std::make_shared<audio::WebAudioEngine>(configIn.audioDir));
	}
#endif

	clearModuleFault();
	module::setFaultDumpDirectory(debug::crashDirectory());
	if (m_moduleApi.on_init != nullptr)
	{
		guardModuleCallback("on_init", [&] { m_moduleApi.on_init(m_moduleMemory); });
	}

	// runModule と同一の adapter (このファイル上部で定義しているものはローカル型
	// なので、同じ形をここにも置く。挙動は runModule 側と一字一句同じにすること)。
	class StaticAdapter : public Game
	{
	public:
		explicit StaticAdapter(Engine* engine) noexcept : m_engine(engine) {}

		void update(float dt) override
		{
			MITIRU_ZONE_NAMED("Engine::StaticAdapter::update");
			m_engine->ensureModuleBindings();
			if (m_engine->moduleFaulted()) { return; }  // ModuleAdapter と同じ
			debug::crashContext().frame.store(m_engine->frameNumber(), std::memory_order_relaxed);
			if (m_engine->applyScrubHold())
			{
				m_engine->publishToolSnapshot();   // ModuleAdapter と同じ
				return;
			}
			m_engine->buildModuleInputSnapshot(dt);
			m_engine->applyResimInputOverride();
			if (!m_engine->runModuleFrameBody()) { return; }
			// デバッグ描画 intent の取り込み (v30、§9-1)。ModuleAdapter と同じ扱い。
			if (m_engine->m_moduleFrameIntents) { m_debugDraws.ingest(*m_engine->m_moduleFrameIntents, dt); }
		}

		void draw(Screen& screen) override
		{
			MITIRU_ZONE_NAMED("Engine::StaticAdapter::draw");
			const auto& api = m_engine->moduleApi();
			if (api.on_draw_commands != nullptr)
			{
				module::DrawContext ctx{};
				if (m_engine->m_moduleInputSnapshot)
				{
					ctx.logicalW = m_engine->m_moduleInputSnapshot->logicalW;
					ctx.logicalH = m_engine->m_moduleInputSnapshot->logicalH;
				}
				ctx.net = screen.netView();
				ctx.netMode = screen.netModeView();
				static thread_local module::DrawCommandBuffer buf;
				buf.count = 0;
				buf.droppedCount = 0;
				// textPool/pointPool も毎フレーム reset する (count と同じく per-frame pool。
				// 未 reset だと used が単調増加し続け、いずれ枯渇して以後の文字列/頂点コマンドが
				// 全て drop される。beko_run の drawStone (id 文字列を毎タイル push) で実際に
				// 19 フレーム目前後から drop が発生することを確認した)。
				buf.textPoolUsed  = 0;
				buf.pointPoolUsed = 0;
				if (!m_engine->callModuleDrawCommands(&ctx, &buf)) { return; }
				auto& sceneObjs = module::detail::lastSceneViewObjects();
				sceneObjs.clear();
				module::detail::lastSceneFieldMappings().clear();
				module::detail::drainDrawCommands(screen, buf, m_engine->m_spriteCache, &sceneObjs);
			}
			else if (api.on_draw != nullptr)
			{
				if (!m_engine->callModuleDraw(&screen)) { return; }
			}
			module::detail::drawDebugDraws(screen, m_debugDraws);
			m_engine->drawCandidateBranches(screen);  // ADR 0035 O4 (runModuleStatic 経路も同様に配線)
		}

		Size layout(int outsideW, int outsideH) override
		{
			return {outsideW, outsideH};
		}

	private:
		Engine* m_engine;
		module::detail::DebugDrawTracker m_debugDraws;
	};

	StaticAdapter adapter(this);
	run(adapter, configIn);

	if (m_moduleApi.on_shutdown != nullptr && m_moduleMemory != nullptr)
	{
		guardModuleCallback("on_shutdown", [&] { m_moduleApi.on_shutdown(m_moduleMemory); });
	}
	m_moduleApi = module::ModuleApi{};
	m_moduleMemory = nullptr;
	m_moduleMemorySize = 0;
	return true;
}

// ── Per-frame signal flow helper 群 (private; ModuleAdapter が呼ぶ) ────────
// 以下は inline で追加する member fn。inline-friend 宣言の方が綺麗だが、
// Engine.hpp が既に素の private として公開しており、ModuleAdapter は
// `m_engine->m_*` access 経由で暗黙に friend 扱いになる。
// 明確さのため Engine.hpp の class body に private member fn として宣言する…
// が、その header を整然と保つため、ここでは adapter が呼ぶ free namespace 内に
// 定義する。より単純には、ModuleAdapter が runModule の本体内で定義され、
// 囲うスコープへ access できる local class となるため friend 扱いとなり、
// m_engine の private member を直接呼べる。
//
// 補足: C++ の local class は friend にしない限り囲う関数の `this` の private
// member へ access できない。そこで helper を PRIVATE member function として
// 公開し、下記マークで friend にする。最も綺麗なのは Engine の private member に
// して ModuleAdapter から `m_engine->fooBar()` で呼ぶ方法。そのためには
// Engine.hpp の private section に宣言する必要がある。
//
// ここで採った実装方針: helper は Engine* を取り、public accessor + buffer 用に
// 新たに公開した少数の accessor (Engine.hpp に追加) 経由で access する file-scope
// の free function とする。

namespace mitiru::module::detail
{
// (ここに helper は不要。Engine が自前の member fn を持つ)
}  // namespace mitiru::module::detail

// ── Engine member helper の定義 (ModuleAdapter から呼ばれる) ──────────────

MITIRU_INLINE void mitiru::Engine::ensureModuleBindings()
{
	// Inspector / perf / mixer のツール窓が読む SharedSnapshot と、hud.set の値の写し。
	// 観測と replay-as-test の比較がどちらも最初のフレームから読めるよう、module の最初の tick で作る。
	if (!m_moduleInspectorSnapshot)
	{
		m_moduleInspectorSnapshot = std::make_unique<observe::SharedSnapshot>();
	}
	if (!m_moduleStateStore)
	{
		m_moduleStateStore = std::make_unique<bridge::StateStore>();
	}
}

MITIRU_INLINE void mitiru::Engine::buildModuleInputSnapshot(float dt)
{
	MITIRU_ZONE_NAMED("Engine::buildModuleInputSnapshot");
	auto* snap = m_moduleInputSnapshot.get();
	if (snap == nullptr) { return; }

	// ── 実効 dt / pause (ABI v21、H-3) ─────────────────────────────────
	// pause / hitStop の dt gating は simulation 入力なので snapshot に載せる。
	// 末尾の moduleInputOverride (replay) / applyResimInputOverride (resim) が
	// snapshot 全体を記録値で置換するため、再生時は記録された実効 dt が再投入される。
	// paused 中 stepFrames>0 なら 1 フレームだけ通常 dt で進める (従来意味論のまま)。
	{
		float        effectiveDt = dt;
		std::uint8_t paused      = m_config.paused ? m_config.pauseKind : std::uint8_t{0};
		if (m_config.paused)
		{
			if (m_config.stepFrames > 0) { --m_config.stepFrames; }
			else                         { effectiveDt = 0.0f; }
		}
		// HitStop (kind=5): 残量がある間 module へ渡す dt を 0 にする (update は
		// 呼び続ける)。intent は決定論的な module 出力なので replay でも同じ
		// フレームで発火する。fade/shake もここで実時間 (固定ステップ) で進める。
		// 演出は engine 側状態であり GameMemory には入れない (観測対象外)。
		const bool hitStop = m_moduleVisualFx.hitStopActive();
		if (hitStop) { effectiveDt = 0.0f; }
		m_moduleVisualFx.setComfortScales(m_config.shakeScale, m_config.rumbleScale);
		// rumble: 強さは host 側演出状態から派生する出力なので GameMemory にも録画にも入れない。
		// hitStop と同じく advance の前に読む (dt 以下の短い振動でも 1 回は送る)。
		const bool rumbleOn = m_moduleVisualFx.rumbleActive();
		const auto [rumbleLow, rumbleHigh] = m_moduleVisualFx.currentRumble();
		m_moduleVisualFx.advance(dt);
		// 毎フレーム送り直すので、持続時間は host が止まった時に回り続けない長さでよい
		constexpr std::uint32_t kRumbleRefreshMs = 250;
		if (rumbleOn)
		{
			m_gamepads.rumbleAll(rumbleLow, rumbleHigh, kRumbleRefreshMs);
			m_rumbleSent = true;
		}
		else if (m_rumbleSent)
		{
			m_gamepads.rumbleAll(0.0f, 0.0f, 0);
			m_rumbleSent = false;
		}
		snap->effectiveDt = effectiveDt;
		snap->paused      = paused;
		m_playtimeSec += static_cast<double>(effectiveDt);
		// D2: fadeOut/fadeIn の覆い alpha をそのまま供給する (Input::fadeProgress01 の説明参照)。
		snap->fadeProgress01 = m_moduleVisualFx.overlay().a;

		// ── layer 別 dt (v30、§1-2、2-1) ─────────────────────────────────
		// timeScale (グローバル) は「1 フレームで回すステップ数」を増減させる別軸
		// (FixedStepPlan.hpp) であり dt そのものを乗じる値ではないため、ここで
		// もう一度掛けると二重適用になる。layerTimeScale は素の dt にだけ掛ける。
		// pause の gating は既定で全 layer 共通だが、game が `MITIRU_PAUSE_ALWAYS_LAYERS`
		// で宣言した layer は pause 中でも dt を受け取る (Godot PROCESS_MODE_WHEN_PAUSED
		// 相当、ポーズメニュー演出用)。hitStop は layerFrozenByHitStop で層別に適用する
		// (mask の影響を受けない別軸)。実配列は Engine を持ち込まず単体テストできるよう
		// module::detail::computeLayerDt に切り出してある。
		const bool pausedGate = m_config.paused && effectiveDt == 0.0f;
		module::detail::computeLayerDt(dt, pausedGate, hitStop,
			m_config.layerTimeScale, m_config.layerFrozenByHitStop,
			module::detail::pauseLayersMaskFor(paused, m_pauseLayersByKind), snap->dtByLayer);
	}

	// ── 論理解像度 (ABI v21、§8-5) ──────────────────────────────────────
	// Screen の logical size を毎フレーム供給。game は kScreenW/kScreenH の自前
	// constexpr を持たなくてよい。replay 時は記録値が再投入される (resize も記録系の内側)。
	{
		const int w = (m_screen != nullptr) ? m_screen->width()  : 0;
		const int h = (m_screen != nullptr) ? m_screen->height() : 0;
		snap->logicalW = static_cast<std::uint16_t>(std::clamp(w, 0, 65535));
		snap->logicalH = static_cast<std::uint16_t>(std::clamp(h, 0, 65535));
	}

	// 前フレームの hud.save / hud.load の結果。値は次の結果まで snapshot に残る (永続バッファ)。
	if (m_pendingSaveResult != 0) { snap->lastSaveResult = m_pendingSaveResult; m_pendingSaveResult = 0; }
	if (m_pendingLoadResult != 0) { snap->lastLoadResult = m_pendingLoadResult; m_pendingLoadResult = 0; }

	// 決定論 seed を供給。replay 時は末尾の moduleInputOverride が
	// snapshot 全体を記録値で置換するので、ここで入れた値は再生時に記録 seed に戻る。
	snap->rngSeed = m_config.randomSeed;

	// 音声クロック (ABI v13)。host の audio backend の再生サンプル位置を供給。replay 時は
	// 末尾の moduleInputOverride が記録値で上書きするので bit-exact 性は保たれる。
	// audio master clock (ABI v13)。契約 (R-03, oscar-rythm): 0 = backend 未準備
	// (game は dt 積算へフォールバックする)。非ゼロになった後は **単調非減少** を
	// engine が保証する。backend がチャンク供給の谷で一瞬小さい値を返しても、
	// game の同期ロジック (snap/lerp) が拍を巻き戻さないようにここで clamp する。
	{
		const double raw = (m_audioEngine != nullptr) ? m_audioEngine->masterTimeSec() : 0.0;
		if (raw > m_lastAudioTimeSec) { m_lastAudioTimeSec = raw; }
		snap->audioTimeSec = m_lastAudioTimeSec;
	}

	// 音声出力レイテンシ (ABI v19)。device 固定値なので毎フレーム同じ。判定を耳基準へ
	// 補正したいリズムゲームが earTime = audioTimeSec - audioLatencySec で使う。0 = 不明。
	// replay 時は moduleInputOverride が記録値で上書きするので再現する。
	snap->audioLatencySec = (m_audioEngine != nullptr) ? m_audioEngine->outputLatencySec() : 0.0;

	// Keys (256 VK codes)。internal を覗かず InputState API を使う。
	// InputState の engine refactor の自由度を保つため。
	for (int vk = 0; vk < 256; ++vk)
	{
		const auto key = static_cast<KeyCode>(vk);
		snap->keysDown[vk]         = m_inputState.isKeyDown(key)         ? 1u : 0u;
		snap->keysJustPressed[vk]  = m_inputState.isKeyJustPressed(key)  ? 1u : 0u;
		snap->keysJustReleased[vk] = m_inputState.isKeyJustReleased(key) ? 1u : 0u;
	}

	detail::fillSnapshotMouse(m_inputState, *snap);

	// テキスト入力 (ABI v34、J5)。本命は UI の入力欄なので Win32 以外は常に空。
	// snapshot は永続バッファなので毎フレーム全 byte 上書き (前フレームの残りを残さない)。
	{
		// 物理問い合わせ job (v37): 前フレームの要求に答える。replay 中は末尾の snapshot 置換で
		// 記録値が上書きされるので、host の物理 world は再生に要らない。
		snap->physicsResultCount = detail::answerPhysicsQueries(m_modulePhysics.get(),
			m_pendingPhysicsQueries.data(), static_cast<int>(m_pendingPhysicsQueries.size()),
			snap->physicsResults, static_cast<int>(sizeof(snap->physicsResults) / sizeof(snap->physicsResults[0])));
		m_pendingPhysicsQueries.clear();

		snap->textInputLen = 0;
		snap->textInput[0] = '\0';
		detail::fillSnapshotIme(platform::ImeCompositionUtf8{}, *snap);
#ifdef _WIN32
		if (auto* win32 = dynamic_cast<mitiru::Win32Window*>(m_window.get()))
		{
			const std::string text = win32->consumeTextInput();
			const std::size_t cap = sizeof(snap->textInput) - 1;  // null 終端分
			const std::size_t n   = std::min(text.size(), cap);
			std::memcpy(snap->textInput, text.data(), n);
			snap->textInput[n]  = '\0';
			snap->textInputLen  = static_cast<std::uint8_t>(n);
			detail::fillSnapshotIme(win32->imeComposition(), *snap);
		}
#endif
	}

	// Gamepad: 枠ごとの 4 台と 1 人用の合成。snapshot は永続バッファなので毎フレーム全 field を書く。
	detail::fillSnapshotGamepads(m_gamepads, *snap);
	// v48: 設定の変化・言語・スロットの一覧と削除の結果・曲の拍 (Engine_Module_Boundary.hpp)
	fillModuleSnapshotV48(*snap);
	// v50: 先読みの進み (Engine_Module_Boundary50.hpp)。今の機器と人ごとの操作は割り当ての後に finishModuleSnapshotV50 が書く
	fillModuleSnapshotV50(*snap);

	// queue 済み action event (UI の操作と host の出来事) を POD buffer へ drain する。
	// wire 上限 (name 64B / payload 256B) を超える event は **切り詰めず破棄** する。
	// 半端に切れた JSON を game に渡すと、parse 失敗が game 側の原因不明のバグに見えるため
	// (warnOnce で通知、R-01)。
	snap->actionEventCount = 0;
	if (m_moduleActionEvents)
	{
		std::lock_guard lock(m_moduleActionEvents->mu);
		const std::size_t slotCap =
			sizeof(snap->actionEvents) / sizeof(snap->actionEvents[0]);
		std::size_t  taken   = 0;
		std::int32_t emitted = 0;
		for (; taken < m_moduleActionEvents->events.size()
		       && emitted < static_cast<std::int32_t>(slotCap); ++taken)
		{
			const auto& [name, payloadJson] = m_moduleActionEvents->events[taken];
			if (name.size() >= sizeof(snap->actionEvents[0].name)
			    || payloadJson.size() >= sizeof(snap->actionEvents[0].payloadJson))
			{
				mitiru::debug::warnOnce("action.event.oversize",
					"UI の操作 '" + name.substr(0, 32) + "' は名前か中身が長すぎる (名前 64 byte、中身 256 byte まで) "
					"ので捨てました。中身を小さくするか、何件かに分けてください。");
				continue;
			}
			module::detail::copyBounded(snap->actionEvents[emitted].name, name);
			module::detail::copyBounded(snap->actionEvents[emitted].payloadJson, payloadJson);
			++emitted;
		}
		snap->actionEventCount = emitted;
		// 消費 (破棄含む) した event を取り除く。溢れた分は次フレームに残す。
		if (taken > 0)
		{
			m_moduleActionEvents->events.erase(
				m_moduleActionEvents->events.begin(),
				m_moduleActionEvents->events.begin() + static_cast<std::ptrdiff_t>(taken));
		}
		// D11: this をキーにした file-local map で連続持ち越しフレーム数を数える
		// (Engine.hpp にメンバを足さずに済ませるため)。
		static std::unordered_map<const void*, int> carryStreak;
		int& streak = carryStreak[this];
		if (!m_moduleActionEvents->events.empty())
		{
			if (++streak >= 3)
			{
				mitiru::debug::warnOnce("action.event.carryover",
					"UI の操作が 1 フレームに入りきらず、3 フレーム続けて次へ持ち越しています。"
					"操作を送る回数を減らすか、1 件の中身にまとめてください。");
			}
		}
		else
		{
			streak = 0;
		}
	}

	// 利用者のキー割り当て。録画と replay が見るのは組み替えた後の入力なので、割り当てを変えても
	// 再生は同じ結果になる (replay の上書きはこの後)。
	if (m_config.moduleInputRemap) { m_config.moduleInputRemap(*snap); }
	finishModuleSnapshotV50(*snap);

	// Replay inject hook (axis 4): headless な `mitiru replay --test` は live 構築
	// した snapshot を記録済み byte で上書きし、on_update が記録通りの input stream を
	// 再実行できるようにする (DLL は input に関して stateless なので、これで
	// run を bit-exact に再現する)。
	if (m_config.moduleInputOverride) { m_config.moduleInputOverride(*snap); }

	// UI (RmlUi) も game と同じ snapshot のマウスで動かす。台本と replay の操作が UI にも届く。
	feedUiInput(*snap);
}

MITIRU_INLINE void mitiru::Engine::zeroModuleFrameIntents()
{
	if (m_moduleFrameIntents)
	{
		m_moduleFrameIntents->reset();
	}
}

// restart intent (§8-4): GameMemory を unload せず初期状態から fresh 再構築する。
// ring 記録前に適用するので ring frame N = 再構築後 bytes = 次フレーム memory_in が
// 保たれる (単一 timeline のまま。ring は破棄しない。restart 前への rewind も正当)。
// memset 0 で padding byte まで決定論化し、on_init (MITIRU_GAME の gameInit) が
// static な既定値イメージを memcpy して NSDMI 既定値へ戻す。
MITIRU_INLINE void mitiru::Engine::applyModuleRestartIntent()
{
	auto* intents = m_moduleFrameIntents.get();
	if (intents == nullptr || intents->restartRequest == 0) { return; }
	if (m_moduleMemory == nullptr || m_moduleMemorySize == 0 || m_moduleApi.on_init == nullptr)
	{
		mitiru::debug::warnOnce("restart.unavailable",
			"GameMemory の大きさ (ModuleApi::memorySize) か on_init がないので、hud.requestRestart() を無視しました。"
			"MITIRU_GAME が memorySize を設定しているか確かめ、gameInit を書いてください。");
		return;
	}
	std::memset(m_moduleMemory, 0, m_moduleMemorySize);
	if (!guardModuleCallback("on_init", [&] { m_moduleApi.on_init(m_moduleMemory); })) { return; }

	// MITIRU_REACHABLE の s_elapsed/s_reached は GameMemory とは別の DLL プロセス static
	// state のため、上の memset+on_init では戻らない。restart のたびに host からまとめて
	// 0 に戻す (未宣言 game は export 自体が無いので nullptr のまま何もしない)。
	if (m_moduleHost)
	{
		if (auto resetFn = m_moduleHost->invariantsResetFn())
		{
			guardModuleCallback("invariants reset", [&] { resetFn(); });
		}
	}
}

MITIRU_INLINE void mitiru::Engine::drainModuleFrameIntents()
{
	auto* intents = m_moduleFrameIntents.get();
	if (intents == nullptr) { return; }

	// 上限到達の検知 (R-01 級・初回のみ)。DLL 側 helper は満杯時に何も知らせずに drop するため、
	// count == 容量 を「超過分が落ちた可能性あり」として一度だけ知らせる。
	// int 比較 3 つだけなので hot path への影響は無視できる (warnOnce は到達時のみ呼ぶ)。
	{
		constexpr std::int32_t kPushCap = static_cast<std::int32_t>(
			sizeof(intents->statePushes) / sizeof(intents->statePushes[0]));
		constexpr std::int32_t kWatchCap = static_cast<std::int32_t>(
			sizeof(intents->exportedInspectables) / sizeof(intents->exportedInspectables[0]));
		constexpr std::int32_t kSoundCap = static_cast<std::int32_t>(
			sizeof(intents->soundIntents) / sizeof(intents->soundIntents[0]));
		if (intents->statePushCount >= kPushCap)
		{
			mitiru::debug::warnOnce("intents.statePush.cap",
				"HUD の更新が 1 フレームの上限 " + std::to_string(kPushCap)
				+ " 件に届いたので、超えた分は反映されません。1 フレームに送る数を減らしてください。");
		}
		if (intents->exportedInspectableCount >= kWatchCap)
		{
			mitiru::debug::warnOnce("intents.watch.cap",
				"watch が 1 フレームの上限 " + std::to_string(kWatchCap)
				+ " 件に届いたので、超えた分は表示されません。watch する値を減らしてください。");
		}
		if (intents->soundIntentCount >= kSoundCap)
		{
			mitiru::debug::warnOnce("intents.sound.cap",
				"音を鳴らす要求が 1 フレームの上限 " + std::to_string(kSoundCap)
				+ " 件に届いたので、超えた分は鳴りません。1 フレームに鳴らす数を減らしてください。");
		}
	}

	// 単純な flag 群
	if (intents->requestStop) { requestStop(); }

	// Screenshot。host が capture + filename を処理する。
	if (intents->requestScreenshot)
	{
		if (m_screen != nullptr)
		{
			const int w = m_screen->width();
			const int h = m_screen->height();
			if (w > 0 && h > 0)
			{
				auto pixels = capture();
				if (!pixels.empty())
				{
					(void)mitiru::render::saveTimestampedFrameToPng(
						pixels.data(), w, h, "screenshots", "module_game");
				}
			}
		}
	}

	// セーブ/ロード intent (v17)。中身は GameMemory bytes の memcpy で、置き場は config.saveDir の
	// セーブスロット (Engine_Save.hpp)。load は rewind と同じ機構で GameMemory へ写し、ring を捨てる。
	if (intents->saveRequest != 0)
	{
		// D1: 結果 (1=成功 / 2=失敗) は次の buildModuleInputSnapshot が InputSnapshot::lastSaveResult へ写し、
		// 次フレームの Input::saveSucceeded() が読む。ここで snapshot へ直に書くと、この後の録画が
		// 「このフレームのゲームが見た入力」として結果入りの snapshot を残し、再生でセーブのフレームがずれる。
		bool saveOk = false;
		const std::string slot = module::save::sanitizeSlot(intents->saveSlot);
		if (slot.empty())
		{
			mitiru::debug::warnOnce("save.slot.empty",
				"hud.save のスロット名 \"" + std::string(intents->saveSlot)
				+ "\" は使えないので、セーブしませんでした。使える文字は a-z、A-Z、0-9、_、- だけです。");
		}
		else
		{
			saveOk = saveModuleMemory(slot, std::string_view{intents->saveChapter,
				module::detail::boundedLen(intents->saveChapter)});
		}
		m_pendingSaveResult = saveOk ? 1u : 2u;
	}
	if (intents->loadRequest != 0)
	{
		bool loadOk = false;
		const std::string slot = module::save::sanitizeSlot(intents->loadSlot);
		if (slot.empty())
		{
			mitiru::debug::warnOnce("load.slot.empty",
				"hud.load のスロット名 \"" + std::string(intents->loadSlot)
				+ "\" は使えないので、ロードしませんでした。使える文字は a-z、A-Z、0-9、_、- だけです。");
		}
		else
		{
			// replay 代用フック: override が true を返したら記録済み state blob を適用済みなので
			// ファイルは読まない。セーブが録画後に上書きされていても bit-exact が構造上保証される。
			const bool substituted = m_saveLoadOverride && m_saveLoadOverride(slot.c_str());
			const bool applied = substituted || loadModuleMemory(slot);
			// 適用成功時は rewind ring を破棄する。load 前の履歴は別時間軸の bytes で、
			// そこへ rewind すると復元がおかしくなる (reloadModule の ring clear と同じ理由)。
			if (applied) { m_moduleMemoryRing.clear(); m_sideStateRing.clear(); }
			loadOk = applied;
		}
		// D1: lastSaveResult と同じく、次フレームの Input::loadSucceeded() へ渡す。
		m_pendingLoadResult = loadOk ? 1u : 2u;
	}

	// Tool window spawn 要求。DLL → host → 別 exe を spawn する。
	// game は Engine* を持てないので「このツール窓を開いて」と intent で頼み、
	// host が mitiru_<tool>.exe を別窓で起動する (必要なときだけ・pulled UI)。exe が
	// 見つからなければ無害に no-op。inspector へは host 自身の pid を渡し、game が
	// exportedInspectables に出した state をそのまま観測させる (SharedSnapshot 経由)。
	if (!m_suppressToolWindows && intents->toolRequestCount > 0)
	{
		const std::int32_t n = std::min<std::int32_t>(
			intents->toolRequestCount,
			static_cast<std::int32_t>(sizeof(intents->toolRequests) /
			                          sizeof(intents->toolRequests[0])));
		for (std::int32_t i = 0; i < n; ++i)
		{
			const auto& req = intents->toolRequests[i];
			if (req.tool[0] == '\0') { continue; }
			// 重複 spawn 防止: hud.open(Tool::X) を毎フレーム update で呼んでも窓は 1 回だけ
			// 開く。これで「どこに置くか」を気にせず、開きたい所で呼べる (pulled UI のまま)。
			std::string key = std::string{req.tool} + '|' + std::string{req.args};
			if (!m_spawnedToolKeys.insert(std::move(key)).second) { continue; }
			// setToolWindowPos が指定されていれば spawn 引数へ --window-pos を足す (録画で実画面に出さない)。
			std::string spawnArgs{req.args};
			if (m_toolWinX != (-2147483647 - 1))
			{
				spawnArgs += " --window-pos " + std::to_string(m_toolWinX) + " " + std::to_string(m_toolWinY);
			}
			(void)mitiru::debug::spawnTool(std::string{req.tool}, 0, spawnArgs);
		}
	}

	// カーソルロック (v23)。毎フレーム宣言。platform 層 (applyCursorCapture) が
	// 遷移を検出して OS へ適用する。自動実行では m_allowCursorCapture=false で無効。
	m_inputState.setCursorCaptured(m_allowCursorCapture && intents->wantMouseLock != 0);

	// テキスト入力 (v45、ADR 0048 段階 1)。毎フレーム宣言。立てないフレームは IME を切る。
	// UI (RmlUi) の入力欄が選ばれている間も IME を戻し、変換窓をその欄へ置く。
#ifdef _WIN32
	if (auto* win32 = dynamic_cast<mitiru::Win32Window*>(m_window.get()); win32 != nullptr && m_screen)
	{
		float field[4] = {};
		const bool uiWantsText = m_rmlUi.focusedTextField(field);
		if (!uiWantsText) { std::copy_n(intents->textInputRect, 4, field); }
		const detail::ClientRect r = detail::textInputRectToClient(field,
			static_cast<float>(m_screen->width()), static_cast<float>(m_screen->height()),
			static_cast<float>(m_window->width()), static_cast<float>(m_window->height()));
		win32->setTextInputArea(uiWantsText || intents->textInputActive != 0, RECT{r.left, r.top, r.right, r.bottom});
	}
#endif

	// State push。DLL → host → UI (RmlUi) の data model と、観測用の状態の写し。
	if (m_rmlUi.active()) { detail::pushStateToRmlUi(m_rmlUi, *intents); }
	if (m_moduleStateStore && intents->statePushCount > 0)
	{
		const std::int32_t n = std::min<std::int32_t>(
			intents->statePushCount,
			static_cast<std::int32_t>(sizeof(intents->statePushes) /
			                          sizeof(intents->statePushes[0])));
		// key / strVal は bounded 読み。DLL からの wire buffer は null 終端を信頼しない
		// (exportedInspectables の boundedLen と同基準の境界防御)。
		for (std::int32_t i = 0; i < n; ++i)
		{
			const auto& item = intents->statePushes[i];
			const std::string_view key{item.key, module::detail::boundedLen(item.key)};
			switch (item.kind)
			{
			case 1: m_moduleStateStore->set(key, item.intVal); break;
			case 2: m_moduleStateStore->set(key, item.floatVal); break;
			case 3: m_moduleStateStore->set(key, static_cast<bool>(item.intVal)); break;
			case 4: m_moduleStateStore->set(key,
				std::string{item.strVal, module::detail::boundedLen(item.strVal)}); break;
			default: break;  // kind=0 (null) は今は意図的に no-op
			}
		}
	}

	// Exported inspectable。DLL が毎フレーム埋める。engine が SharedSnapshot へ書き、
	// inspector sub-window が DLL 側 state を拾えるようにする。ツール窓が読んでいない間は組み立てない。
	if (intents->exportedInspectableCount > 0 && m_moduleInspectorSnapshot && m_moduleInspectorSnapshot->hasReader())
	{
		const std::int32_t n = std::min<std::int32_t>(
			intents->exportedInspectableCount,
			static_cast<std::int32_t>(sizeof(intents->exportedInspectables) /
			                          sizeof(intents->exportedInspectables[0])));

		// 変化検知: export 内容 (name + json) を FNV-1a でハッシュ化し、前回と同一なら
		// parse+rebuild+disk-write を丸ごと省く。inspector は同じ内容を読み続けるので
		// skip しても観測結果は変わらず、毎フレームの temp-file 書き込みを避けられる。
		std::uint64_t digest = 14695981039346656037ull;
		const auto fold = [&digest](const char* p, std::size_t len)
		{
			for (std::size_t k = 0; k < len; ++k)
			{
				digest ^= static_cast<unsigned char>(p[k]);
				digest *= 1099511628211ull;
			}
		};
		// jsonLen も game 申告値。buffer サイズへ clamp してから読む (境界防御)。
		const auto clampedJsonLen = [](const module::InspectableExport& e) noexcept
		{
			const auto cap = static_cast<std::int32_t>(sizeof(e.json));
			return (e.jsonLen < 0) ? 0 : (e.jsonLen > cap ? cap : e.jsonLen);
		};
		for (std::int32_t i = 0; i < n; ++i)
		{
			const auto& exp = intents->exportedInspectables[i];
			fold(exp.name, module::detail::boundedLen(exp.name));
			const std::int32_t jl = clampedJsonLen(exp);
			if (jl > 0) { fold(exp.json, static_cast<std::size_t>(jl)); }
		}

		if (digest != m_lastInspectorDigest)
		{
			m_lastInspectorDigest = digest;
			// mitiru_inspector.exe が期待する次の形の JSON map を構築する。
			//   { name: { title, state },... }
			nlohmann::json out = nlohmann::json::object();
			for (std::int32_t i = 0; i < n; ++i)
			{
				const auto& exp = intents->exportedInspectables[i];
				const std::string name{exp.name, module::detail::boundedLen(exp.name)};
				const std::string title{exp.title, module::detail::boundedLen(exp.title)};
				const std::int32_t jl = clampedJsonLen(exp);
				nlohmann::json state;
				try
				{
					state = jl > 0
						? nlohmann::json::parse(std::string{exp.json,
						                               static_cast<std::size_t>(jl)})
						: nlohmann::json::object();
				}
				catch (...)
				{
					state = nlohmann::json{{"error", "invalid inspectable JSON"}};
				}
				out[name] = nlohmann::json{{"title", title}, {"state", state}};
			}
			// 書き出しは publishToolSnapshot が host の節と併せて行う (game export 無しでも perf が動くように)。
			m_lastGameInspectables = std::move(out);
			m_inspectorDirty   = true;
		}
	}

	// perf / audio / 巻き戻しの節は常に変わるので、digest の門に乗せず ~10Hz で併記する。
	publishToolSnapshot();

	// Sound 再生要求。DLL → host → audio engine。game は mixer
	// pointer を持たず、sound 名を指定するだけ。audio engine 未設定時
	// (graceful degradation) や v3 module では無音 no-op となる
	// (soundIntentCount は 0 のまま。on_update 前に毎フレーム zero される)。
	// 聞き手とバス音量 (v45) はそのフレームの音より先に取り込む (同じフレームに鳴らす音にも反映する)。
	if (m_audioEngine) { m_soundIntentRouter.applyMix(*m_audioEngine, *intents); }
	if (intents->soundIntentCount > 0 && m_audioEngine)
	{
		const std::int32_t n = std::min<std::int32_t>(
			intents->soundIntentCount,
			static_cast<std::int32_t>(sizeof(intents->soundIntents) /
			                          sizeof(intents->soundIntents[0])));
		// category / stop / loop / volume の解釈は applySoundIntent に集約。
		// BGM の同 id 連打は router が冪等化する (毎フレーム hud.music("bgm") を許容)。
		for (std::int32_t i = 0; i < n; ++i)
		{
			const auto& si = intents->soundIntents[i];
			// F2: hud.playAt() は backend がサンプル精度予約に対応しない場合、
			// SoundIntentRouter (v19、scheduleSec>0 分岐) が即時再生へフォールバックする。
			// 知らせないままのズレは判定タイミングのバグに見えるため一度だけ知らせる。
			if (si.scheduleSec > 0.0 && !m_audioEngine->supportsScheduledPlayback())
			{
				mitiru::debug::warnOnce("hud.playAt.unsupported",
					"いまの音の出し方は時刻を決めた再生に対応していないので、hud.playAt() の音はすぐに鳴らします。"
					"タイミングを確かめるときは、Miniaudio か WebAudio で音を出す起動で試してください。");
			}
			if (!m_soundIntentRouter.apply(*m_audioEngine, si)) { continue; }  // dedupe skip
			// AI 観測ログ (/api/ai/audio): 適用済み intent をそのまま記録する。
			m_audioLog.push(frameNumber(), si.id, si.category, si.loop, si.stop,
			                si.volume, si.pitchScale);
		}
	}

	// 毎フレームの audio 定期掃除 (#51): 終了 SE voice 回収 + fade-out music 解放。
	// 再生有無に関わらず呼ぶ (静かな区間でも ended voice が滞留しないように)。
	if (m_audioEngine) { m_audioEngine->update(); }

	// v48: カメラの切り替え・パッドへの出力・スロットの削除と一覧・曲の強さ・残響の場所
	drainModuleIntentsV48(*intents);
	// v50: 先読み・カットシーンの印・asset.reloaded の後の資産の写し替え
	drainModuleIntentsV50(*intents);

	// 物理問い合わせ (v37): ここでは控えるだけで、答えは次の buildModuleInputSnapshot が書く
	// (同期呼び出しにしない = ADR 0005)。
	{
		const int cap = static_cast<int>(sizeof(intents->physicsQueries) / sizeof(intents->physicsQueries[0]));
		const int n   = std::min<int>(intents->physicsQueryCount, cap);
		m_pendingPhysicsQueries.assign(intents->physicsQueries, intents->physicsQueries + (n > 0 ? n : 0));
	}

	// VisualIntent (#33、v7): kind=1 (Tint) は Screen::pushTint へ、kind 2-6
	// (FadeOut/FadeIn/Shake/HitStop/Letterbox) は VisualIntentFx へ流す。kind=0 は no-op。
	// FX の適用 (覆い描画・shake transform・dt=0 gating) は ModuleAdapter が行う。
	if (intents->visualIntentCount > 0 && m_screen)
	{
		const std::int32_t n = std::min<std::int32_t>(
			intents->visualIntentCount,
			static_cast<std::int32_t>(sizeof(intents->visualIntents) /
			                          sizeof(intents->visualIntents[0])));
		for (std::int32_t i = 0; i < n; ++i)
		{
			const auto& vi = intents->visualIntents[i];
			if (vi.kind == mitiru::module::kVisualIntentTint)
			{
				if (vi.durSec > 0.0f)
				{
					m_screen->pushTint(sgc::Colorf{vi.r, vi.g, vi.b, vi.a}, vi.durSec);
				}
			}
			else
			{
				(void)m_moduleVisualFx.applyIntent(vi);  // kind 2-6 (それ以外は no-op)
			}
		}
	}
}
