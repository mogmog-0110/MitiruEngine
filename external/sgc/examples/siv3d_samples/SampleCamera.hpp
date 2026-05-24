#pragma once

/// @file SampleCamera.hpp
/// @brief 2Dカメラ（パン・ズーム・シェイク・追従）サンプル
///
/// Camerafを使った2Dカメラ操作を可視化する。
/// ワールド空間上のオブジェクトをカメラ変換でスクリーンに描画する。
/// - 矢印キー: カメラをパン
/// - 1/2キー: ズームイン/アウト
/// - Space: カメラシェイク
/// - Rキー: カメラリセット
/// - ESCキー: メニューに戻る

#include <cmath>
#include <cstddef>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/scene/Camera.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief 2Dカメラサンプルシーン
///
/// ワールド座標系に配置されたオブジェクトを、
/// Camerafのposition/zoom/shake/followを使って
/// スクリーン座標に変換して描画する。
class SampleCamera : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_camera = sgc::Cameraf{};
		m_camera.setPosition({0.0f, 0.0f, 0.0f});
		m_camera.setZoom(1.0f);
		m_followTarget = false;
		m_targetX = 0.0f;
		m_targetY = 0.0f;
		m_targetAngle = 0.0f;
	}

	/// @brief 毎フレームの更新処理
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		if (input->isKeyJustPressed(KeyCode::R))
		{
			onEnter();
			return;
		}

		// カメラパン（矢印キー）
		constexpr float panSpeed = 200.0f;
		if (input->isKeyDown(KeyCode::LEFT))
		{
			auto pos = m_camera.position();
			pos.x -= panSpeed * dt;
			m_camera.setPosition(pos);
		}
		if (input->isKeyDown(KeyCode::RIGHT))
		{
			auto pos = m_camera.position();
			pos.x += panSpeed * dt;
			m_camera.setPosition(pos);
		}
		if (input->isKeyDown(KeyCode::UP))
		{
			auto pos = m_camera.position();
			pos.y -= panSpeed * dt;
			m_camera.setPosition(pos);
		}
		if (input->isKeyDown(KeyCode::DOWN))
		{
			auto pos = m_camera.position();
			pos.y += panSpeed * dt;
			m_camera.setPosition(pos);
		}

		// ズーム（1/2キー）
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			const float z = m_camera.zoom();
			m_camera.setZoom(z * 0.8f);  // ズームイン(FOV狭く=拡大)
		}
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			const float z = m_camera.zoom();
			m_camera.setZoom(z * 1.25f);  // ズームアウト
		}

		// カメラシェイク（Space）
		if (input->isKeyJustPressed(KeyCode::SPACE))
		{
			m_camera.shake(8.0f, 0.5f);
		}

		// クリックで追従トグル
		if (input->isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT))
		{
			m_followTarget = !m_followTarget;
		}

		// ターゲット移動（円軌道）
		m_targetAngle += dt * 0.8f;
		m_targetX = std::cos(m_targetAngle) * 150.0f;
		m_targetY = std::sin(m_targetAngle) * 100.0f;

		// 追従モード
		if (m_followTarget)
		{
			m_camera.follow(
				sgc::Vec3f{m_targetX, m_targetY, 0.0f},
				0.9f, dt);
		}

		// カメラ更新（シェイク処理）
		m_camera.update(dt);
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		r->clearBackground(sgc::Colorf{0.04f, 0.05f, 0.08f, 1.0f});

		// カメラパラメータ取得
		const auto camPos = m_camera.position();
		const float camZoom = m_camera.zoom();
		// 2D用: zoom < 1 = ズームイン（拡大）
		const float scale = 1.0f / camZoom;
		const float screenCx = sw * 0.5f;
		const float screenCy = sh * 0.5f;

		// ワールド座標→スクリーン座標変換
		// screenPos = (worldPos - camPos) * scale + screenCenter
		// シェイクオフセットはcamera.update()で内部反映済み（viewMatrixに適用）
		// ここではposition()を使って簡易的に2D変換する

		// グリッド描画（ワールド座標）
		drawWorldGrid(r, camPos, scale, screenCx, screenCy, sw, sh);

		// ワールドオブジェクト描画
		drawWorldObjects(r, camPos, scale, screenCx, screenCy);

		// ターゲット描画
		drawTarget(r, camPos, scale, screenCx, screenCy);

		// 原点マーカー
		const float originSx = (0.0f - camPos.x) * scale + screenCx;
		const float originSy = (0.0f - camPos.y) * scale + screenCy;
		r->drawLine({originSx - 10.0f, originSy}, {originSx + 10.0f, originSy},
			2.0f, sgc::Colorf{1.0f, 0.3f, 0.3f, 0.8f});
		r->drawLine({originSx, originSy - 10.0f}, {originSx, originSy + 10.0f},
			2.0f, sgc::Colorf{0.3f, 1.0f, 0.3f, 0.8f});

		// UI オーバーレイ
		drawUI(tr, sw, camPos, camZoom);
	}

private:
	sgc::Cameraf m_camera;         ///< カメラ
	bool m_followTarget{false};    ///< ターゲット追従モード
	float m_targetX{0.0f};         ///< ターゲットX座標
	float m_targetY{0.0f};         ///< ターゲットY座標
	float m_targetAngle{0.0f};     ///< ターゲット角度

	/// @brief ワールド空間のグリッドを描画する
	void drawWorldGrid(
		sgc::IRenderer* r,
		const sgc::Vec3f& camPos, float scale,
		float screenCx, float screenCy,
		float sw, float sh) const
	{
		constexpr float gridSize = 100.0f;
		const sgc::Colorf gridCol{0.15f, 0.15f, 0.25f, 0.4f};

		for (int i = -5; i <= 5; ++i)
		{
			const float worldX = static_cast<float>(i) * gridSize;
			const float sx = (worldX - camPos.x) * scale + screenCx;
			r->drawLine({sx, 0.0f}, {sx, sh}, 1.0f, gridCol);

			const float worldY = static_cast<float>(i) * gridSize;
			const float sy = (worldY - camPos.y) * scale + screenCy;
			r->drawLine({0.0f, sy}, {sw, sy}, 1.0f, gridCol);
		}
	}

	/// @brief ワールドオブジェクトを描画する
	void drawWorldObjects(
		sgc::IRenderer* r,
		const sgc::Vec3f& camPos, float scale,
		float screenCx, float screenCy) const
	{
		// 散らばった矩形オブジェクト
		struct WorldObj
		{
			float x, y, w, h;
			sgc::Colorf color;
		};

		const WorldObj objects[] =
		{
			{ -200.0f, -150.0f, 60.0f, 40.0f, {0.2f, 0.6f, 1.0f, 0.7f} },
			{  150.0f,  -80.0f, 50.0f, 50.0f, {1.0f, 0.4f, 0.3f, 0.7f} },
			{ -100.0f,  120.0f, 70.0f, 35.0f, {0.3f, 0.9f, 0.4f, 0.7f} },
			{  200.0f,  180.0f, 45.0f, 55.0f, {1.0f, 0.8f, 0.2f, 0.7f} },
			{ -300.0f,   50.0f, 40.0f, 60.0f, {0.7f, 0.3f, 1.0f, 0.7f} },
			{  300.0f, -200.0f, 55.0f, 45.0f, {1.0f, 0.5f, 0.7f, 0.7f} },
		};

		for (const auto& obj : objects)
		{
			const float sx = (obj.x - camPos.x) * scale + screenCx;
			const float sy = (obj.y - camPos.y) * scale + screenCy;
			const float sW = obj.w * scale;
			const float sH = obj.h * scale;

			r->drawRect(
				sgc::AABB2f{
					{sx - sW * 0.5f, sy - sH * 0.5f},
					{sx + sW * 0.5f, sy + sH * 0.5f}},
				obj.color);
			r->drawRectFrame(
				sgc::AABB2f{
					{sx - sW * 0.5f, sy - sH * 0.5f},
					{sx + sW * 0.5f, sy + sH * 0.5f}},
				1.5f, obj.color.withAlpha(1.0f));
		}
	}

	/// @brief ターゲット（追従対象）を描画する
	void drawTarget(
		sgc::IRenderer* r,
		const sgc::Vec3f& camPos, float scale,
		float screenCx, float screenCy) const
	{
		const float sx = (m_targetX - camPos.x) * scale + screenCx;
		const float sy = (m_targetY - camPos.y) * scale + screenCy;

		// ターゲット本体
		r->drawCircle({sx, sy}, 14.0f * scale,
			sgc::Colorf{1.0f, 0.9f, 0.3f, 0.8f});
		r->drawCircleFrame({sx, sy}, 14.0f * scale, 2.0f,
			sgc::Colorf{1.0f, 1.0f, 0.5f, 1.0f});

		// 追従時のハイライト
		if (m_followTarget)
		{
			r->drawCircleFrame({sx, sy}, 22.0f * scale, 1.5f,
				sgc::Colorf{1.0f, 1.0f, 0.3f, 0.4f});
		}
	}

	/// @brief UIオーバーレイを描画する
	void drawUI(
		sgc::ITextRenderer* tr, float sw,
		const sgc::Vec3f& camPos, float camZoom) const
	{
		// タイトルと操作説明
		tr->drawText("Camera2D - Pan / Zoom / Shake / Follow", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{0.9f, 0.8f, 0.6f, 1.0f});
		tr->drawText(
			"Arrows: Pan  [1/2] Zoom  [Space] Shake  [Click] Follow  [R] Reset  [Esc] Back",
			12.0f, {10.0f, 38.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		// カメラ情報
		char posBuf[64];
		std::snprintf(posBuf, sizeof(posBuf),
			"Pos: (%.0f, %.0f)", camPos.x, camPos.y);
		tr->drawText(posBuf, 14.0f,
			{sw - 200.0f, 10.0f},
			sgc::Colorf{0.7f, 0.7f, 0.8f, 1.0f});

		char zoomBuf[32];
		std::snprintf(zoomBuf, sizeof(zoomBuf), "Zoom: %.2fx", 1.0f / camZoom);
		tr->drawText(zoomBuf, 14.0f,
			{sw - 200.0f, 30.0f},
			sgc::Colorf{0.7f, 0.7f, 0.8f, 1.0f});

		const std::string followText = m_followTarget
			? "Follow: ON" : "Follow: OFF";
		const sgc::Colorf followCol = m_followTarget
			? sgc::Colorf{0.3f, 1.0f, 0.5f, 1.0f}
			: sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f};
		tr->drawText(followText, 14.0f,
			{sw - 200.0f, 50.0f}, followCol);
	}
};
