#pragma once

/// @file SampleNoise.hpp
/// @brief パーリンノイズ可視化デモシーン
///
/// グリッド状の矩形をノイズ値で着色して表示する。
/// 簡易インラインパーリンノイズ実装を使用（sgc::math::Noise未実装のため）。
/// - 1/2/3: オクターブ数を変更
/// - R: シード変更（リシード）
/// - SPACE: アニメーション（ノイズスクロール）トグル
/// - ESC: メニューに戻る

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief パーリンノイズ可視化サンプル
///
/// 画面をグリッドに分割し、各セルをノイズ値に応じた色で描画する。
/// オクターブ数やシードをリアルタイムに変更できる。
class SampleNoise : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_seed = 42;
		m_octaves = 4;
		m_animating = false;
		m_scrollOffset = 0.0f;
		initPermutation(m_seed);
	}

	/// @brief 更新処理
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		if (input->isKeyJustPressed(KeyCode::NUM1)) { m_octaves = 1; }
		if (input->isKeyJustPressed(KeyCode::NUM2)) { m_octaves = 4; }
		if (input->isKeyJustPressed(KeyCode::NUM3)) { m_octaves = 8; }

		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_seed = (m_seed * 1103515245 + 12345) & 0x7FFFFFFF;
			initPermutation(static_cast<std::uint32_t>(m_seed));
		}

		if (input->isKeyJustPressed(KeyCode::SPACE))
		{
			m_animating = !m_animating;
		}

		if (m_animating)
		{
			m_scrollOffset += dt * 0.5f;
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		renderer->clearBackground(sgc::Colorf{0.02f, 0.02f, 0.05f});

		// ── ノイズグリッド描画 ──
		const float gridTop = GRID_Y;
		const float gridW = sw - 2.0f * GRID_MARGIN;
		const float gridH = sh - gridTop - 80.0f;
		const float cellW = gridW / static_cast<float>(COLS);
		const float cellH = gridH / static_cast<float>(ROWS);

		for (int row = 0; row < ROWS; ++row)
		{
			for (int col = 0; col < COLS; ++col)
			{
				const float nx = static_cast<float>(col) * NOISE_SCALE + m_scrollOffset;
				const float ny = static_cast<float>(row) * NOISE_SCALE;

				const float val = octaveNoise(nx, ny, m_octaves, 0.5f);
				const float clamped = (val < 0.0f) ? 0.0f : ((val > 1.0f) ? 1.0f : val);

				// ノイズ値を色に変換（青→シアン→白）
				const sgc::Colorf cellColor = noiseToColor(clamped);

				const float px = GRID_MARGIN + static_cast<float>(col) * cellW;
				const float py = gridTop + static_cast<float>(row) * cellH;

				const sgc::AABB2f cell{
					{px, py},
					{px + cellW - 1.0f, py + cellH - 1.0f}};
				renderer->drawRect(cell, cellColor);
			}
		}

		// ── UI テキスト ──
		text->drawTextCentered(
			"Perlin Noise Visualization", 28.0f,
			sgc::Vec2f{sw * 0.5f, 25.0f}, sgc::Colorf{0.9f, 0.9f, 1.0f});

		const std::string info =
			"Octaves: " + std::to_string(m_octaves) +
			" | Seed: " + std::to_string(m_seed) +
			(m_animating ? " | Animating" : " | Paused");
		text->drawText(info, 14.0f,
			sgc::Vec2f{20.0f, 55.0f}, sgc::Colorf{0.7f, 0.7f, 0.8f});

		text->drawText(
			"1/2/3: Octaves | R: Reseed | Space: Animate | ESC: Back", 14.0f,
			sgc::Vec2f{20.0f, sh - 25.0f}, sgc::Colorf{0.5f, 0.5f, 0.6f});
	}

private:
	// ── レイアウト定数 ──
	static constexpr float GRID_MARGIN = 20.0f;
	static constexpr float GRID_Y = 80.0f;
	static constexpr int COLS = 80;
	static constexpr int ROWS = 50;
	static constexpr float NOISE_SCALE = 0.08f;

	// ── メンバ変数 ──
	int m_seed{42};
	int m_octaves{4};
	bool m_animating{false};
	float m_scrollOffset{0.0f};
	std::array<int, 512> m_perm{};

	// ── パーリンノイズ実装 ──

	/// @brief パーミュテーションテーブルを初期化する
	void initPermutation(std::uint32_t seed)
	{
		std::array<int, 256> p{};
		for (int i = 0; i < 256; ++i) { p[static_cast<std::size_t>(i)] = i; }

		// Fisher-Yates シャッフル
		for (int i = 255; i > 0; --i)
		{
			seed = seed * 1103515245u + 12345u;
			const int j = static_cast<int>((seed >> 16) % static_cast<std::uint32_t>(i + 1));
			const int tmp = p[static_cast<std::size_t>(i)];
			p[static_cast<std::size_t>(i)] = p[static_cast<std::size_t>(j)];
			p[static_cast<std::size_t>(j)] = tmp;
		}

		for (int i = 0; i < 256; ++i)
		{
			m_perm[static_cast<std::size_t>(i)] = p[static_cast<std::size_t>(i)];
			m_perm[static_cast<std::size_t>(i + 256)] = p[static_cast<std::size_t>(i)];
		}
	}

	/// @brief フェード関数
	[[nodiscard]] static float fade(float t) noexcept
	{
		return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
	}

	/// @brief 勾配関数
	[[nodiscard]] static float grad(int hash, float x, float y) noexcept
	{
		const int h = hash & 3;
		const float u = (h < 2) ? x : y;
		const float v = (h < 2) ? y : x;
		return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
	}

	/// @brief 線形補間
	[[nodiscard]] static float lerpF(float a, float b, float t) noexcept
	{
		return a + t * (b - a);
	}

	/// @brief 2Dパーリンノイズ（結果を[0,1]に正規化）
	[[nodiscard]] float noise2D(float x, float y) const noexcept
	{
		const int xi = static_cast<int>(std::floor(x)) & 255;
		const int yi = static_cast<int>(std::floor(y)) & 255;
		const float xf = x - std::floor(x);
		const float yf = y - std::floor(y);
		const float u = fade(xf);
		const float v = fade(yf);

		const int aa = m_perm[static_cast<std::size_t>(m_perm[static_cast<std::size_t>(xi)] + yi)];
		const int ab = m_perm[static_cast<std::size_t>(m_perm[static_cast<std::size_t>(xi)] + yi + 1)];
		const int ba = m_perm[static_cast<std::size_t>(m_perm[static_cast<std::size_t>(xi + 1)] + yi)];
		const int bb = m_perm[static_cast<std::size_t>(m_perm[static_cast<std::size_t>(xi + 1)] + yi + 1)];

		const float result = lerpF(
			lerpF(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u),
			lerpF(grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f), u),
			v);

		return (result + 1.0f) * 0.5f;
	}

	/// @brief オクターブノイズ
	[[nodiscard]] float octaveNoise(float x, float y, int octaves, float persistence) const noexcept
	{
		float total = 0.0f;
		float amplitude = 1.0f;
		float frequency = 1.0f;
		float maxValue = 0.0f;

		for (int i = 0; i < octaves; ++i)
		{
			total += noise2D(x * frequency, y * frequency) * amplitude;
			maxValue += amplitude;
			amplitude *= persistence;
			frequency *= 2.0f;
		}

		return (maxValue > 0.0f) ? (total / maxValue) : 0.0f;
	}

	/// @brief ノイズ値を色に変換する
	[[nodiscard]] static sgc::Colorf noiseToColor(float v) noexcept
	{
		// 低い値: 深い青、中間: シアン、高い値: 明るい白
		if (v < 0.4f)
		{
			const float t = v / 0.4f;
			return sgc::Colorf{0.0f, 0.0f, 0.15f + t * 0.4f};
		}
		if (v < 0.7f)
		{
			const float t = (v - 0.4f) / 0.3f;
			return sgc::Colorf{0.0f, t * 0.7f, 0.55f + t * 0.2f};
		}
		const float t = (v - 0.7f) / 0.3f;
		return sgc::Colorf{t * 0.8f, 0.7f + t * 0.3f, 0.75f + t * 0.25f};
	}
};
