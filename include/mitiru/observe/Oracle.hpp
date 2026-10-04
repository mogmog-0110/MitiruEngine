#pragma once

/// @file Oracle.hpp
/// @brief GameMemory の異常を検出する組み込みオラクルの宣言。
/// NaN/Inf、MITIRU_FIELD_RANGE の範囲外、停滞、フレーム時間の 10 倍スパイク、決定論の破れ、画面の不変と全黒、MITIRU_INVARIANT の違反、
/// 同一フレーム内で複数 phase が同じフィールドを書く順序未定義の衝突 (WriteBlame 併用時のみ) を扱う。
/// Engine ごとの状態は Engine_Module_Adapter.hpp の D11 と同じく、Engine* をキーにした静的 map に持つ。
/// 違反は最新 256 件を OracleEvent に保持し、種類とフィールドごとに 1 回だけ stderr へ出す。JSON は oracleEventsJson で取得し、HTTP route への接続は Engine_Http.hpp が担う。

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/Invariant.hpp>
#include <mitiru/module/LayoutWalk.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/util/Hash.hpp>
#include <mitiru/observe/BugRing.hpp>
#include <mitiru/observe/GameMemoryRing.hpp>
#include <mitiru/observe/WriteBlame.hpp>
#include <mitiru/replay/Recorder.hpp>

namespace mitiru::observe
{

/// @brief DLL 境界を渡らない、host 内部用のオラクル違反。
struct OracleEvent
{
	char          kind[16];       ///< "nan"/"range"/"stagnant"/"spike"/"determinism"/"screen_black"/"screen_stuck"/"invariant"/"conflict"
	std::uint32_t frame;          ///< 検出したフレーム番号
	char          fieldName[32];  ///< 関係するフィールド名 (無ければ空文字)
	float         value;          ///< 関係する値 (無ければ 0)
	char          message[128];   ///< 人間向けの説明 (対処法込み)
};

/// @brief 最新 256 件を保持する OracleEvent のリング。
class OracleRing
{
public:
	static constexpr std::size_t kCapacity = 256;

	void push(const char* kind, std::uint32_t frame, const char* fieldName,
		float value, const std::string& message) noexcept
	{
		OracleEvent& e = m_events[m_head];
		module::detail::copyTag(e.kind, sizeof(e.kind), kind);
		e.frame = frame;
		module::detail::copyTag(e.fieldName, sizeof(e.fieldName), fieldName);
		e.value = value;
		// 文字の途中で切ると JSON にしたとき不正な UTF-8 になるので、文字の境目まで戻して切る
		std::size_t len = message.size() < sizeof(e.message) ? message.size() : sizeof(e.message) - 1;
		while (len > 0 && len < message.size() && (static_cast<unsigned char>(message[len]) & 0xC0) == 0x80) { --len; }
		std::memcpy(e.message, message.data(), len);
		e.message[len] = '\0';
		m_head = (m_head + 1) % kCapacity;
		if (m_count < kCapacity) { ++m_count; }
		if (std::strcmp(kind, "determinism") == 0 || std::strcmp(kind, "draw-writes") == 0) { ++m_mismatchTotal; }
	}

	[[nodiscard]] std::size_t size() const noexcept { return m_count; }

	/// @brief やり直しと記録が食い違った回数 (determinism / draw-writes)。リングが一周しても減らない
	[[nodiscard]] std::uint32_t mismatchTotal() const noexcept { return m_mismatchTotal; }

	/// @brief 0 は直近を指す。範囲外では nullptr を返す。
	[[nodiscard]] const OracleEvent* at(std::size_t offsetFromNewest) const noexcept
	{
		if (offsetFromNewest >= m_count) { return nullptr; }
		const std::size_t idx = (m_head + kCapacity - 1 - offsetFromNewest) % kCapacity;
		return &m_events[idx];
	}

	/// @brief `EngineConfig::oracleMachineLog` を反映する。Engine 側が毎フレーム先頭で設定する。
	void setMachineLogEnabled(bool enabled) noexcept { m_machineLogEnabled = enabled; }
	[[nodiscard]] bool isMachineLogEnabled() const noexcept { return m_machineLogEnabled; }

private:
	std::array<OracleEvent, kCapacity> m_events{};
	std::size_t m_head{0};
	std::size_t m_count{0};
	std::uint32_t m_mismatchTotal{0};
	bool m_machineLogEnabled{false};
};

struct OracleTimeState
{
	std::uint64_t lastMemHash{0};
	std::uint32_t stagnantSinceFrame{0};
	bool          stagnantReported{false};
	std::uint64_t lastInputHash{0};

	bool  haveAvgFrameMs{false};
	float avgFrameMs{0.0f};
	std::uint32_t calmFrames{0};         ///< 重くて当然のフレームの後に続いた、普通のフレームの数
	std::uint64_t lastFinishedLoads{0};
	std::uint64_t lastShaderCompiles{0};
	const void*   lastUpdateFn{nullptr};

	std::uint64_t lastScreenHash{0};
	std::uint32_t screenStagnantSinceFrame{0};
	bool          screenStagnantReported{false};
};

namespace detail
{

inline std::unordered_map<const void*, OracleRing>& oracleRingRegistry()
{
	static std::unordered_map<const void*, OracleRing> reg;
	return reg;
}

inline std::unordered_map<const void*, OracleTimeState>& oracleTimeStateRegistry()
{
	static std::unordered_map<const void*, OracleTimeState> reg;
	return reg;
}

}  // namespace detail

/// @brief engineKey には Engine* を void* にした値を使う。
[[nodiscard]] inline OracleRing& oracleRingFor(const void* engineKey)
{
	return detail::oracleRingRegistry()[engineKey];
}

[[nodiscard]] inline OracleTimeState& oracleStateFor(const void* engineKey)
{
	return detail::oracleTimeStateRegistry()[engineKey];
}

/// @brief ログ用に小数第 1 位まで表示する。
[[nodiscard]] inline std::string fmtMs(float v)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
	return buf;
}

/// @brief mitiru-cli の ScanOracleLines (internal/hunt/oracle.go、正規表現 `^\[oracle\].*$`) が
///        拾う機械可読 1 行を組み立てる。文字列生成だけを分離してあるのは、stderr を
///        キャプチャせずにテストできるようにするため。
[[nodiscard]] inline std::string formatOracleMachineLine(const char* kind, std::uint32_t frame,
	const char* fieldName, float value)
{
	char buf[160];
	std::snprintf(buf, sizeof(buf), "[oracle] kind=%s frame=%u field=%s value=%g",
		kind, frame, (fieldName != nullptr && fieldName[0] != '\0') ? fieldName : "-",
		static_cast<double>(value));
	return buf;
}

/// @brief 端末に出さず、記録と詳細の出力だけに回す種類か
/// @details フレーム時間の跳ねと停滞は推定で、遊んでいる場面によっては正しい動きでも当たる。
///          ゲームを直す必要があると言い切れないので、既定では端末に出さない (HTTP と mitiru hunt は記録から読む)。
[[nodiscard]] inline bool isOracleHeuristic(const char* kind) noexcept
{
	return std::strcmp(kind, "spike") == 0 || std::strcmp(kind, "stagnant") == 0;
}

/// @param message 記録 (OracleEvent::message、127 byte まで) に残す短い文。句点は付けない
/// @param hint 端末にだけ足す原因の見当。句点は付けない。無ければ nullptr
inline void reportOracleEvent(OracleRing& ring, const char* kind, std::uint32_t frame,
	const char* fieldName, float value, const std::string& message, const char* hint = nullptr)
{
	ring.push(kind, frame, fieldName, value, message);
	std::string key = std::string("oracle.") + kind;
	if (fieldName != nullptr && fieldName[0] != '\0') { key += std::string(".") + fieldName; }
	const bool asciiHead = !message.empty() && static_cast<unsigned char>(message.front()) < 0x80;
	std::string line = std::to_string(frame) + " フレーム目に" + (asciiHead ? " " : "") + message + "。";
	if (hint != nullptr) { line += std::string(hint) + "。"; }
	// 種類名は docs/BUG_HUNT.md の表と同じ綴りにする。
	line += std::string("直し方は docs/BUG_HUNT.md の ") + kind + " の項にあります。";
	if (isOracleHeuristic(kind)) { debug::verboseOnce(key, line); }
	else { debug::warnOnce(key, line); }

	// 上の端末への知らせは種類とフィールドごとに 1 回に間引くため、hunt の継続監視には向かない。
	// こちらは opt-in (既定 OFF) で間引かずに毎回出す。
	if (ring.isMachineLogEnabled())
	{
		std::fputs((formatOracleMachineLine(kind, frame, fieldName, value) + "\n").c_str(), stderr);
	}
}

/// @brief elemType 先頭の "range:min:max" (値域) だけを読む。後ろに ";ui:..." /
///        ";group:..." (module/FieldAttr.hpp が合成する UI 範囲・表示グループ) が続いても
///        先頭 2 個の数値だけを拾うので影響しない。先頭が "range:" でなければ false。
[[nodiscard]] inline bool parseRangeTag(const char* elemType, float& outMin, float& outMax) noexcept
{
	if (elemType == nullptr || std::strncmp(elemType, "range:", 6) != 0) { return false; }
	double mn = 0.0, mx = 0.0;
	if (std::sscanf(elemType + 6, "%lf:%lf", &mn, &mx) != 2) { return false; }
	outMin = static_cast<float>(mn);
	outMax = static_cast<float>(mx);
	return true;
}

/// @brief リングに積む表示名 "name"、"name[i]"、"name.member"、"name[i].member" の部品
/// @details 毎フレーム全要素を調べるので、名前の文字列は外れた値を報告するときだけ組み立てる。
struct OracleFieldName
{
	const char* name = "";
	std::int64_t index = -1;        ///< FixedVec の要素番号。-1 なら付けない
	const char* member = nullptr;   ///< struct のメンバー名。nullptr なら付けない

	[[nodiscard]] std::string str() const
	{
		std::string s = name;
		if (index >= 0) { s += "[" + std::to_string(index) + "]"; }
		if (member != nullptr) { s += std::string(".") + member; }
		return s;
	}
};

/// @brief f32/f64 の NaN/Inf と範囲外を調べる。
inline void checkScalarOracle(const std::uint8_t* p, const char* typeTag, const char* elemType,
	const OracleFieldName& at, std::uint32_t frame, OracleRing& ring)
{
	const bool isF32 = std::strcmp(typeTag, "f32") == 0;
	const bool isF64 = std::strcmp(typeTag, "f64") == 0;
	if (!isF32 && !isF64) { return; }

	float value = 0.0f;
	if (isF32) { std::memcpy(&value, p, sizeof(float)); }
	else { double d = 0.0; std::memcpy(&d, p, sizeof(double)); value = static_cast<float>(d); }

	if (!std::isfinite(value))
	{
		const std::string reportName = at.str();
		reportOracleEvent(ring, "nan", frame, reportName.c_str(), value,
			reportName + " が NaN か Inf になりました",
			"0 で割ったか、初期化していない値を読んだ可能性があります");
		return;  // 非有限値は range 判定の対象外
	}
	float mn = 0.0f, mx = 0.0f;
	if (parseRangeTag(elemType, mn, mx) && (value < mn || value > mx))
	{
		const std::string reportName = at.str();
		reportOracleEvent(ring, "range", frame, reportName.c_str(), value,
			reportName + " が " + fmtMs(value) + " になり、宣言した範囲 " + fmtMs(mn) + " から " +
			fmtMs(mx) + " を外れました");
	}
}

/// @brief FixedVec<struct,N> の要素または直にネストした struct の f32/f64 を、schema から 1 段だけ調べる。schema がない場合は何もしない。
inline void checkStructFieldsOracle(const module::FieldDescriptor* schemaFields, std::int32_t schemaFieldCount,
	const std::uint8_t* elemBytes, std::uint32_t elemSize, OracleFieldName at,
	std::uint32_t frame, OracleRing& ring)
{
	for (std::int32_t j = 0; j < schemaFieldCount; ++j)
	{
		const module::FieldDescriptor& sf = schemaFields[j];
		if (sf.name[0] == '\0' || static_cast<std::uint64_t>(sf.offset) + sf.elemSize > elemSize) { continue; }
		at.member = sf.name;
		checkScalarOracle(elemBytes + sf.offset, sf.typeTag, sf.elemType, at, frame, ring);
	}
}

/// @brief f32/f64、FixedVec<f32/f64,N>、FixedVec<struct,N>、直にネストした struct の NaN/Inf と範囲外を調べる。struct は 1 段だけ調べる。
/// @param schemas/schemaCount nullptr/0 の場合は struct の要素を調べない。
inline void checkFieldsOracle(const module::FieldDescriptor* fields, std::int32_t fieldCount,
	const std::uint8_t* mem, std::uint32_t memSize, std::uint32_t frame, OracleRing& ring,
	const module::ReflectSchema* schemas = nullptr, std::int32_t schemaCount = 0)
{
	if (fields == nullptr || mem == nullptr) { return; }
	for (std::int32_t i = 0; i < fieldCount; ++i)
	{
		const module::FieldDescriptor& f = fields[i];
		if (f.name[0] == '\0') { continue; }

		if (std::strcmp(f.typeTag, "vec") == 0)
		{
			std::uint32_t cnt = 0;
			if (static_cast<std::uint64_t>(f.offset) + f.countOffset + sizeof(cnt) <= memSize)
			{ std::memcpy(&cnt, mem + f.offset + f.countOffset, sizeof(cnt)); }
			if (cnt > f.elemCount) { cnt = f.elemCount; }

			const module::ReflectSchema* sch = nullptr;
			for (std::int32_t s = 0; schemas != nullptr && s < schemaCount; ++s)
			{ if (std::strcmp(schemas[s].typeName, f.elemType) == 0) { sch = &schemas[s]; break; } }

			for (std::uint32_t e = 0; e < cnt; ++e)
			{
				const std::uint64_t elemEnd =
					static_cast<std::uint64_t>(f.offset) + static_cast<std::uint64_t>(e + 1) * f.elemSize;
				if (elemEnd > memSize) { break; }
				const std::uint8_t* ep = mem + f.offset + static_cast<std::size_t>(e) * f.elemSize;
				const OracleFieldName at{f.name, static_cast<std::int64_t>(e), nullptr};
				if (sch != nullptr)
				{ checkStructFieldsOracle(sch->fields, sch->fieldCount, ep, f.elemSize, at, frame, ring); }
				else
				{ checkScalarOracle(ep, f.elemType, "", at, frame, ring); }
			}
		}
		else if (std::strcmp(f.typeTag, "struct") == 0)
		{
			const module::ReflectSchema* sch = nullptr;
			for (std::int32_t s = 0; schemas != nullptr && s < schemaCount; ++s)
			{ if (std::strcmp(schemas[s].typeName, f.elemType) == 0) { sch = &schemas[s]; break; } }
			if (sch != nullptr && static_cast<std::uint64_t>(f.offset) + f.elemSize <= memSize)
			{ checkStructFieldsOracle(sch->fields, sch->fieldCount, mem + f.offset, f.elemSize, {f.name}, frame, ring); }
		}
		else if (static_cast<std::uint64_t>(f.offset) + f.elemSize <= memSize)
		{
			checkScalarOracle(mem + f.offset, f.typeTag, f.elemType, {f.name}, frame, ring);
		}
	}
}

/// @brief 反射の無い game の NaN/Inf を、`mitiru_module_float_offsets` の位置 (LayoutWalk.hpp) で調べる。
/// @details 名前が分からないので「GameMemory+位置」で報告する。1 フレームに報告するのは最初の 1 個だけにする
///          (配列が丸ごと NaN になったときに同じ知らせが要素の数だけ並ばないように)。
inline void checkFloatOffsetsOracle(const std::uint32_t* offsets, std::size_t count,
	const std::uint8_t* mem, std::uint32_t memSize, std::uint32_t frame, OracleRing& ring)
{
	if (offsets == nullptr || mem == nullptr) { return; }
	for (std::size_t i = 0; i < count; ++i)
	{
		const bool isDouble = (offsets[i] & module::kFloatOffsetDouble) != 0;
		const std::uint32_t at = offsets[i] & ~module::kFloatOffsetDouble;
		const std::size_t bytes = isDouble ? sizeof(double) : sizeof(float);
		if (static_cast<std::uint64_t>(at) + bytes > memSize) { continue; }
		double value = 0.0;
		if (isDouble) { std::memcpy(&value, mem + at, sizeof(double)); }
		else { float f = 0.0f; std::memcpy(&f, mem + at, sizeof(float)); value = f; }
		if (std::isfinite(value)) { continue; }
		const std::string name = "GameMemory+" + std::to_string(at);
		reportOracleEvent(ring, "nan", frame, name.c_str(), static_cast<float>(value),
			std::string("GameMemory の ") + std::to_string(at) + " byte 目の " + (isDouble ? "double" : "float") +
				" が NaN か Inf になりました",
			"0 で割ったか、初期化していない値を読んだ可能性があります。MITIRU_REFLECT を書くとフィールドの名前が出ます");
		return;
	}
}

/// @brief 同じフレームで異なる phase が同じフィールドを書いた「順序未定義の衝突」を報告する(§5-3)。
/// 値そのものの妥当性 (NaN/範囲) と違い「誰が書いたか」の情報が要るため、`checkFieldsOracle` とは
/// 別に `WriteBlame` を引数に取る。呼び出し側 (ゲーム側 on_update) が `WriteBlame::phase` で
/// 名前を付けている場合にのみ意味を持ち、フレームに 1 phase しか使わないゲームでは常に false。
inline void checkConflictOracle(const module::FieldDescriptor* fields, std::int32_t fieldCount,
	const WriteBlame& blame, std::uint32_t frame, OracleRing& ring)
{
	if (fields == nullptr) { return; }
	for (std::int32_t i = 0; i < fieldCount; ++i)
	{
		const module::FieldDescriptor& f = fields[i];
		if (f.name[0] == '\0' || f.elemSize == 0) { continue; }

		std::uint32_t conflictOffset = 0;
		bool          found = false;
		const std::uint32_t end = static_cast<std::uint32_t>(f.offset) + f.elemSize;
		for (std::uint32_t off = f.offset; off < end && off < blame.frameSize(); ++off)
		{
			if (blame.hasConflict(off)) { conflictOffset = off; found = true; break; }
		}
		if (!found) { continue; }

		const auto        phases = blame.conflictingPhases(conflictOffset);
		std::string       names;
		for (std::size_t p = 0; p < phases.size(); ++p) { if (p != 0) { names += ","; } names += phases[p]; }
		reportOracleEvent(ring, "conflict", frame, f.name, 0.0f,
			std::string(f.name) + " を同じフレームに " + names + " の複数の phase が書きました",
			"書く順番で結果が変わります");
	}
}

/// @brief 入力が変化している間に GameMemory のハッシュが thresholdSeconds 秒間変わらない停滞を検出する。
inline void checkStagnationOracle(const std::uint8_t* mem, std::uint32_t memSize,
	const std::uint8_t* inputBytes, std::uint32_t inputSize, std::uint32_t frame,
	float dtSeconds, float thresholdSeconds, OracleTimeState& state, OracleRing& ring)
{
	if (mem == nullptr || memSize == 0 || thresholdSeconds <= 0.0f) { return; }

	// 毎フレーム GameMemory 全体を通すので、1 byte ずつの FNV-1a でなく速い xxHash を使う (前のフレームと比べるだけ)
	const std::uint64_t memHash = ::mitiru::util::Hash::xxhash(mem, memSize);
	const std::uint64_t inputHash =
		(inputBytes != nullptr && inputSize > 0) ? ::mitiru::util::Hash::xxhash(inputBytes, inputSize) : 0;
	const bool inputChanged = (inputHash != state.lastInputHash);
	state.lastInputHash = inputHash;

	if (memHash != state.lastMemHash)
	{
		state.lastMemHash        = memHash;
		state.stagnantSinceFrame = frame;
		state.stagnantReported   = false;
		return;
	}
	if (!inputChanged || state.stagnantReported) { return; }

	const float elapsed = static_cast<float>(frame - state.stagnantSinceFrame) * dtSeconds;
	if (elapsed >= thresholdSeconds)
	{
		state.stagnantReported = true;
		reportOracleEvent(ring, "stagnant", frame, "", elapsed,
			"入力は変わっているのに GameMemory が " + fmtMs(thresholdSeconds) + " 秒以上変わりません",
			"update が途中で return しているか、入力を読んでいない可能性があります");
	}
}

/// @brief 重くて当然のフレームかを決める材料。前のフレームの判定からの変化で見る
struct FrameLoadSignals
{
	std::uint32_t pendingLoads = 0;    ///< まだ読み終えていない資産の数
	std::uint64_t finishedLoads = 0;   ///< 読み終えた資産の累計
	std::uint64_t shaderCompiles = 0;  ///< キャッシュに無くてコンパイルしたシェーダーの累計
	const void*   updateFn = nullptr;  ///< ゲームの on_update。DLL を差し替えると変わる
};

/// @brief このフレームが読み込み・シェーダーのコンパイル・DLL の差し替えを含んだかを返し、比べる基準を進める
[[nodiscard]] inline bool consumeSettlingSignals(const FrameLoadSignals& s, OracleTimeState& state) noexcept
{
	const bool settling = s.pendingLoads > 0 || s.finishedLoads != state.lastFinishedLoads ||
		s.shaderCompiles != state.lastShaderCompiles || s.updateFn != state.lastUpdateFn;
	state.lastFinishedLoads = s.finishedLoads;
	state.lastShaderCompiles = s.shaderCompiles;
	state.lastUpdateFn = s.updateFn;
	return settling;
}

/// @brief 起動直後と重いフレームの後、この数の普通のフレームを見てから跳ねを判定する (60fps で 1 秒)
inline constexpr std::uint32_t kSpikeWarmupFrames = 60;

/// @brief 直近の平均の 10 倍を超えるフレーム時間を検出する。
/// @param settling 読み込み・シェーダーのコンパイル・DLL の差し替えを含んだフレーム。判定せず、平均も取り直す
inline void checkFrameTimeSpikeOracle(float frameMs, std::uint32_t frame, bool settling,
	OracleTimeState& state, OracleRing& ring)
{
	if (frameMs <= 0.0f) { return; }
	if (settling)
	{
		state.calmFrames = 0;
		state.haveAvgFrameMs = false;
		return;
	}
	// 起動直後のフレームは初期化の続きで重いので、平均にも入れない
	if (++state.calmFrames < kSpikeWarmupFrames) { return; }
	if (!state.haveAvgFrameMs)
	{
		state.avgFrameMs     = frameMs;
		state.haveAvgFrameMs = true;
		return;
	}
	// 2 ms 前後の処理で vsync を 1 コマ落としただけでは鳴らさず、手触りに出る 50 ms 以上に限る。
	constexpr float kSpikeFloorMs = 50.0f;
	if (state.avgFrameMs > 0.01f && frameMs >= state.avgFrameMs * 10.0f && frameMs >= kSpikeFloorMs)
	{
		reportOracleEvent(ring, "spike", frame, "", frameMs,
			"1 フレームに " + fmtMs(frameMs) + " ms かかりました (直近の平均は " + fmtMs(state.avgFrameMs) + " ms)",
			"Tracy でこのフレームの中身を見られます");
	}
	// 急上昇に引きずられないよう、緩い指数移動平均で追従する。
	state.avgFrameMs = state.avgFrameMs * 0.95f + frameMs * 0.05f;
}

/// @brief capture が有効なとき、画素が K フレーム変わらない状態と全黒を検出する。
inline void checkScreenOracle(const std::uint8_t* pixelsRgba, std::size_t pixelBytes,
	std::uint32_t frame, std::uint32_t stagnantFramesThreshold,
	OracleTimeState& state, OracleRing& ring)
{
	if (pixelsRgba == nullptr || pixelBytes < 4) { return; }

	bool allBlack = true;
	for (std::size_t i = 0; i + 3 < pixelBytes; i += 4)
	{
		if (pixelsRgba[i] != 0 || pixelsRgba[i + 1] != 0 || pixelsRgba[i + 2] != 0) { allBlack = false; break; }
	}
	if (allBlack)
	{
		reportOracleEvent(ring, "screen_black", frame, "", 0.0f, "画面が真っ黒になりました",
			"何も描いていないか、カメラかライトの設定がおかしくなっています");
	}

	const std::uint64_t hash = ::mitiru::util::Hash::fnv1a(pixelsRgba, pixelBytes);
	if (hash != state.lastScreenHash)
	{
		state.lastScreenHash           = hash;
		state.screenStagnantSinceFrame = frame;
		state.screenStagnantReported   = false;
		return;
	}
	if (state.screenStagnantReported || stagnantFramesThreshold == 0) { return; }
	if (frame - state.screenStagnantSinceFrame >= stagnantFramesThreshold)
	{
		state.screenStagnantReported = true;
		reportOracleEvent(ring, "screen_stuck", frame, "", 0.0f,
			"画面が " + std::to_string(stagnantFramesThreshold) + " フレーム以上 1 ピクセルも変わりません",
			"描画が止まっています");
	}
}

inline void checkInvariantsOracle(const module::InvariantDescriptor* invariants, std::int32_t count,
	const void* mem, std::uint32_t frame, OracleRing& ring)
{
	if (invariants == nullptr || mem == nullptr) { return; }
	for (std::int32_t i = 0; i < count; ++i)
	{
		const module::InvariantDescriptor& inv = invariants[i];
		if (inv.check == nullptr) { continue; }
		bool ok = false;
		try { ok = inv.check(mem); } catch (...) { ok = false; }
		if (!ok)
		{
			reportOracleEvent(ring, "invariant", frame, inv.name, 0.0f,
				"MITIRU_INVARIANT の " + std::string(inv.name) + " が成り立たなくなりました");
		}
	}
}

/// @brief byteOffset を含む反射のフィールドの名前。反射が無いか、どのフィールドにも入らなければ nullptr。
[[nodiscard]] inline const char* fieldNameAtOffset(const module::FieldDescriptor* fields, std::int32_t fieldCount,
	std::uint32_t byteOffset) noexcept
{
	if (fields == nullptr) { return nullptr; }
	for (std::int32_t i = 0; i < fieldCount; ++i)
	{
		const module::FieldDescriptor& f = fields[i];
		const std::uint32_t count = (f.elemCount > 0) ? f.elemCount : 1;
		std::uint32_t end = f.offset + f.elemSize * count;
		if (f.hasCount != 0) { end = std::max(end, f.offset + f.countOffset + static_cast<std::uint32_t>(sizeof(std::uint32_t))); }
		if (byteOffset >= f.offset && byteOffset < end) { return f.name; }
	}
	return nullptr;
}

namespace detail
{
/// @brief byteOffset が f32/f64 field の中にあり、a・b どちらもその要素が NaN なら true。
/// @details Burst の `FloatMode.Deterministic` と同じ立場 (NaN になること自体は一致、ビット
///          パターンの一致は保証しない)。f32/f64 以外の field や範囲外は対象にしない。
[[nodiscard]] inline bool isNanBitMismatch(const module::FieldDescriptor* fields, std::int32_t fieldCount,
	const std::uint8_t* a, const std::uint8_t* b, std::uint32_t byteOffset) noexcept
{
	if (fields == nullptr) { return false; }
	for (std::int32_t i = 0; i < fieldCount; ++i)
	{
		const module::FieldDescriptor& f = fields[i];
		const bool isF32 = std::strncmp(f.typeTag, "f32", sizeof(f.typeTag)) == 0;
		const bool isF64 = std::strncmp(f.typeTag, "f64", sizeof(f.typeTag)) == 0;
		if (!isF32 && !isF64) { continue; }
		const std::uint32_t elemSize = isF32 ? sizeof(float) : sizeof(double);
		const std::uint32_t count    = (f.elemCount > 0) ? f.elemCount : 1;
		const std::uint32_t begin    = f.offset;
		const std::uint32_t end      = begin + elemSize * count;
		if (byteOffset < begin || byteOffset >= end) { continue; }
		const std::uint32_t elemOffset = begin + ((byteOffset - begin) / elemSize) * elemSize;
		if (isF32)
		{
			float av = 0.0f, bv = 0.0f;
			std::memcpy(&av, a + elemOffset, sizeof(float));
			std::memcpy(&bv, b + elemOffset, sizeof(float));
			return std::isnan(av) && std::isnan(bv);
		}
		double av = 0.0, bv = 0.0;
		std::memcpy(&av, a + elemOffset, sizeof(double));
		std::memcpy(&bv, b + elemOffset, sizeof(double));
		return std::isnan(av) && std::isnan(bv);
	}
	return false;
}
}  // namespace detail

/// @brief K フレーム前の GameMemory と記録済み入力から再シミュレーションし、現在の GameMemory と memcmp する。
/// @param scratch memSize byte 以上の作業領域。
/// @param blameLookup 最初の差分 byte offset から書き込み元を探す関数。指定しない場合は空のままにする。
/// @param fields NaN ビット誤検知の除外判定に使う reflect field 表 (無ければ除外なし)。
/// @return 差分がなければ true。
inline bool checkDeterminismOracle(module::ModuleApi& api, std::uint8_t* liveMemory, std::uint32_t memSize,
	const std::uint8_t* pastBytes, const module::InputSnapshot* inputs, int frameCount,
	std::uint8_t* scratch, std::uint32_t frame, OracleRing& ring,
	const std::function<const char*(std::uint32_t)>& blameLookup = nullptr,
	const module::FieldDescriptor* fields = nullptr, std::int32_t fieldCount = 0)
{
	if (api.on_update == nullptr || liveMemory == nullptr || pastBytes == nullptr ||
		inputs == nullptr || frameCount <= 0 || scratch == nullptr || memSize == 0)
	{
		return true;
	}

	std::memcpy(scratch, liveMemory, memSize);   // 現在の live bytes を退避
	std::memcpy(liveMemory, pastBytes, memSize); // K フレーム前の状態から再開する

	module::FrameIntents intents{};
	for (int i = 0; i < frameCount; ++i)
	{
		std::memset(&intents, 0, sizeof(intents));
		api.on_update(liveMemory, inputs[i].effectiveDt, &inputs[i], &intents);
	}

	bool diverged = std::memcmp(liveMemory, scratch, memSize) != 0;
	std::uint32_t diffOffset = memSize;
	bool onlyNanBits = false;
	if (diverged)
	{
		bool anyRealDiff = false;
		for (std::uint32_t i = 0; i < memSize; ++i)
		{
			if (liveMemory[i] == scratch[i]) { continue; }
			if (detail::isNanBitMismatch(fields, fieldCount, liveMemory, scratch, i)) { continue; }
			anyRealDiff = true;
			if (diffOffset == memSize) { diffOffset = i; }
		}
		if (!anyRealDiff) { onlyNanBits = true; diverged = false; }
	}
	std::memcpy(liveMemory, scratch, memSize);  // 試行前の状態へ復元 (副作用を外に出さない)

	if (onlyNanBits)
	{
		// 差分は全部 NaN のビット表現違いだった = 決定論の破れではない。N 節での二重起票
		// (nan オラクルと determinism オラクルの重複) を避けるため 1 度だけ知らせる。
		debug::verboseOnce("determinism.nan-bits",
			"決定論の検査で見つけた違いは NaN のビットの並びだけなので、決定論の破れとしては数えません。");
	}
	if (diverged)
	{
		const std::string msg = "同じ入力からもう一度計算した結果が今の GameMemory と一致しません";
		std::string hint = "update が GameMemory と MITIRU_SIDE_STATE の外にある値 (static 変数・時計・rand・スレッドの順) を読んでいます";
		if (blameLookup)
		{
			if (const char* blame = blameLookup(diffOffset); blame != nullptr && blame[0] != '\0')
			{
				hint += "。食い違った値を書いたのは ";
				hint += blame;
				hint += " です";
			}
		}
		reportOracleEvent(ring, "determinism", frame, "", static_cast<float>(diffOffset), msg, hint.c_str());
	}
	return !diverged;
}

[[nodiscard]] inline std::string oracleEventsJson(const void* engineKey)
{
	const OracleRing& ring = oracleRingFor(engineKey);
	nlohmann::json arr = nlohmann::json::array();
	for (std::size_t i = ring.size(); i > 0; --i)  // 記録順 (古→新) に並べ直す
	{
		const OracleEvent* e = ring.at(i - 1);
		if (e == nullptr) { continue; }
		arr.push_back(nlohmann::json{
			{"kind", e->kind}, {"frame", e->frame}, {"field", e->fieldName},
			{"value", e->value}, {"message", e->message}});
	}
	return arr.dump();
}

}  // namespace mitiru::observe
