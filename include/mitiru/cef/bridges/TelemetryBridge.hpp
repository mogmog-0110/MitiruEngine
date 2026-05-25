#pragma once

// tools/generate_bridge.py が bridges/telemetry.bridge.json から自動生成。
// 手で編集しないこと。再生成は以下:
//   python tools/generate_bridge.py bridges/telemetry.bridge.json
//
// 軽量な metrics / event-counter bridge。bridge codegen パイプラインのデモ。consumer プロジェクトでは削除して構わない。

#include <mitiru/cef/MitiruCefContext.hpp>

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace mitiru::cef::bridges
{

/// @brief `telemetry` 向けに生成された CEF bridge。callback を bind してから registerOn() を呼ぶ。
class TelemetryBridge
{
public:
	using EmitFn = std::function<nlohmann::json(const std::string&, const double&)>;
	using SnapshotFn = std::function<nlohmann::json()>;
	using ResetFn = std::function<nlohmann::json()>;

	EmitFn onEmit;
	SnapshotFn onSnapshot;
	ResetFn onReset;

	void registerOn(MitiruCefContext& ctx)
	{
		ctx.registerHandler("telemetry.emit", [this](const std::string& payload) -> std::string { return _handleEmit(payload); });
		ctx.registerHandler("telemetry.snapshot", [this](const std::string& payload) -> std::string { return _handleSnapshot(payload); });
		ctx.registerHandler("telemetry.reset", [this](const std::string& payload) -> std::string { return _handleReset(payload); });
	}

private:
	std::string _handleEmit(const std::string& payload)
	{
		try
		{
			auto in = nlohmann::json::parse(payload);
			std::string event = in.at("event").get<std::string>();
			double value = in.value("value", 1.0);
			if (!onEmit) { return nlohmann::json{{"error", "onEmit not bound"}}.dump(); }
			nlohmann::json result = onEmit(event, value);
			return result.dump();
		}
		catch (const std::exception& e)
		{
			return nlohmann::json{{"error", e.what()}}.dump();
		}
	}

	std::string _handleSnapshot(const std::string& payload)
	{
		try
		{
			auto in = nlohmann::json::parse(payload);
			if (!onSnapshot) { return nlohmann::json{{"error", "onSnapshot not bound"}}.dump(); }
			nlohmann::json result = onSnapshot();
			return result.dump();
		}
		catch (const std::exception& e)
		{
			return nlohmann::json{{"error", e.what()}}.dump();
		}
	}

	std::string _handleReset(const std::string& payload)
	{
		try
		{
			auto in = nlohmann::json::parse(payload);
			if (!onReset) { return nlohmann::json{{"error", "onReset not bound"}}.dump(); }
			nlohmann::json result = onReset();
			return result.dump();
		}
		catch (const std::exception& e)
		{
			return nlohmann::json{{"error", e.what()}}.dump();
		}
	}
};

} // namespace mitiru::cef::bridges
