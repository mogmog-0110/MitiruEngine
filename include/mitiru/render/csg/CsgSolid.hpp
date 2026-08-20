#pragma once

/// @file CsgSolid.hpp
/// @brief Makina の CSG ソリッドをエンジンから使うための薄い層
/// @details makina-core は GPU もエンジンも知らないヘッダーオンリーのライブラリで、
///          依存はこの向き（makina-core ← MitiruEngine）にしか無い。
///          ここはその上に sgc 型のインターフェースを被せるだけで、幾何の判断は一切しない。
///
///          @b なぜ距離場をそのままランタイムに持ち込むのか：
///          メッシュなら別途コライダを作る必要があるが、SDF は
///          「その点は内側か、表面までどれだけあるか」に直接答えられる。
///          衝突判定・接地判定・スイープの停止距離が、描画に使っているのと
///          @b 同じ定義から出る。二重に持たないので、ずれようがない。
///
///          `MITIRU_HAS_MAKINA` が立っていないビルドではこのヘッダーは空になる。
///          makina-core が見つからない環境でもエンジンはビルドできる。

#ifdef MITIRU_HAS_MAKINA

#include <makina/Animation.hpp>
#include <makina/Bounds.hpp>
#include <makina/Edit.hpp>
#include <makina/Eval.hpp>
#include <makina/Flatten.hpp>
#include <makina/SceneJson.hpp>

#include <sgc/math/Vec3.hpp>

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace mitiru::render::csg
{

/// @brief Makina のシーンを 1 個のソリッドとして扱う
///
/// コピーは重い（`makina::Scene` は固定容量の flat POD）。
/// 参照で回すか、ロード済みのものをアセット側に 1 つ置くこと。
class CsgSolid
{
public:
	CsgSolid() = default;

	/// @brief .makina.json を読み込む
	/// @return 読めなかったときは false。理由は @ref error
	[[nodiscard]] bool loadFromFile(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			m_error = "could not open '" + path + "'";
			m_valid = false;
			return false;
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		return loadFromJson(ss.str());
	}

	/// @brief JSON 文字列から読み込む
	[[nodiscard]] bool loadFromJson(const std::string& text)
	{
		try
		{
			// ヒープの Scene に直接読む。makina::Scene は約 390 KB あり（輸入容量 4096 ノード）、
			// 値返しの parseScene はその写しを呼び出しフレームに置くので、既定 1 MB の
			// スレッドスタックではテストの `csg::CsgSolid solid;` 一つで溢れる。
			if (!m_scene)
			{
				m_scene = std::make_unique<makina::Scene>();
			}
			makina::parseSceneInto(*m_scene, text);
			// ミュートされたノードは立体の外 (makina Edit.hpp)。makina_bake は withoutMuted
			// してから平坦化するので、こちらも読み込み時に同じ木にしておく — CPU の距離場と
			// 焼いた絵、そして live プログラムのノード数が一致するのはこのため。
			// フラグが立っていないシーン (ほぼ全部) では写しを取らない。
			if (hasMuted(*m_scene))
			{
				// ヒープに直接構築する。値返しの Scene を一度スタックに置くと、
				// loadFromJson の注意書きのとおり既定スタックで溢れる。
				m_scene = std::make_unique<makina::Scene>(makina::withoutMuted(*m_scene));
			}
		}
		catch (const std::exception& e)
		{
			m_error = e.what();
			m_valid = false;
			return false;
		}
		m_posed.reset();
		m_program = makina::EvalProgram{};
		// 動く立体はモーション全体が入る箱 (D-15)。レイマーチは箱の外を切るので、休止姿勢の
		// 箱では振り出した前腕が箱の面で切れる。静止シーンでは worldBounds と同じ。
		m_bounds = makina::motionBounds(*m_scene);
		m_hash = hashOf(text);
		m_valid = true;
		m_error.clear();
		return true;
	}

	[[nodiscard]] bool valid() const noexcept { return m_valid; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

	/// @brief トラック (キーフレーム) を持つか。Makina PLAN.md D-15
	[[nodiscard]] bool animated() const noexcept
	{
		return m_scene && m_scene->tracks.count > 0;
	}

	/// @brief モーションの長さ (秒)。静止シーンは 0
	[[nodiscard]] float motionLength() const
	{
		return m_scene ? makina::animationLength(*m_scene) : 0.0f;
	}

	/// @brief 時刻 t の姿を平坦化した評価プログラム (D-15)
	/// @details live に焼いたシェーダ (CsgBake::live) は葉の数値をこのバッファから読むので、
	///          毎フレームこれを載せれば関節が動く。姿はヒープの写しに sampleInto する —
	///          呼び出しフレームに Scene を置かないのは loadFromJson と同じ理由。
	///          静止シーンでも同じ道を通る (トラックが無ければ休止姿勢がそのまま出る)。
	///          同じ t が続く間は前回の結果を返す。
	/// @pre valid()
	[[nodiscard]] const makina::EvalProgram& programAt(float t)
	{
		if (!m_scene)
		{
			m_program = makina::EvalProgram{};
			return m_program;
		}
		if (m_posed && t == m_posedTime && !m_program.nodes.empty())
		{
			return m_program;
		}
		if (!m_posed)
		{
			m_posed = std::make_unique<makina::Scene>();
		}
		makina::sampleInto(*m_scene, t, *m_posed);
		m_program = makina::flatten(*m_posed);
		m_posedTime = t;
		return m_program;
	}

	/// @brief 読み込んだシーン本文のハッシュ（FNV-1a、16 桁 hex）
	/// @details 焼いた .cso が**このシーンから**焼かれたものかを確かめるためだけにある。
	///          照合できないと、古い .cso が読めて描けて、しかし黙って違う形になる。
	///          makina 側の `makina_bake` が同じ計算をしてマニフェストに入れている。
	[[nodiscard]] const std::string& sourceHash() const noexcept { return m_hash; }

	/// @brief FNV-1a。暗号用途ではない
	[[nodiscard]] static std::string hashOf(const std::string& text)
	{
		std::uint64_t h = 1469598103934665603ull;
		for (const char c : text)
		{
			h ^= static_cast<std::uint8_t>(c);
			h *= 1099511628211ull;
		}
		char buf[24];
		std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
		return buf;
	}

	/// @brief ワールド座標での符号付き距離
	/// @details 符号は厳密（負が内側）。大きさは控えめな下限になりうる。
	///          これは makina-core の契約そのままで、min/max による CSG 合成と
	///          非一様 Scale の補正がどちらも安全側に倒すため（Eval.hpp）。
	///          スイープの前進量に使う分にはこの性質でちょうどよい。
	[[nodiscard]] float distance(const sgc::Vec3f& p) const
	{
		if (!m_valid)
		{
			return 0.0f;
		}
		const double q[3] = { p.x, p.y, p.z };
		const double d = makina::eval(*m_scene, q);
		return makina::isEmpty(d) ? 0.0f : static_cast<float>(d);
	}

	/// @brief その点が立体の内側か
	[[nodiscard]] bool contains(const sgc::Vec3f& p) const
	{
		if (!m_valid)
		{
			return false;
		}
		const double q[3] = { p.x, p.y, p.z };
		const double d = makina::eval(*m_scene, q);
		return !makina::isEmpty(d) && d < 0.0;
	}

	/// @brief 表面法線（距離場の勾配）
	/// @param h 差分幅。省略時はモデルの大きさに比例させる。
	///          固定値だと、ミリ単位のモデルでは粗すぎ、メートル単位では
	///          浮動小数の桁落ちに埋もれる。
	[[nodiscard]] sgc::Vec3f normal(const sgc::Vec3f& p, float h = 0.0f) const
	{
		if (!m_valid)
		{
			return { 0.0f, 1.0f, 0.0f };
		}
		const float eps = (h > 0.0f) ? h : radius() * 1.0e-3f;
		const sgc::Vec3f dx{ eps, 0.0f, 0.0f };
		const sgc::Vec3f dy{ 0.0f, eps, 0.0f };
		const sgc::Vec3f dz{ 0.0f, 0.0f, eps };
		sgc::Vec3f n{ distance(p + dx) - distance(p - dx),
		              distance(p + dy) - distance(p - dy),
		              distance(p + dz) - distance(p - dz) };
		const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
		if (len < 1e-12f)
		{
			return { 0.0f, 1.0f, 0.0f };
		}
		return { n.x / len, n.y / len, n.z / len };
	}

	[[nodiscard]] sgc::Vec3f boundsMin() const
	{
		return m_bounds.box.valid ? sgc::Vec3f{ static_cast<float>(m_bounds.box.lo[0]),
		                                        static_cast<float>(m_bounds.box.lo[1]),
		                                        static_cast<float>(m_bounds.box.lo[2]) }
		                          : sgc::Vec3f{ 0.0f, 0.0f, 0.0f };
	}

	[[nodiscard]] sgc::Vec3f boundsMax() const
	{
		return m_bounds.box.valid ? sgc::Vec3f{ static_cast<float>(m_bounds.box.hi[0]),
		                                        static_cast<float>(m_bounds.box.hi[1]),
		                                        static_cast<float>(m_bounds.box.hi[2]) }
		                          : sgc::Vec3f{ 0.0f, 0.0f, 0.0f };
	}

	/// @brief 境界球の半径。イプシロンをモデルの大きさに合わせるのに使う
	[[nodiscard]] float radius() const
	{
		if (!m_bounds.box.valid)
		{
			return 1.0f;
		}
		double diag = 0.0;
		for (int i = 0; i < 3; ++i)
		{
			const double span = m_bounds.box.hi[i] - m_bounds.box.lo[i];
			diag += span * span;
		}
		const float r = static_cast<float>(std::sqrt(diag) * 0.5);
		return r > 1e-4f ? r : 1e-4f;
	}

	[[nodiscard]] std::uint32_t nodeCount() const noexcept
	{
		return m_valid ? m_scene->nodes.count : 0u;
	}

	/// @brief 生のシーン。レイマーチパスが平坦化するのに要る
	/// @pre valid()。読み込み前は空のシーンを返す
	[[nodiscard]] const makina::Scene& scene() const noexcept
	{
		return m_scene ? *m_scene : emptyScene();
	}

private:
	[[nodiscard]] static bool hasMuted(const makina::Scene& s) noexcept
	{
		for (std::uint32_t i = 0; i < s.nodes.count; ++i)
		{
			if ((s.nodes[i].flags & makina::flags::kMuted) != 0)
			{
				return true;
			}
		}
		return false;
	}

	/// @brief 未読込の CsgSolid が返す空シーン。ヒープに一度だけ作る
	[[nodiscard]] static const makina::Scene& emptyScene()
	{
		static const std::unique_ptr<makina::Scene> kEmpty = std::make_unique<makina::Scene>();
		return *kEmpty;
	}

	/// @details unique_ptr なのは大きさのため（loadFromJson のコメント参照）。このクラスの
	///          「コピーは重い、参照で回せ」という契約はそのまま — コピー不可になっただけ。
	std::unique_ptr<makina::Scene> m_scene;
	/// programAt の作業用。時刻 t の姿と、その平坦化
	std::unique_ptr<makina::Scene> m_posed;
	makina::EvalProgram m_program;
	float m_posedTime = 0.0f;
	makina::BoundsResult m_bounds{};
	bool m_valid = false;
	std::string m_error;
	std::string m_hash;
};

/// @brief 距離場に沿ってレイを進め、最初に当たった点を返す（スフィアトレーシング）
/// @param solid 対象
/// @param origin レイの始点
/// @param dir 正規化済みの向き
/// @param maxDistance これを超えたら当たらなかったとみなす
/// @param hit 当たった点（当たったときのみ書く）
/// @return 当たったか
/// @details 描画と同じ距離場を歩くので、絵とアタリが食い違わない。
///          歩幅を 0.85 に落としてあるのは Difference が距離の下限しか返さないためで、
///          満幅で進むと薄い継ぎ目を通り抜けることがある（PLAN.md R-03）。
[[nodiscard]] inline bool raycast(const CsgSolid& solid, const sgc::Vec3f& origin,
                                  const sgc::Vec3f& dir, float maxDistance, sgc::Vec3f& hit)
{
	if (!solid.valid())
	{
		return false;
	}
	const float eps = solid.radius() * 3.0e-5f;
	float t = 0.0f;
	for (int i = 0; i < 256; ++i)
	{
		const sgc::Vec3f p{ origin.x + dir.x * t, origin.y + dir.y * t, origin.z + dir.z * t };
		const float d = solid.distance(p);
		if (d < eps)
		{
			hit = p;
			return true;
		}
		t += d * 0.85f;
		if (t > maxDistance)
		{
			return false;
		}
	}
	return false;
}

} // namespace mitiru::render::csg

#endif // MITIRU_HAS_MAKINA
