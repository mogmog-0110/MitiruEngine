#pragma once
// 3D で見回す図。物を線で描き、ドラッグで回り、ホイールで寄り引きし、クリックで物を選ぶ。直す操作は上からの図でする。
// 世界は右手系で y が上。方位 0 は -z の側から +z を向く (遊ぶときのカメラと同じ側)。

#include "Canvas.hpp"
#include "StageDoc.hpp"

namespace mitiru::tool::stageedit
{

class OrbitView
{
public:
	OrbitView(const Schema& schema, int width, int height);

	[[nodiscard]] int width() const noexcept { return m_w; }
	[[nodiscard]] int height() const noexcept { return m_h; }

	/// 物が全部収まるように見る位置を決める
	void frame(const StageDoc& doc);
	void focusOn(const Vec3& target) { m_target = target; }
	void orbit(double dxPixels, double dyPixels);
	void zoom(double factor);

	[[nodiscard]] Canvas render(const StageDoc& doc) const;
	[[nodiscard]] int pick(const StageDoc& doc, double sx, double sy) const;

	[[nodiscard]] double azimuthDeg() const noexcept { return m_azimuth; }
	[[nodiscard]] double pitchDeg() const noexcept { return m_pitch; }
	[[nodiscard]] double distance() const noexcept { return m_dist; }

private:
	struct Camera
	{
		Vec3 pos;
		Vec3 right;
		Vec3 up;
		Vec3 fwd;
		double focal = 1.0;
	};

	[[nodiscard]] Camera camera() const;
	void drawGeom(Canvas& canvas, const Camera& cam, const Geom& g, Rgba color, int thickness, bool fillFaces) const;

	const Schema* m_schema;
	int m_w;
	int m_h;
	Vec3 m_target;
	double m_azimuth = 20.0;
	double m_pitch = 35.0;
	double m_dist = 80.0;
};

} // namespace mitiru::tool::stageedit
