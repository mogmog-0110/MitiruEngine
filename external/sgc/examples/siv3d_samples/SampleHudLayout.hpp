#pragma once

/// @file SampleHudLayout.hpp
/// @brief HUDレイアウトシステムのビジュアルデモ
///
/// sgc::ui::HudLayout と sgc::ui::Anchor を使い、
/// 9点アンカーの配置計算を視覚的に確認するサンプル。
/// - 9つのアンカー位置にラベル付き色付きボックスを配置
/// - NUM1/NUM2/NUM3でマージンプリセット切替（none, symmetric, uniform）
/// - Rキーでデフォルトに戻す
/// - デモ領域内にアンカーグリッド線とドットを描画

#include <array>
#include <cstdint>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/ui/Anchor.hpp"
#include "sgc/ui/HudLayout.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief HUDレイアウトサンプルシーン
///
/// 9点アンカーの配置を視覚的に確認し、
/// マージンプリセットを切り替えてレイアウトの変化を観察する。
class SampleHudLayout : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_presetIndex = 0;
		rebuildLayout();
	}

	/// @brief 更新処理
	void update(float /*dt*/) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// マージンプリセット切替
		bool changed = false;
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			m_presetIndex = 0;
			changed = true;
		}
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			m_presetIndex = 1;
			changed = true;
		}
		if (input->isKeyJustPressed(KeyCode::NUM3))
		{
			m_presetIndex = 2;
			changed = true;
		}
		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_presetIndex = 0;
			changed = true;
		}

		if (changed)
		{
			rebuildLayout();
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		r->clearBackground(sgc::Colorf{0.08f, 0.08f, 0.10f, 1.0f});

		// タイトル
		tr->drawTextCentered(
			"HudLayout Demo", 28.0f,
			sgc::Vec2f{sw * 0.5f, 20.0f},
			sgc::Colorf{0.3f, 0.6f, 1.0f, 1.0f});

		// 現在のプリセット名
		tr->drawTextCentered(
			std::string{"Margin: "} + presetName(), 16.0f,
			sgc::Vec2f{sw * 0.5f, 46.0f},
			sgc::Colorf{0.55f, 0.55f, 0.6f, 1.0f});

		// デモ領域の枠線
		const auto demoArea = demoAreaRect();
		const sgc::AABB2f demoAABB{
			{demoArea.x(), demoArea.y()},
			{demoArea.x() + demoArea.width(), demoArea.y() + demoArea.height()}};
		r->drawRectFrame(demoAABB, 2.0f,
			sgc::Colorf{0.3f, 0.3f, 0.4f, 0.8f});

		// マージン適用後の内側領域の枠線（破線風に点線で表示）
		if (m_presetIndex > 0)
		{
			const auto inner = sgc::ui::applyMargin(demoArea, currentMargin());
			const sgc::AABB2f innerAABB{
				{inner.x(), inner.y()},
				{inner.x() + inner.width(), inner.y() + inner.height()}};
			r->drawRectFrame(innerAABB, 1.0f,
				sgc::Colorf{0.5f, 0.8f, 0.3f, 0.5f});
		}

		// アンカーグリッド線（デモ領域内の3x3分割）
		drawGridLines(r, demoArea);

		// アンカーポイントのドット
		drawAnchorDots(r, demoArea);

		// 9つのHUD要素（ラベル付きボックス）
		drawHudElements(r, tr);

		// ガイドライン（アンカーポイント → ボックス中心）
		drawGuideLines(r, demoArea);

		// 操作ヒント
		tr->drawText(
			"[1] None  [2] Symmetric(20,10)  [3] Uniform(30)  [R] Reset  [Esc] Back",
			12.0f,
			sgc::Vec2f{10.0f, sh - 18.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

private:
	/// @brief HUD要素のボックスサイズ
	static constexpr sgc::Vec2f BOX_SIZE{110.0f, 32.0f};

	/// @brief デモ領域の上端Y（タイトルの下）
	static constexpr float DEMO_TOP = 65.0f;

	/// @brief デモ領域の下端Y（フッターの上）
	static constexpr float DEMO_BOTTOM_PAD = 30.0f;

	/// @brief デモ領域の左右パディング
	static constexpr float DEMO_SIDE_PAD = 10.0f;

	/// @brief アンカー情報
	struct AnchorInfo
	{
		const char* label;
		sgc::ui::Anchor anchor;
		sgc::Colorf color;
		std::uint64_t key;
	};

	/// @brief 全9アンカーの定義テーブル
	static constexpr std::array<AnchorInfo, 9> ANCHOR_TABLE{{
		{"TopLeft",      sgc::ui::Anchor::TopLeft,      {0.8f, 0.3f, 0.3f, 0.85f}, 1},
		{"TopCenter",    sgc::ui::Anchor::TopCenter,    {0.3f, 0.8f, 0.3f, 0.85f}, 2},
		{"TopRight",     sgc::ui::Anchor::TopRight,     {0.3f, 0.3f, 0.8f, 0.85f}, 3},
		{"CenterLeft",   sgc::ui::Anchor::CenterLeft,   {0.8f, 0.8f, 0.3f, 0.85f}, 4},
		{"Center",       sgc::ui::Anchor::Center,       {0.8f, 0.3f, 0.8f, 0.85f}, 5},
		{"CenterRight",  sgc::ui::Anchor::CenterRight,  {0.3f, 0.8f, 0.8f, 0.85f}, 6},
		{"BottomLeft",   sgc::ui::Anchor::BottomLeft,   {0.9f, 0.5f, 0.2f, 0.85f}, 7},
		{"BottomCenter", sgc::ui::Anchor::BottomCenter, {0.5f, 0.2f, 0.9f, 0.85f}, 8},
		{"BottomRight",  sgc::ui::Anchor::BottomRight,  {0.2f, 0.9f, 0.5f, 0.85f}, 9},
	}};

	sgc::ui::HudLayout m_hud;
	int m_presetIndex{0};

	/// @brief デモ領域の矩形を返す
	[[nodiscard]] sgc::Rectf demoAreaRect() const noexcept
	{
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;
		return sgc::Rectf{
			{DEMO_SIDE_PAD, DEMO_TOP},
			{sw - DEMO_SIDE_PAD * 2.0f, sh - DEMO_TOP - DEMO_BOTTOM_PAD}};
	}

	/// @brief 現在のマージンを返す
	[[nodiscard]] sgc::ui::Margin currentMargin() const noexcept
	{
		switch (m_presetIndex)
		{
		case 1:  return sgc::ui::Margin::symmetric(20.0f, 10.0f);
		case 2:  return sgc::ui::Margin::uniform(30.0f);
		default: return sgc::ui::Margin::zero();
		}
	}

	/// @brief 現在のプリセット名を返す
	[[nodiscard]] const char* presetName() const noexcept
	{
		switch (m_presetIndex)
		{
		case 1:  return "symmetric(20, 10)";
		case 2:  return "uniform(30)";
		default: return "none";
		}
	}

	/// @brief HUDレイアウトを現在のプリセットで再構築する
	///
	/// マージン適用済みの領域を recalculate() に渡すことで、
	/// 各要素がマージン内に収まるよう配置される。
	void rebuildLayout()
	{
		const auto demo = demoAreaRect();
		const auto inner = sgc::ui::applyMargin(demo, currentMargin());

		m_hud.clear();
		for (const auto& info : ANCHOR_TABLE)
		{
			// offset={0,0}、size=BOX_SIZE で登録
			// recalculate(inner)がalignedRectで各アンカー位置に配置する
			m_hud.add(info.key, sgc::ui::HudElement{
				info.anchor,
				{0.0f, 0.0f},
				BOX_SIZE,
				true
			});
		}

		// マージン適用済み領域で再計算
		m_hud.recalculate(inner);
	}

	/// @brief デモ領域内にアンカーグリッド線を描画する
	void drawGridLines(sgc::IRenderer* r, const sgc::Rectf& area) const
	{
		const sgc::Colorf lineColor{0.25f, 0.25f, 0.3f, 0.4f};
		const float cx = area.x() + area.width() * 0.5f;
		const float cy = area.y() + area.height() * 0.5f;
		const float l = area.x();
		const float rr = area.x() + area.width();
		const float t = area.y();
		const float b = area.y() + area.height();

		// 垂直中央線
		r->drawLine({cx, t}, {cx, b}, 1.0f, lineColor);
		// 水平中央線
		r->drawLine({l, cy}, {rr, cy}, 1.0f, lineColor);
	}

	/// @brief デモ領域内の各アンカーポイントにドットを描画する
	void drawAnchorDots(sgc::IRenderer* r, const sgc::Rectf& area) const
	{
		const sgc::Colorf dotColor{1.0f, 0.7f, 0.2f, 0.9f};

		for (const auto& info : ANCHOR_TABLE)
		{
			const auto pt = sgc::ui::anchorPoint(area, info.anchor);
			r->drawCircle(pt, 4.0f, dotColor);
		}
	}

	/// @brief 9つのHUD要素を描画する
	void drawHudElements(sgc::IRenderer* r, sgc::ITextRenderer* tr) const
	{
		for (const auto& info : ANCHOR_TABLE)
		{
			const auto bounds = m_hud.bounds(info.key);

			const sgc::AABB2f boxAABB{
				{bounds.x(), bounds.y()},
				{bounds.x() + bounds.width(), bounds.y() + bounds.height()}};
			r->drawRect(boxAABB, info.color);
			r->drawRectFrame(boxAABB, 1.5f,
				sgc::Colorf{0.93f, 0.93f, 0.93f, 1.0f});

			const sgc::Vec2f center{
				bounds.x() + bounds.width() * 0.5f,
				bounds.y() + bounds.height() * 0.5f};
			tr->drawTextCentered(
				info.label, 12.0f, center,
				sgc::Colorf{0.93f, 0.93f, 0.93f, 1.0f});
		}
	}

	/// @brief アンカーポイントからボックス中心へのガイドラインを描画する
	void drawGuideLines(sgc::IRenderer* r, const sgc::Rectf& area) const
	{
		const sgc::Colorf guideColor{0.5f, 0.5f, 0.6f, 0.3f};

		for (const auto& info : ANCHOR_TABLE)
		{
			const auto pt = sgc::ui::anchorPoint(area, info.anchor);
			const auto bounds = m_hud.bounds(info.key);
			const sgc::Vec2f boxCenter{
				bounds.x() + bounds.width() * 0.5f,
				bounds.y() + bounds.height() * 0.5f};

			const float dx = boxCenter.x - pt.x;
			const float dy = boxCenter.y - pt.y;
			if (dx * dx + dy * dy > 100.0f)
			{
				r->drawLine(pt, boxCenter, 1.0f, guideColor);
			}
		}
	}
};
