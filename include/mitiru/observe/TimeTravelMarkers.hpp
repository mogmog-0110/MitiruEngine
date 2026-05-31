#pragma once

/// @file TimeTravelMarkers.hpp
/// @brief snapshot ring から「見るべきフレーム」を導く純関数群 (差別化軸 2)
/// @details
/// `TimeTravelRecorder<Snapshot>` の ring は raw snapshot を at(offset)/size() で
/// 返すだけで、系列の「形」や「節目」を一切示さない。300 フレームを 1 枚ずつ
/// scrub して「どこで HP が落ちたか」を探すのは、まさに哲学が戒める非可読な操作
/// (全部見える = 何も見えない)。本ヘッダはその ANALYSIS 層を ring の上に足す:
///
/// - extractMarkers: ユーザーの `double accessor(const Snapshot&)` で ring を
///   oldest->newest に走査し、Edge / Threshold / 極値の Marker 列を返す。
/// - buildSparkline: 同じ accessor で系列を間引き、min/max 正規化した [0,1] 列を返す。
/// - nearestMarker: inspector の scrub cursor 位置から最寄り Marker を返す (snap 用)。
/// - toJson: 既存 exportedInspectables[] payload 用の compact JSON を組み立てる。
///
/// 設計判断:
/// - Snapshot 型に engine は無知 (template T)。ring/accessor が唯一の入力。
/// - offsetFromNewest は recorder.at() と同一規約 (0 = newest)。cursor snap が直結する。
/// - 純関数・決定論的・例外を投げない。allocation は sampleCount/ring size で有界。
/// - accessor は渡された Snapshot のみ読むこと (GameMemory 由来のコピー)。static や
///   外部状態を読むと replay 非再現になる (ADR 0005)。これは契約であり構造保証ではない。
/// - JSON は JsonEscape.hpp の hand-rolled 方式 (StructuredDiff に倣う)。InspectableExport
///   の char json[3968] に収める制約があり、nlohmann 依存も持ち込まないため。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <mitiru/observe/TimeTravelRecorder.hpp>

namespace mitiru::observe
{

/// @brief Marker の種別
enum class MarkerKind : std::uint8_t
{
	Edge,          ///< 前サンプルから値が (epsilon を超えて) 変化した
	ThresholdUp,   ///< caller 指定 threshold を下から上へ跨いだ
	ThresholdDown, ///< threshold を上から下へ跨いだ
	LocalMin,      ///< 谷 (前後より小さい turning point)
	LocalMax       ///< 山 (前後より大きい turning point)
};

/// @brief 1 つの注目フレーム (POD)
struct Marker
{
	std::size_t offsetFromNewest{0};       ///< recorder.at() と同一規約 (0 = newest)
	double      value{0.0};                ///< そのフレームの accessor 値
	MarkerKind  kind{MarkerKind::Edge};    ///< 何故注目に値するか
};

/// @brief extractMarkers の挙動を制御する設定 (POD)
struct MarkerOpts
{
	double      epsilon      = 1e-4;  ///< sub-epsilon の差分は無視 (Snap.hpp の sliver 規約に合わせる)
	bool        wantEdges    = true;  ///< 値変化 Marker を出すか
	bool        wantExtrema  = false; ///< 極値 (山/谷) Marker を出すか
	bool        hasThreshold = false; ///< threshold 跨ぎ判定を行うか
	double      threshold    = 0.0;   ///< hasThreshold=true のときの閾値
	std::size_t maxMarkers   = 64;    ///< 上限超過時は |変化量| が大きい順に残す
};

/// @brief 間引き済みの正規化系列 (inline graph 用)
struct Sparkline
{
	double             minV = 0.0;  ///< 元系列の最小値
	double             maxV = 0.0;  ///< 元系列の最大値
	std::vector<float> normalized;  ///< 各要素 [0,1]。minV==maxV のとき全要素 0.5
};

// ---------------------------------------------------------------------------
// 実装詳細 (internal)
// ---------------------------------------------------------------------------
namespace detail
{

/// @brief 数値を locale 非依存の小数文字列に変換する
inline std::string numToStr(double v)
{
	// 整数に丸まる場合は小数点なし、それ以外は最大 6 桁
	const long long iv = static_cast<long long>(v);
	if (static_cast<double>(iv) == v)
	{
		return std::to_string(iv);
	}
	// 固定小数点 6 桁を手動で組み立てる (locale 非依存)
	const bool neg = v < 0.0;
	double abs_v = neg ? -v : v;
	const long long intPart = static_cast<long long>(abs_v);
	const double frac = abs_v - static_cast<double>(intPart);
	const long long fracPart = static_cast<long long>(frac * 1000000.0 + 0.5);
	std::string s;
	if (neg) { s += '-'; }
	s += std::to_string(intPart);
	s += '.';
	// 6 桁ゼロ埋め
	const std::string fp = std::to_string(fracPart);
	for (int i = static_cast<int>(fp.size()); i < 6; ++i) { s += '0'; }
	s += fp;
	// 末尾ゼロ除去
	while (s.size() > 1 && s.back() == '0') { s.pop_back(); }
	if (!s.empty() && s.back() == '.') { s.pop_back(); }
	return s;
}

/// @brief 1 サンプルの分類結果 (extractMarkers 内で使う中間値)
struct SampleResult
{
	bool        isEdge         = false;
	bool        isThresholdUp  = false;
	bool        isThresholdDown = false;
	bool        isLocalMin     = false;
	bool        isLocalMax     = false;
	double      magnitude      = 0.0;  ///< 変化量絶対値 (cap 判定用)
};

/// @brief 隣接 2 点から Edge / Threshold 判定を行う
inline SampleResult classifyPair(
	double prev, double curr,
	const MarkerOpts& opts)
{
	SampleResult r;
	const double delta = curr - prev;
	r.magnitude = std::abs(delta);

	if (opts.wantEdges && r.magnitude > opts.epsilon)
	{
		r.isEdge = true;
	}
	if (opts.hasThreshold)
	{
		if (prev < opts.threshold && curr >= opts.threshold)
		{
			r.isThresholdUp = true;
		}
		else if (prev >= opts.threshold && curr < opts.threshold)
		{
			r.isThresholdDown = true;
		}
	}
	return r;
}

/// @brief 3 点から極値 (LocalMin/Max) 判定を行う
inline void classifyExtrema(
	double prev, double curr, double next,
	SampleResult& r)
{
	if (curr <= prev && curr <= next)
	{
		r.isLocalMin = true;
	}
	if (curr >= prev && curr >= next)
	{
		r.isLocalMax = true;
	}
}

}  // namespace detail

// ---------------------------------------------------------------------------
// 公開 API
// ---------------------------------------------------------------------------

/// @brief ring を走査して注目フレーム (Marker) 列を導く
/// @tparam Snapshot ユーザー定義のフレーム状態型 (recorder と同一)
/// @tparam Accessor `double(const Snapshot&)` 互換の呼び出し可能オブジェクト
/// @param ring      解析対象の recorder (読み取りのみ)
/// @param accessor  Snapshot から追跡スカラーを取り出す純関数
/// @param opts      判定設定
/// @return offsetFromNewest 降順 (oldest 側が大) の Marker 列。size()<2 では空。
/// @note 例外は投げない。
template <typename Snapshot, typename Accessor>
[[nodiscard]] std::vector<Marker> extractMarkers(
	const TimeTravelRecorder<Snapshot>& ring,
	Accessor&&                          accessor,
	const MarkerOpts&                   opts = {})
{
	const std::size_t n = ring.size();
	if (n < 2) { return {}; }

	// oldest->newest に値列を構築 (index i → offsetFromNewest = n-1-i)
	std::vector<double> vals(n);
	for (std::size_t i = 0; i < n; ++i)
	{
		vals[i] = accessor(*ring.at(n - 1 - i));
	}

	// 中間リスト: (Marker, magnitude) のペアで保持して後で cap する
	struct Candidate { Marker m; double mag; };
	std::vector<Candidate> cands;
	cands.reserve(n);

	for (std::size_t i = 1; i < n; ++i)
	{
		const std::size_t offsetFromNewest = n - 1 - i;
		auto r = detail::classifyPair(vals[i - 1], vals[i], opts);

		if (opts.wantExtrema && i > 0 && i < n - 1)
		{
			detail::classifyExtrema(vals[i - 1], vals[i], vals[i + 1], r);
		}

		auto pushCand = [&](MarkerKind k, double mag)
		{
			cands.push_back({ Marker{offsetFromNewest, vals[i], k}, mag });
		};

		if (r.isEdge)         { pushCand(MarkerKind::Edge,          r.magnitude); }
		if (r.isThresholdUp)  { pushCand(MarkerKind::ThresholdUp,   r.magnitude); }
		if (r.isThresholdDown){ pushCand(MarkerKind::ThresholdDown,  r.magnitude); }
		if (r.isLocalMin)     { pushCand(MarkerKind::LocalMin,       0.0); }
		if (r.isLocalMax)     { pushCand(MarkerKind::LocalMax,       0.0); }
	}

	// maxMarkers cap: 変化量降順で上位を残し、offsetFromNewest 降順に並べ直す
	if (opts.maxMarkers > 0 && cands.size() > opts.maxMarkers)
	{
		std::partial_sort(
			cands.begin(),
			cands.begin() + static_cast<std::ptrdiff_t>(opts.maxMarkers),
			cands.end(),
			[](const Candidate& a, const Candidate& b)
			{
				if (a.mag != b.mag) { return a.mag > b.mag; }
				return a.m.offsetFromNewest > b.m.offsetFromNewest; // 決定論的 tie-break
			});
		cands.resize(opts.maxMarkers);
	}

	// offsetFromNewest 降順 (oldest 側が先) にソートして返す
	std::sort(cands.begin(), cands.end(),
		[](const Candidate& a, const Candidate& b)
		{
			return a.m.offsetFromNewest > b.m.offsetFromNewest;
		});

	std::vector<Marker> result;
	result.reserve(cands.size());
	for (auto& c : cands) { result.push_back(c.m); }
	return result;
}

/// @brief ring を間引いて min/max 正規化した sparkline を組み立てる
/// @tparam Snapshot ユーザー定義のフレーム状態型
/// @tparam Accessor `double(const Snapshot&)` 互換の呼び出し可能オブジェクト
/// @param ring        解析対象の recorder (読み取りのみ)
/// @param accessor    追跡スカラーを取り出す純関数
/// @param sampleCount 出力したい点数。ring.size() を超える場合は size() に clamp。
/// @return normalized の長さ == min(sampleCount, ring.size())。size()<2 では空。
/// @note minV==maxV のとき全要素 0.5f を返す。例外は投げない。
template <typename Snapshot, typename Accessor>
[[nodiscard]] Sparkline buildSparkline(
	const TimeTravelRecorder<Snapshot>& ring,
	Accessor&&                          accessor,
	std::size_t                         sampleCount)
{
	const std::size_t n = ring.size();
	if (n < 2) { return {}; }

	// 出力点数を clamp
	const std::size_t numSamples = (sampleCount < n) ? sampleCount : n;
	if (numSamples == 0) { return {}; }

	// oldest->newest の値列 (full scan) で min/max を求める
	double minV = accessor(*ring.at(n - 1));
	double maxV = minV;
	for (std::size_t i = 0; i < n; ++i)
	{
		const double v = accessor(*ring.at(n - 1 - i));
		if (v < minV) { minV = v; }
		if (v > maxV) { maxV = v; }
	}

	const bool flat = (maxV - minV) <= 1e-12;

	// even stride でサンプリングし正規化
	std::vector<float> normalized;
	normalized.reserve(numSamples);
	for (std::size_t s = 0; s < numSamples; ++s)
	{
		// oldest 側 (大 offset) から newest 側 (offset 0) へ均等にマッピング
		const std::size_t idx = (numSamples == 1)
			? 0
			: (n - 1) - (s * (n - 1) / (numSamples - 1));
		const double v = accessor(*ring.at(idx));
		normalized.push_back(flat ? 0.5f : static_cast<float>((v - minV) / (maxV - minV)));
	}

	return Sparkline{minV, maxV, std::move(normalized)};
}

/// @brief scrub cursor 位置から最寄りの Marker を返す (snap 用)
/// @param markers              extractMarkers の結果
/// @param currentOffsetFromNewest inspector のローカル scrub cursor (0 = newest)
/// @return 最寄り Marker への pointer。markers が空なら nullptr。
/// @note 返り値は markers が生存する間のみ valid。例外は投げない。
[[nodiscard]] inline const Marker* nearestMarker(
	const std::vector<Marker>& markers,
	std::size_t                currentOffsetFromNewest) noexcept
{
	if (markers.empty()) { return nullptr; }

	const Marker* best = &markers[0];
	std::size_t   bestDist = (markers[0].offsetFromNewest >= currentOffsetFromNewest)
		? (markers[0].offsetFromNewest - currentOffsetFromNewest)
		: (currentOffsetFromNewest - markers[0].offsetFromNewest);

	for (std::size_t i = 1; i < markers.size(); ++i)
	{
		const std::size_t dist = (markers[i].offsetFromNewest >= currentOffsetFromNewest)
			? (markers[i].offsetFromNewest - currentOffsetFromNewest)
			: (currentOffsetFromNewest - markers[i].offsetFromNewest);

		// 距離が小さい方を採用。同距離は小さい offset (newer 側) を優先
		if (dist < bestDist ||
			(dist == bestDist && markers[i].offsetFromNewest < best->offsetFromNewest))
		{
			best = &markers[i];
			bestDist = dist;
		}
	}
	return best;
}

/// @brief Marker 列を compact JSON 配列にする (exportedInspectables[] payload 用)
/// @param markers 直列化対象
/// @return 例: [{"o":12,"v":80,"k":0},{"o":5,"v":40,"k":2}]
/// @note InspectableExport.json (char[3968]) に収まるよう冗長な空白を出さない。
[[nodiscard]] inline std::string toJson(const std::vector<Marker>& markers)
{
	std::string json;
	json += '[';
	for (std::size_t i = 0; i < markers.size(); ++i)
	{
		if (i > 0) { json += ','; }
		json += "{\"o\":";
		json += std::to_string(markers[i].offsetFromNewest);
		json += ",\"v\":";
		json += detail::numToStr(markers[i].value);
		json += ",\"k\":";
		json += std::to_string(static_cast<unsigned>(markers[i].kind));
		json += '}';
	}
	json += ']';
	return json;
}

/// @brief Sparkline を compact JSON にする (exportedInspectables[] payload 用)
/// @param spark 直列化対象
/// @return 例: {"min":40,"max":100,"n":[0.0,0.5,1.0]}
[[nodiscard]] inline std::string toJson(const Sparkline& spark)
{
	std::string json;
	json += "{\"min\":";
	json += detail::numToStr(spark.minV);
	json += ",\"max\":";
	json += detail::numToStr(spark.maxV);
	json += ",\"n\":[";
	for (std::size_t i = 0; i < spark.normalized.size(); ++i)
	{
		if (i > 0) { json += ','; }
		json += detail::numToStr(static_cast<double>(spark.normalized[i]));
	}
	json += "]}";
	return json;
}

}  // namespace mitiru::observe
