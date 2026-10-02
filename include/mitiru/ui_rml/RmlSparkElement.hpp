#pragma once
// <spark value="{{level}}" cap="90"/>: 値が変わるたびに 1 点足して折れ線を描く要素。
// HTML 版の data-m-spark-push (mitiru_bind_tools.js の canvas 描画) に当たる。RmlUi には
// canvas が無いので要素として C++ で描く。正規化 (min-max)・線幅 2px・色 #1a1c1f・上下 2px の
// 余白は HTML 版と同じなので、同じ履歴なら同じ形になる。
//
// seq を付けると、値が同じままでも seq が変わるたびに 1 点足す (fps のように一定の値が続く系列で、
// 時間の経過を横軸に出す)。baseline を付けると 0 起点の絶対スケールになり、基準線 (#e6e7ea) を引く。
// 縦の上限は min (無ければ baseline の 1.25 倍) と履歴の最大の 1.1 倍の大きい方で、点は cap 本ぶんの幅に
// 左から並ぶ。子の <span class="spark-label"> は基準線のすぐ上に置き直す (ツール窓 perf の 60fps 線)。

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <utility>

namespace mitiru::ui_rml
{

class SparkElement final : public Rml::Element
{
public:
	explicit SparkElement(const Rml::String& tag) : Rml::Element(tag) {}

	static void registerInstancer()
	{
		static Rml::ElementInstancerGeneric<SparkElement> instancer;
		Rml::Factory::RegisterElementInstancer("spark", &instancer);
	}

protected:
	void OnAttributeChange(const Rml::ElementAttributes& changed) override
	{
		Rml::Element::OnAttributeChange(changed);
		// seq がある時は value と seq のどちらが先に書き換わっても 1 回だけ足すよう、OnUpdate (data model の
		// 反映が全部済んだ後) まで待つ。
		if (HasAttribute("seq"))
		{
			if (changed.find("seq") != changed.end()) { m_pendingPush = true; }
			return;
		}
		if (changed.find("value") != changed.end()) { push(); }
	}

	void OnResize() override { m_dirty = true; }

	void OnUpdate() override
	{
		Rml::Element::OnUpdate();
		if (std::exchange(m_pendingPush, false)) { push(); }
		if (m_labelBottom < 0.0f || m_labelBottom == m_labelApplied) { return; }
		m_labelApplied = m_labelBottom;
		for (int i = 0; i < GetNumChildren(); ++i)
		{
			Rml::Element* child = GetChild(i);
			if (child->IsClassSet("spark-label")) { child->SetProperty(Rml::PropertyId::Bottom, Rml::Property(m_labelBottom, Rml::Unit::PX)); }
		}
	}

	void OnRender() override
	{
		if (m_dirty) { rebuild(); }
		m_geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Padding));
	}

private:
	using Points = Rml::Vector<Rml::Vector2f>;

	void push()
	{
		const Rml::String text = GetAttribute<Rml::String>("value", "");
		if (text.empty()) { return; }
		m_history.push_back(std::strtof(text.c_str(), nullptr));
		const auto cap = static_cast<std::size_t>(std::max(2, GetAttribute<int>("cap", 180)));
		while (m_history.size() > cap) { m_history.pop_front(); }
		m_dirty = true;
	}

	void rebuild()
	{
		m_dirty = false;
		const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Padding);
		const float baseline = GetAttribute<float>("baseline", 0.0f);
		Rml::Mesh mesh;
		if (baseline > 0.0f) { buildAbsolute(mesh, size, baseline); }
		else if (m_history.size() >= 2) { appendStroke(mesh, relativePoints(size)); }
		auto* rm = GetRenderManager();
		m_geometry = (rm != nullptr && !mesh.indices.empty()) ? rm->MakeGeometry(std::move(mesh)) : Rml::Geometry();
	}

	Points relativePoints(Rml::Vector2f size) const
	{
		const auto [lo, hi] = std::minmax_element(m_history.begin(), m_history.end());
		const float mn = *lo, range = (*hi == *lo) ? 1.0f : (*hi - *lo);
		const float stepX = size.x / static_cast<float>(m_history.size() - 1);
		Points pts;
		pts.reserve(m_history.size());
		for (std::size_t i = 0; i < m_history.size(); ++i)
		{
			pts.push_back({ stepX * static_cast<float>(i), size.y - 2.0f - (m_history[i] - mn) / range * (size.y - 4.0f) });
		}
		return pts;
	}

	void buildAbsolute(Rml::Mesh& mesh, Rml::Vector2f size, float baseline)
	{
		float top = GetAttribute<float>("min", 0.0f);
		if (top <= 0.0f) { top = baseline * 1.25f; }
		for (const float v : m_history) { top = std::max(top, v * 1.1f); }
		const float baseY = size.y - baseline / top * size.y;
		Rml::MeshUtilities::GenerateQuad(mesh, { 0.0f, baseY - 0.5f }, { size.x, 1.0f }, Rml::ColourbPremultiplied(0xe6, 0xe7, 0xea, 255));
		m_labelBottom = size.y - baseY + 4.0f;
		if (m_history.size() < 2) { return; }
		const float cap = static_cast<float>(std::max(2, GetAttribute<int>("cap", 180)));
		Points pts;
		pts.reserve(m_history.size());
		for (std::size_t i = 0; i < m_history.size(); ++i)
		{
			pts.push_back({ size.x * static_cast<float>(i) / (cap - 1.0f), size.y - m_history[i] / top * size.y });
		}
		appendStroke(mesh, pts);
	}

	// 1 点につき「外側の縁 (透明) / 線の端 / 線の端 / 外側の縁」の 4 頂点を並べ、端を 1px ぼかして
	// アンチエイリアスに見せる (canvas の stroke は 2px 幅 + AA)。
	static void appendStroke(Rml::Mesh& mesh, const Points& pts)
	{
		constexpr float kHalf = 1.0f, kFeather = 1.0f;
		const Rml::ColourbPremultiplied ink(0x1a, 0x1c, 0x1f, 255), clear(0, 0, 0, 0);
		const int base = static_cast<int>(mesh.vertices.size());
		const int n = static_cast<int>(pts.size());
		for (int i = 0; i < n; ++i)
		{
			const Rml::Vector2f nrm = miterNormal(pts, i);
			const float offsets[4] = { -(kHalf + kFeather), -kHalf, kHalf, kHalf + kFeather };
			for (int k = 0; k < 4; ++k)
			{
				Rml::Vertex v;
				v.position = pts[static_cast<std::size_t>(i)] + nrm * offsets[k];
				v.colour = (k == 0 || k == 3) ? clear : ink;
				mesh.vertices.push_back(v);
			}
			if (i == 0) { continue; }
			const int a = base + (i - 1) * 4, b = base + i * 4;
			for (int k = 0; k < 3; ++k)
			{
				mesh.indices.insert(mesh.indices.end(), { a + k, b + k, a + k + 1, a + k + 1, b + k, b + k + 1 });
			}
		}
	}

	// 隣り合う 2 本の線分の法線の平均を、折れ角で細らないよう 1/cos で伸ばす (急な角は 3 倍で頭打ち)。
	static Rml::Vector2f miterNormal(const Points& pts, int i)
	{
		auto segNormal = [&](int from) {
			const Rml::Vector2f d = (pts[static_cast<std::size_t>(from + 1)] - pts[static_cast<std::size_t>(from)]).Normalise();
			return Rml::Vector2f(-d.y, d.x);
		};
		const int last = static_cast<int>(pts.size()) - 1;
		if (i == 0) { return segNormal(0); }
		if (i == last) { return segNormal(last - 1); }
		const Rml::Vector2f n0 = segNormal(i - 1), n1 = segNormal(i);
		const Rml::Vector2f m = (n0 + n1).Normalise();
		const float cosHalf = std::max(m.DotProduct(n1), 1.0f / 3.0f);
		return m / cosHalf;
	}

	std::deque<float> m_history;
	Rml::Geometry m_geometry;
	float m_labelBottom = -1.0f;   ///< 基準線の上に置く名札の bottom (px)。基準線が無ければ負
	float m_labelApplied = -1.0f;
	bool m_pendingPush = false;
	bool m_dirty = false;
};

} // namespace mitiru::ui_rml
