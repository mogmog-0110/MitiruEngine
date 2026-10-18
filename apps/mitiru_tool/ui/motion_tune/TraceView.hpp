#pragma once
// 流し直した軌跡 (POST /api/ai/trace の応答) を読み、元の値と調整後の値を重ねた画像と数値を作る宣言。
// 左は上から見た足跡 (x と z)、右は高さの変化 (時間と y)。窓や RmlUi には触れない。

#include "../stage_edit/Canvas.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace mitiru::tool::motiontune
{

struct TracePoint
{
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	double speed = 0.0;
};

struct Trace
{
	std::vector<TracePoint> points;
	double dt = 1.0 / 60.0;
};

struct TraceStats
{
	double apexRise = 0.0;     ///< 出発の高さからの最高点 (m)
	double travel = 0.0;       ///< 出発点から終わりまでの水平の距離 (m)
	double endRise = 0.0;      ///< 終わりの高さと出発の高さの差 (m)
	double peakSpeed = 0.0;    ///< speed の欄があれば、その最大
	int apexFrame = 0;
};

/// 応答 {"probe":[x,y,z,(speed)],"rows":[[...],...]} を読む。probe の並びは呼び出し側が決める。
[[nodiscard]] bool parseTrace(const nlohmann::json& body, bool hasSpeed, Trace& out, std::string& error);
[[nodiscard]] TraceStats statsOf(const Trace& trace);

/// base は灰色、tuned は橙色で重ねる。片方が null なら、もう片方だけを描く。
[[nodiscard]] stageedit::Canvas renderTrace(const Trace* base, const Trace* tuned, int width, int height);

} // namespace mitiru::tool::motiontune
