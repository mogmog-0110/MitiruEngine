// mitiru::Engine の detail header。直接 include しないこと。core/Engine.hpp 経由で include される
#pragma once

/// @file Engine_Module_ToolSnapshot.hpp
/// @brief ツール窓 (mitiru_tool) が読む SharedSnapshot を組み立てて書く部分。
/// @details 進めるフレームと、別窓のバーで過去に止めているフレーム (scrub-hold) の両方がここを通る。
/// 止めている間も同じ節 (rewind / perf / gameMemory など) を書くので、バーの窓は止めた後も
/// 自分の節を読み続けられ、▶ で再開を頼める。

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <mitiru/core/InlineMacro.hpp>

MITIRU_INLINE void mitiru::Engine::publishToolSnapshot()
{
	if (!m_moduleInspectorSnapshot) { return; }
	const auto now = std::chrono::steady_clock::now();
	if (m_havePerfTp)
	{
		const float dtMs = std::chrono::duration<float, std::milli>(now - m_lastPerfTp).count();
		if (dtMs > 0.0f)
		{
			const float fps = 1000.0f / dtMs;
			m_emaFps      = m_emaFps > 0.0f ? (m_emaFps * 0.9f + fps * 0.1f) : fps;
			m_lastFrameMs = dtMs;
		}
	}
	m_lastPerfTp = now;
	m_havePerfTp = true;

	// 読み手 (ツール窓) がいない間は組み立てない。JSON とファイルの書き出しが 10Hz で数百回の確保になる。
	// 止めたか解いたかだけは読み手がいなくても書く。読み手が消えて停止を解いた後に、止めたままの snapshot を残さない
	if (m_toolWriteAccum < 6) { ++m_toolWriteAccum; }
	const bool holdUnpublished = m_scrubHold != m_publishedScrubHold;
	if (!holdUnpublished && (!(m_inspectorDirty || m_toolWriteAccum >= 6) || !m_moduleInspectorSnapshot->hasReader())) { return; }
	m_inspectorDirty = false;
	m_toolWriteAccum = 0;

	nlohmann::json out = m_lastGameInspectables.is_object() ? m_lastGameInspectables : nlohmann::json::object();
	out["perf"] = toolPerfSection();
	out["sideState"] = observe::sideStateSection(m_sideState, m_sideStateRing, observe::sideStateReplayMarks());
	out["audio"] = toolAudioSection();
	if (nlohmann::json rewind = toolRewindSection(); !rewind.is_null()) { out["rewind"] = std::move(rewind); }
	// AI Lens: game が MITIRU_REFLECT を宣言していれば、AI が窓を開かずに全状態を構造で読める。
	if (m_moduleReflection.fieldCount() > 0 && m_moduleMemory != nullptr && m_moduleMemorySize > 0)
	{
		out["gameMemory"] = module::detail::buildGameMemoryJson(m_moduleReflection, m_moduleMemory, m_moduleMemorySize);
	}
	if (nlohmann::json scene = toolSceneViewSection(); !scene.is_null()) { out["sceneView"] = std::move(scene); }
	// O5: 直近 commit の決定論ゲート結果 (無ければキー自体を出さない、既定 waiting 表示のため)。
	if (const auto& gate = module::detail::lastReplayGateResults(); !gate.empty())
	{
		nlohmann::json runs = nlohmann::json::array();
		for (const auto& g : gate) { runs.push_back(nlohmann::json{{"file", g.file}, {"pass", g.pass}, {"reason", g.reason}}); }
		out["replayGate"] = nlohmann::json{{"title", "決定論ゲート"}, {"state", nlohmann::json{{"runs", std::move(runs)}}}};
	}
	publishModuleInspectAssets(out);
	publishModuleStory(out);
	if (m_moduleInspectorSnapshot->write(out)) { m_publishedScrubHold = m_scrubHold; }
}

MITIRU_INLINE nlohmann::json mitiru::Engine::toolPerfSection()
{
	// frame は host のフレーム番号。止めている間も進むので、読む側は「host は回っているのに値が動かない」を確かめられる
	nlohmann::json state{{"frame", frameNumber()},
	                     {"fps", static_cast<int>(m_emaFps + 0.5f)},
	                     {"frameMs", m_lastFrameMs},
	                     {"droppedSteps", m_droppedFixedSteps},
	                     {"slowMotion", m_droppedFixedSteps > 0}};
	if (auto gpu = detail::gpuPassTimesJson(m_renderer3D.get()); !gpu.is_null()) { state["gpu"] = std::move(gpu); }
	if (auto loads = detail::streamingReportJson(streamingReport()); !loads.is_null()) { state["loads"] = std::move(loads); }
	return nlohmann::json{{"title", "Performance"}, {"state", std::move(state)}};
}

MITIRU_INLINE nlohmann::json mitiru::Engine::toolAudioSection()
{
	// 再生中チャンネルのメーター (任意)。列挙非対応の audio engine は空配列。
	nlohmann::json channels = nlohmann::json::array();
	int voiceCount = 0;
	if (m_audioEngine)
	{
		for (const auto& m : m_audioEngine->meterChannels())
		{
			if (std::strcmp(m.kind, "voice") == 0) { ++voiceCount; }
			// 空 id / 負の残り秒は「不明」なので、mixer の voice 一覧の行に出さない。
			nlohmann::json ch{{"kind", m.kind}, {"level", m.level}, {"pan", m.pan}};
			if (m.id[0] != '\0')        { ch["id"] = m.id; }
			if (m.asset[0] != '\0')     { ch["asset"] = m.asset; }
			if (m.remainingSec >= 0.0f) { ch["remainingSec"] = m.remainingSec; }
			channels.push_back(std::move(ch));
		}
	}
	return nlohmann::json{{"title", "Audio"},
	                      {"state", nlohmann::json{{"masterVolume", masterVolume()},
	                                               {"engine", m_audioEngine ? "active" : "none"},
	                                               {"voiceCount", voiceCount},
	                                               {"channels", std::move(channels)}}}};
}

MITIRU_INLINE nlohmann::json mitiru::Engine::toolRewindSection()
{
	// 全フレームを送るので、バーの位置がそのまま「何フレーム前か」に 1:1 で対応する。
	if (m_moduleMemoryRing.size() < 2 || m_moduleMemorySize == 0) { return nullptr; }
	const std::size_t frames = m_moduleMemoryRing.size();
	const std::int32_t probeCap = static_cast<std::int32_t>(sizeof(m_moduleApi.seriesProbes) / sizeof(m_moduleApi.seriesProbes[0]));
	const std::int32_t pc = std::min(m_moduleApi.seriesProbeCount, probeCap);

	nlohmann::json state;
	state["capacity"] = static_cast<int>(frames);
	// 窓のバーが頼んだフレームで host が止まっているか (何フレーム前か)。再生中はキーごと無い
	if (m_scrubHold) { state["hold"] = static_cast<std::int64_t>(m_scrubHoldOffset); }
	nlohmann::json markers = nlohmann::json::array();
	bool markersDone = false;
	for (std::int32_t p = 0; p < pc; ++p)
	{
		const auto& probe = m_moduleApi.seriesProbes[p];
		if (probe.accessor == nullptr || probe.name[0] == '\0') { continue; }
		// ring を oldest → newest に走査する (バーの左端 = 最古)。
		std::vector<double> series;
		series.reserve(frames);
		for (std::size_t k = 0; k < frames; ++k)
		{
			if (const std::uint8_t* bytes = m_moduleMemoryRing.at(frames - 1 - k)) { series.push_back(probe.accessor(bytes)); }
		}
		// 窓は /History$/ のキーを系列として探す (例 "hpHistory")。
		state[std::string{probe.name} + "History"] = series;
		if (!markersDone)
		{
			markers = toolRewindMarkers(probe, series);
			markersDone = true;
		}
	}
	state["markers"] = std::move(markers);
	state["spans"] = m_timelineSpans.toJson(static_cast<std::int64_t>(frames));
	return nlohmann::json{{"title", "巻き戻し"}, {"state", std::move(state)}};
}

MITIRU_INLINE nlohmann::json mitiru::Engine::toolRewindMarkers(const module::SeriesProbe& probe, const std::vector<double>& series)
{
	const auto* fields = m_moduleReflection.fieldsData();
	const std::int32_t fieldCount = m_moduleReflection.fieldCount();
	observe::MarkerOpts opts;
	opts.wantEdges  = true;
	opts.epsilon    = 0.5;
	opts.maxMarkers = 24;
	// 系列名が MITIRU_ENUM の field と同名なら状態遷移の系列。enum の差 (3→4) には意味が無いので、
	// 間引きは変化量ではなく新しい順にし、節目に名前を付ける。
	const bool enumSeries = !series.empty() && !observe::enumSeriesName(fields, fieldCount, probe.name, series.back()).empty();
	opts.preferNewest = enumSeries;
	if (probe.hasThreshold)
	{
		opts.hasThreshold = true;
		opts.threshold    = probe.threshold;
	}
	nlohmann::json out = nlohmann::json::array();
	for (const auto& m : observe::extractMarkers(series, opts))
	{
		nlohmann::json mj{{"o", m.offsetFromNewest}, {"v", m.value}, {"k", static_cast<int>(m.kind)}};
		if (enumSeries && m.offsetFromNewest + 1 < series.size())
		{
			const double prev = series[series.size() - 2 - m.offsetFromNewest];
			mj["label"] = observe::enumSeriesName(fields, fieldCount, probe.name, prev) + "\xE2\x86\x92"
			              + observe::enumSeriesName(fields, fieldCount, probe.name, m.value);
		}
		out.push_back(std::move(mj));
	}
	return out;
}

MITIRU_INLINE nlohmann::json mitiru::Engine::toolSceneViewSection()
{
	// 分岐エディタのシーンビュー (ADR 0035 O2)。draw() で溜めた bbox 列に、一致する reflect フィールド名を添える
	// (sourceId は名前の fnv1a32 なので 64 件の線形探索で足りる)。名前が見つからない sourceId (game 側の宣言ミス・衝突) は
	// "field" を付けず id だけ返し、窓の側で読み取り専用にする。
	const auto& objects = module::detail::lastSceneViewObjects();
	if (objects.empty()) { return nullptr; }
	nlohmann::json objs = nlohmann::json::array();
	for (const auto& o : objects)
	{
		nlohmann::json entry{{"id", o.sourceId}, {"x", o.x}, {"y", o.y}, {"w", o.w}, {"h", o.h}};
		// beginObject(name, fieldX, fieldY) の明示指定 (name+".x"/".y" に乗らない分離 scalar 用) を最優先にする。
		const auto& mappings = module::detail::lastSceneFieldMappings();
		const auto fm = std::find_if(mappings.begin(), mappings.end(), [&](const auto& m) { return m.sourceId == o.sourceId; });
		if (fm != mappings.end())
		{
			entry["fieldX"] = fm->fieldX;
			entry["fieldY"] = fm->fieldY;
		}
		else
		{
			for (std::int32_t fi = 0; fi < m_moduleReflection.fieldCount(); ++fi)
			{
				if (module::fnv1a32(m_moduleReflection.fieldsData()[fi].name) != o.sourceId) { continue; }
				entry["field"] = m_moduleReflection.fieldsData()[fi].name;
				break;
			}
		}
		objs.push_back(std::move(entry));
	}
	return nlohmann::json{{"title", "シーンビュー"}, {"state", nlohmann::json{{"objects", std::move(objs)}}}};
}
