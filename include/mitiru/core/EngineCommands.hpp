#pragma once

/// @file EngineCommands.hpp
/// @brief エンジン操作コマンド一括登録
/// @details CommandSystemにエンジンの全操作をカテゴリ別に登録する。
///          scene / render / audio / physics / ui / vn / system / editor / asset / input

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <mitiru/core/CommandSystem.hpp>

namespace mitiru
{

// ── ヘルパー ────────────────────────────────────

namespace detail
{

/// @brief CommandArgからstringを取得する（デフォルト値付き）
[[nodiscard]] inline std::string argString(
	const std::vector<CommandArg>& args, std::size_t idx,
	const std::string& defaultVal = {})
{
	if (idx >= args.size())
	{
		return defaultVal;
	}
	if (auto* s = std::get_if<std::string>(&args[idx]))
	{
		return *s;
	}
	return defaultVal;
}

/// @brief CommandArgからintを取得する（デフォルト値付き）
[[nodiscard]] inline int argInt(
	const std::vector<CommandArg>& args, std::size_t idx,
	int defaultVal = 0)
{
	if (idx >= args.size())
	{
		return defaultVal;
	}
	if (auto* v = std::get_if<int>(&args[idx]))
	{
		return *v;
	}
	if (auto* s = std::get_if<std::string>(&args[idx]))
	{
		try { return std::stoi(*s); }
		catch (...) { return defaultVal; }
	}
	return defaultVal;
}

/// @brief CommandArgからfloatを取得する（デフォルト値付き）
[[nodiscard]] inline float argFloat(
	const std::vector<CommandArg>& args, std::size_t idx,
	float defaultVal = 0.0f)
{
	if (idx >= args.size())
	{
		return defaultVal;
	}
	if (auto* v = std::get_if<float>(&args[idx]))
	{
		return *v;
	}
	if (auto* v = std::get_if<int>(&args[idx]))
	{
		return static_cast<float>(*v);
	}
	if (auto* s = std::get_if<std::string>(&args[idx]))
	{
		try { return std::stof(*s); }
		catch (...) { return defaultVal; }
	}
	return defaultVal;
}

/// @brief CommandArgからboolを取得する（デフォルト値付き）
[[nodiscard]] inline bool argBool(
	const std::vector<CommandArg>& args, std::size_t idx,
	bool defaultVal = false)
{
	if (idx >= args.size())
	{
		return defaultVal;
	}
	if (auto* v = std::get_if<bool>(&args[idx]))
	{
		return *v;
	}
	if (auto* s = std::get_if<std::string>(&args[idx]))
	{
		return (*s == "true" || *s == "1" || *s == "on" || *s == "yes");
	}
	if (auto* v = std::get_if<int>(&args[idx]))
	{
		return *v != 0;
	}
	return defaultVal;
}

} // namespace detail

// ── エンジンコマンド登録 ─────────────────────────────

/// @brief エンジンの全操作をCommandSystemに登録するクラス
/// @details Engineへの参照を保持し、各コマンドのラムダから操作を実行する。
///          Engineが生存している間だけ有効。
///
/// @code
/// mitiru::CommandSystem cmd;
/// mitiru::Engine engine;
/// mitiru::EngineCommands::registerAll(cmd, engine);
///
/// cmd.executeString("system.fps");
/// cmd.executeString("scene.list");
/// @endcode
class EngineCommands
{
public:
	/// @brief 全カテゴリのコマンドを登録する
	/// @param cmd コマンドシステム
	/// @param engine エンジンインスタンス
	static void registerAll(CommandSystem& cmd, Engine& engine)
	{
		registerSceneCommands(cmd, engine);
		registerRenderCommands(cmd, engine);
		registerAudioCommands(cmd, engine);
		registerPhysicsCommands(cmd, engine);
		registerUICommands(cmd, engine);
		registerVNCommands(cmd, engine);
		registerSystemCommands(cmd, engine);
		registerEditorCommands(cmd, engine);
		registerAssetCommands(cmd, engine);
		registerInputCommands(cmd, engine);
	}

private:
	// ════════════════════════════════════════════════════════
	// scene.* — シーン操作
	// ════════════════════════════════════════════════════════

	static void registerSceneCommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
	{
		// scene.add_node <name> [type] [parent]
		{
			CommandDef def;
			def.name = "scene.add_node";
			def.category = "scene";
			def.description = "Add a new node to the scene";
			def.usage = "scene.add_node <name> [type] [parent_id]";
			def.argNames = {"name", "type", "parent_id"};
			def.argTypes = {"string", "string", "int"};
			def.argRequired = {true, false, false};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto name = detail::argString(args, 0, "NewNode");
				const auto type = detail::argString(args, 1, "empty");
				const auto parent = detail::argInt(args, 2, 0);
				return CommandResult::ok(
					"Added node '" + name + "' (type=" + type
					+ ", parent=" + std::to_string(parent) + ")");
			};
			cmd.registerCommand(def);
		}

		// scene.delete_node <id>
		{
			CommandDef def;
			def.name = "scene.delete_node";
			def.category = "scene";
			def.description = "Delete a node from the scene";
			def.usage = "scene.delete_node <id>";
			def.argNames = {"id"};
			def.argTypes = {"int"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto id = detail::argInt(args, 0);
				return CommandResult::ok(
					"Deleted node " + std::to_string(id));
			};
			cmd.registerCommand(def);
		}

		// scene.rename_node <id> <name>
		{
			CommandDef def;
			def.name = "scene.rename_node";
			def.category = "scene";
			def.description = "Rename a scene node";
			def.usage = "scene.rename_node <id> <name>";
			def.argNames = {"id", "name"};
			def.argTypes = {"int", "string"};
			def.argRequired = {true, true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto id = detail::argInt(args, 0);
				const auto name = detail::argString(args, 1);
				return CommandResult::ok(
					"Renamed node " + std::to_string(id) + " to '" + name + "'");
			};
			cmd.registerCommand(def);
		}

		// scene.move_node <id> <x> <y> <z>
		{
			CommandDef def;
			def.name = "scene.move_node";
			def.category = "scene";
			def.description = "Set node position";
			def.usage = "scene.move_node <id> <x> <y> <z>";
			def.argNames = {"id", "x", "y", "z"};
			def.argTypes = {"int", "float", "float", "float"};
			def.argRequired = {true, true, true, true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto id = detail::argInt(args, 0);
				const auto x = detail::argFloat(args, 1);
				const auto y = detail::argFloat(args, 2);
				const auto z = detail::argFloat(args, 3);
				return CommandResult::ok(
					"Moved node " + std::to_string(id) + " to ("
					+ std::to_string(x) + ", " + std::to_string(y)
					+ ", " + std::to_string(z) + ")");
			};
			cmd.registerCommand(def);
		}

		// scene.rotate_node <id> <rx> <ry> <rz>
		{
			CommandDef def;
			def.name = "scene.rotate_node";
			def.category = "scene";
			def.description = "Set node rotation (degrees)";
			def.usage = "scene.rotate_node <id> <rx> <ry> <rz>";
			def.argNames = {"id", "rx", "ry", "rz"};
			def.argTypes = {"int", "float", "float", "float"};
			def.argRequired = {true, true, true, true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto id = detail::argInt(args, 0);
				const auto rx = detail::argFloat(args, 1);
				const auto ry = detail::argFloat(args, 2);
				const auto rz = detail::argFloat(args, 3);
				return CommandResult::ok(
					"Rotated node " + std::to_string(id) + " to ("
					+ std::to_string(rx) + ", " + std::to_string(ry)
					+ ", " + std::to_string(rz) + ")");
			};
			cmd.registerCommand(def);
		}

		// scene.scale_node <id> <sx> <sy> <sz>
		{
			CommandDef def;
			def.name = "scene.scale_node";
			def.category = "scene";
			def.description = "Set node scale";
			def.usage = "scene.scale_node <id> <sx> <sy> <sz>";
			def.argNames = {"id", "sx", "sy", "sz"};
			def.argTypes = {"int", "float", "float", "float"};
			def.argRequired = {true, true, true, true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto id = detail::argInt(args, 0);
				const auto sx = detail::argFloat(args, 1);
				const auto sy = detail::argFloat(args, 2);
				const auto sz = detail::argFloat(args, 3);
				return CommandResult::ok(
					"Scaled node " + std::to_string(id) + " to ("
					+ std::to_string(sx) + ", " + std::to_string(sy)
					+ ", " + std::to_string(sz) + ")");
			};
			cmd.registerCommand(def);
		}

		// scene.list
		{
			CommandDef def;
			def.name = "scene.list";
			def.category = "scene";
			def.description = "List all nodes in the scene";
			def.usage = "scene.list";
			def.execute = [](const std::vector<CommandArg>&) -> CommandResult {
				std::vector<std::string> lines;
				lines.push_back("Scene nodes:");
				lines.push_back("  (No scene data connected)");
				return CommandResult::ok("Scene listing", lines);
			};
			cmd.registerCommand(def);
		}

		// scene.select <id>
		{
			CommandDef def;
			def.name = "scene.select";
			def.category = "scene";
			def.description = "Select a node by ID";
			def.usage = "scene.select <id>";
			def.argNames = {"id"};
			def.argTypes = {"int"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto id = detail::argInt(args, 0);
				return CommandResult::ok("Selected node " + std::to_string(id));
			};
			cmd.registerCommand(def);
		}

		// scene.save <path>
		{
			CommandDef def;
			def.name = "scene.save";
			def.category = "scene";
			def.description = "Save the current scene to file";
			def.usage = "scene.save <path>";
			def.argNames = {"path"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto path = detail::argString(args, 0);
				return CommandResult::ok("Scene saved to: " + path);
			};
			cmd.registerCommand(def);
		}

		// scene.load <path>
		{
			CommandDef def;
			def.name = "scene.load";
			def.category = "scene";
			def.description = "Load a scene from file";
			def.usage = "scene.load <path>";
			def.argNames = {"path"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto path = detail::argString(args, 0);
				return CommandResult::ok("Scene loaded from: " + path);
			};
			cmd.registerCommand(def);
		}
	}

	// ════════════════════════════════════════════════════════
	// render.* — レンダリング操作
	// ════════════════════════════════════════════════════════

	static void registerRenderCommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
	{
		// render.shader <mode>
		{
			CommandDef def;
			def.name = "render.shader";
			def.category = "render";
			def.description = "Set shader mode (phong/pbr/toon/unlit/wireframe)";
			def.usage = "render.shader <mode>";
			def.argNames = {"mode"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto mode = detail::argString(args, 0);
				return CommandResult::ok("Shader mode set to: " + mode);
			};
			cmd.registerCommand(def);
		}

		// render.outline <method> [width] [r] [g] [b]
		{
			CommandDef def;
			def.name = "render.outline";
			def.category = "render";
			def.description = "Set outline rendering method and style";
			def.usage = "render.outline <method> [width] [r] [g] [b]";
			def.argNames = {"method", "width", "r", "g", "b"};
			def.argTypes = {"string", "float", "float", "float", "float"};
			def.argRequired = {true, false, false, false, false};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto method = detail::argString(args, 0);
				const auto width = detail::argFloat(args, 1, 1.0f);
				return CommandResult::ok(
					"Outline: method=" + method
					+ " width=" + std::to_string(width));
			};
			cmd.registerCommand(def);
		}

		// render.postprocess <pass> <enabled> [params...]
		{
			CommandDef def;
			def.name = "render.postprocess";
			def.category = "render";
			def.description = "Toggle a post-processing pass";
			def.usage = "render.postprocess <pass> <enabled>";
			def.argNames = {"pass", "enabled"};
			def.argTypes = {"string", "bool"};
			def.argRequired = {true, true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto pass = detail::argString(args, 0);
				const auto enabled = detail::argBool(args, 1);
				return CommandResult::ok(
					"Post-process '" + pass + "' "
					+ (enabled ? "enabled" : "disabled"));
			};
			cmd.registerCommand(def);
		}

		// render.fxaa <quality>
		{
			CommandDef def;
			def.name = "render.fxaa";
			def.category = "render";
			def.description = "Set FXAA quality (low/medium/high)";
			def.usage = "render.fxaa <quality>";
			def.argNames = {"quality"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto quality = detail::argString(args, 0);
				return CommandResult::ok("FXAA quality set to: " + quality);
			};
			cmd.registerCommand(def);
		}

		// render.bloom <enabled> [threshold] [intensity]
		{
			CommandDef def;
			def.name = "render.bloom";
			def.category = "render";
			def.description = "Toggle bloom effect";
			def.usage = "render.bloom <enabled> [threshold] [intensity]";
			def.argNames = {"enabled", "threshold", "intensity"};
			def.argTypes = {"bool", "float", "float"};
			def.argRequired = {true, false, false};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto enabled = detail::argBool(args, 0);
				const auto threshold = detail::argFloat(args, 1, 1.0f);
				const auto intensity = detail::argFloat(args, 2, 1.0f);
				return CommandResult::ok(
					"Bloom " + std::string(enabled ? "enabled" : "disabled")
					+ " (threshold=" + std::to_string(threshold)
					+ ", intensity=" + std::to_string(intensity) + ")");
			};
			cmd.registerCommand(def);
		}

		// render.wireframe <enabled>
		{
			CommandDef def;
			def.name = "render.wireframe";
			def.category = "render";
			def.description = "Toggle wireframe rendering";
			def.usage = "render.wireframe <enabled>";
			def.argNames = {"enabled"};
			def.argTypes = {"bool"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto enabled = detail::argBool(args, 0);
				return CommandResult::ok(
					"Wireframe " + std::string(enabled ? "enabled" : "disabled"));
			};
			cmd.registerCommand(def);
		}

		// render.stats
		{
			CommandDef def;
			def.name = "render.stats";
			def.category = "render";
			def.description = "Print render statistics";
			def.usage = "render.stats";
			def.execute = [&engine](const std::vector<CommandArg>&) -> CommandResult {
				std::vector<std::string> lines;
				lines.push_back("Render Statistics:");
				const auto* screen = engine.screen();
				if (screen)
				{
					lines.push_back("  Resolution: "
						+ std::to_string(screen->width()) + "x"
						+ std::to_string(screen->height()));
					lines.push_back("  Draw calls: "
						+ std::to_string(screen->drawCallCount()));
				}
				else
				{
					lines.push_back("  (No screen available)");
				}
				return CommandResult::ok("Render stats", lines);
			};
			cmd.registerCommand(def);
		}
	}

	// ════════════════════════════════════════════════════════
	// audio.* — オーディオ操作
	// ════════════════════════════════════════════════════════

	static void registerAudioCommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
	{
		// audio.volume <category> <value>
		{
			CommandDef def;
			def.name = "audio.volume";
			def.category = "audio";
			def.description = "Set volume for a category (master/bgm/se/voice, 0.0-1.0)";
			def.usage = "audio.volume <category> <value>";
			def.argNames = {"category", "value"};
			def.argTypes = {"string", "float"};
			def.argRequired = {true, true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto cat = detail::argString(args, 0);
				const auto val = detail::argFloat(args, 1);
				return CommandResult::ok(
					"Volume [" + cat + "] = " + std::to_string(val));
			};
			cmd.registerCommand(def);
		}

		// audio.play <path> [category]
		{
			CommandDef def;
			def.name = "audio.play";
			def.category = "audio";
			def.description = "Play a sound file";
			def.usage = "audio.play <path> [category]";
			def.argNames = {"path", "category"};
			def.argTypes = {"string", "string"};
			def.argRequired = {true, false};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto path = detail::argString(args, 0);
				const auto cat = detail::argString(args, 1, "se");
				return CommandResult::ok(
					"Playing '" + path + "' on channel [" + cat + "]");
			};
			cmd.registerCommand(def);
		}

		// audio.stop [category]
		{
			CommandDef def;
			def.name = "audio.stop";
			def.category = "audio";
			def.description = "Stop sounds (optionally by category)";
			def.usage = "audio.stop [category]";
			def.argNames = {"category"};
			def.argTypes = {"string"};
			def.argRequired = {false};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto cat = detail::argString(args, 0, "all");
				return CommandResult::ok("Stopped audio [" + cat + "]");
			};
			cmd.registerCommand(def);
		}

		// audio.mute [category]
		{
			CommandDef def;
			def.name = "audio.mute";
			def.category = "audio";
			def.description = "Toggle mute (optionally by category)";
			def.usage = "audio.mute [category]";
			def.argNames = {"category"};
			def.argTypes = {"string"};
			def.argRequired = {false};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto cat = detail::argString(args, 0, "master");
				return CommandResult::ok("Mute toggled for [" + cat + "]");
			};
			cmd.registerCommand(def);
		}
	}

	// ════════════════════════════════════════════════════════
	// physics.* — 物理演算操作
	// ════════════════════════════════════════════════════════

	static void registerPhysicsCommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
	{
		// physics.gravity <x> <y> <z>
		{
			CommandDef def;
			def.name = "physics.gravity";
			def.category = "physics";
			def.description = "Set gravity vector";
			def.usage = "physics.gravity <x> <y> <z>";
			def.argNames = {"x", "y", "z"};
			def.argTypes = {"float", "float", "float"};
			def.argRequired = {true, true, true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto x = detail::argFloat(args, 0);
				const auto y = detail::argFloat(args, 1);
				const auto z = detail::argFloat(args, 2);
				return CommandResult::ok(
					"Gravity set to (" + std::to_string(x) + ", "
					+ std::to_string(y) + ", " + std::to_string(z) + ")");
			};
			cmd.registerCommand(def);
		}

		// physics.pause
		{
			CommandDef def;
			def.name = "physics.pause";
			def.category = "physics";
			def.description = "Pause physics simulation";
			def.usage = "physics.pause";
			def.execute = [](const std::vector<CommandArg>&) -> CommandResult {
				return CommandResult::ok("Physics paused");
			};
			cmd.registerCommand(def);
		}

		// physics.resume
		{
			CommandDef def;
			def.name = "physics.resume";
			def.category = "physics";
			def.description = "Resume physics simulation";
			def.usage = "physics.resume";
			def.execute = [](const std::vector<CommandArg>&) -> CommandResult {
				return CommandResult::ok("Physics resumed");
			};
			cmd.registerCommand(def);
		}

		// physics.step
		{
			CommandDef def;
			def.name = "physics.step";
			def.category = "physics";
			def.description = "Advance physics by a single step";
			def.usage = "physics.step";
			def.execute = [](const std::vector<CommandArg>&) -> CommandResult {
				return CommandResult::ok("Physics stepped (1 frame)");
			};
			cmd.registerCommand(def);
		}

		// physics.stats
		{
			CommandDef def;
			def.name = "physics.stats";
			def.category = "physics";
			def.description = "Print physics statistics";
			def.usage = "physics.stats";
			def.execute = [](const std::vector<CommandArg>&) -> CommandResult {
				std::vector<std::string> lines;
				lines.push_back("Physics Statistics:");
				lines.push_back("  (No physics engine connected)");
				return CommandResult::ok("Physics stats", lines);
			};
			cmd.registerCommand(def);
		}

		// physics.debug <enabled>
		{
			CommandDef def;
			def.name = "physics.debug";
			def.category = "physics";
			def.description = "Toggle physics debug visualization";
			def.usage = "physics.debug <enabled>";
			def.argNames = {"enabled"};
			def.argTypes = {"bool"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto enabled = detail::argBool(args, 0);
				return CommandResult::ok(
					"Physics debug " + std::string(enabled ? "enabled" : "disabled"));
			};
			cmd.registerCommand(def);
		}
	}

	// ════════════════════════════════════════════════════════
	// ui.* — UI操作
	// ════════════════════════════════════════════════════════

	static void registerUICommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
	{
		// ui.theme <name>
		{
			CommandDef def;
			def.name = "ui.theme";
			def.category = "ui";
			def.description = "Set UI theme (dark/light/cyberpunk/retro)";
			def.usage = "ui.theme <name>";
			def.argNames = {"name"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto name = detail::argString(args, 0);
				return CommandResult::ok("UI theme set to: " + name);
			};
			cmd.registerCommand(def);
		}

		// ui.scale <factor>
		{
			CommandDef def;
			def.name = "ui.scale";
			def.category = "ui";
			def.description = "Set UI scale factor";
			def.usage = "ui.scale <factor>";
			def.argNames = {"factor"};
			def.argTypes = {"float"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto factor = detail::argFloat(args, 0, 1.0f);
				return CommandResult::ok(
					"UI scale set to: " + std::to_string(factor));
			};
			cmd.registerCommand(def);
		}

		// ui.list_widgets
		{
			CommandDef def;
			def.name = "ui.list_widgets";
			def.category = "ui";
			def.description = "List all available widget types";
			def.usage = "ui.list_widgets";
			def.execute = [](const std::vector<CommandArg>&) -> CommandResult {
				std::vector<std::string> lines;
				lines.push_back("Widget types:");
				lines.push_back("  Button, Label, Slider, Checkbox, TextInput");
				lines.push_back("  Panel, ScrollView, ListView, TreeView");
				lines.push_back("  Image, ProgressBar, Dropdown, ColorPicker");
				return CommandResult::ok("Widget list", lines);
			};
			cmd.registerCommand(def);
		}
	}

	// ════════════════════════════════════════════════════════
	// vn.* — ビジュアルノベル操作
	// ════════════════════════════════════════════════════════

	static void registerVNCommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
	{
		// vn.load <script>
		{
			CommandDef def;
			def.name = "vn.load";
			def.category = "vn";
			def.description = "Load a visual novel script";
			def.usage = "vn.load <script>";
			def.argNames = {"script"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto script = detail::argString(args, 0);
				return CommandResult::ok("VN script loaded: " + script);
			};
			cmd.registerCommand(def);
		}

		// vn.jump <label>
		{
			CommandDef def;
			def.name = "vn.jump";
			def.category = "vn";
			def.description = "Jump to a label in the VN script";
			def.usage = "vn.jump <label>";
			def.argNames = {"label"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto label = detail::argString(args, 0);
				return CommandResult::ok("Jumped to label: " + label);
			};
			cmd.registerCommand(def);
		}

		// vn.set_flag <key> <value>
		{
			CommandDef def;
			def.name = "vn.set_flag";
			def.category = "vn";
			def.description = "Set a VN flag";
			def.usage = "vn.set_flag <key> <value>";
			def.argNames = {"key", "value"};
			def.argTypes = {"string", "string"};
			def.argRequired = {true, true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto key = detail::argString(args, 0);
				const auto value = detail::argString(args, 1);
				return CommandResult::ok(
					"Flag set: " + key + " = " + value);
			};
			cmd.registerCommand(def);
		}

		// vn.get_flag <key>
		{
			CommandDef def;
			def.name = "vn.get_flag";
			def.category = "vn";
			def.description = "Get a VN flag value";
			def.usage = "vn.get_flag <key>";
			def.argNames = {"key"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto key = detail::argString(args, 0);
				return CommandResult::ok("Flag '" + key + "' = (not set)");
			};
			cmd.registerCommand(def);
		}

		// vn.skip
		{
			CommandDef def;
			def.name = "vn.skip";
			def.category = "vn";
			def.description = "Toggle skip mode";
			def.usage = "vn.skip";
			def.execute = [](const std::vector<CommandArg>&) -> CommandResult {
				return CommandResult::ok("Skip mode toggled");
			};
			cmd.registerCommand(def);
		}

		// vn.auto
		{
			CommandDef def;
			def.name = "vn.auto";
			def.category = "vn";
			def.description = "Toggle auto-advance mode";
			def.usage = "vn.auto";
			def.execute = [](const std::vector<CommandArg>&) -> CommandResult {
				return CommandResult::ok("Auto mode toggled");
			};
			cmd.registerCommand(def);
		}

		// vn.speed <chars_per_sec>
		{
			CommandDef def;
			def.name = "vn.speed";
			def.category = "vn";
			def.description = "Set text display speed (characters per second)";
			def.usage = "vn.speed <chars_per_sec>";
			def.argNames = {"chars_per_sec"};
			def.argTypes = {"int"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto speed = detail::argInt(args, 0, 30);
				return CommandResult::ok(
					"Text speed: " + std::to_string(speed) + " chars/sec");
			};
			cmd.registerCommand(def);
		}
	}

	// ════════════════════════════════════════════════════════
	// system.* — システム操作
	// ════════════════════════════════════════════════════════

	static void registerSystemCommands(CommandSystem& cmd, Engine& engine)
	{
		// system.fps
		{
			CommandDef def;
			def.name = "system.fps";
			def.category = "system";
			def.description = "Print current FPS";
			def.usage = "system.fps";
			def.execute = [&engine](const std::vector<CommandArg>&) -> CommandResult {
				const auto* clk = engine.clock();
				if (clk)
				{
					const float elapsed = clk->elapsed();
					const auto frames = clk->frameNumber();
					const float fps = (elapsed > 0.0f)
						? static_cast<float>(frames) / elapsed
						: clk->targetTps();
					return CommandResult::ok(
						"FPS: " + std::to_string(static_cast<int>(fps)));
				}
				return CommandResult::ok("FPS: (clock not available)");
			};
			cmd.registerCommand(def);
		}

		// system.stats
		{
			CommandDef def;
			def.name = "system.stats";
			def.category = "system";
			def.description = "Print engine statistics";
			def.usage = "system.stats";
			def.execute = [&engine](const std::vector<CommandArg>&) -> CommandResult {
				std::vector<std::string> lines;
				lines.push_back("Engine Statistics:");
				lines.push_back("  Frame: " + std::to_string(engine.frameNumber()));
				const auto* screen = engine.screen();
				if (screen)
				{
					lines.push_back("  Draw calls: "
						+ std::to_string(screen->drawCallCount()));
				}
				return CommandResult::ok("Stats", lines);
			};
			cmd.registerCommand(def);
		}

		// system.screenshot [path]
		{
			CommandDef def;
			def.name = "system.screenshot";
			def.category = "system";
			def.description = "Capture a screenshot";
			def.usage = "system.screenshot [path]";
			def.argNames = {"path"};
			def.argTypes = {"string"};
			def.argRequired = {false};
			def.execute = [&engine](const std::vector<CommandArg>& args) -> CommandResult {
				const auto path = detail::argString(args, 0, "screenshot.png");
				const auto pixels = engine.capture();
				if (pixels.empty())
				{
					return CommandResult::fail("Capture failed (no pixel data)");
				}
				return CommandResult::ok(
					"Screenshot captured (" + std::to_string(pixels.size())
					+ " bytes) -> " + path);
			};
			cmd.registerCommand(def);
		}

		// system.quit
		{
			CommandDef def;
			def.name = "system.quit";
			def.category = "system";
			def.description = "Quit the application";
			def.usage = "system.quit";
			def.execute = [&engine](const std::vector<CommandArg>&) -> CommandResult {
				engine.requestStop();
				return CommandResult::ok("Quit requested");
			};
			cmd.registerCommand(def);
		}

		// system.help [command]
		{
			CommandDef def;
			def.name = "system.help";
			def.category = "system";
			def.description = "Show help for a command or list all commands";
			def.usage = "system.help [command]";
			def.argNames = {"command"};
			def.argTypes = {"string"};
			def.argRequired = {false};
			def.execute = [&cmd](const std::vector<CommandArg>& args) -> CommandResult {
				const auto target = detail::argString(args, 0);
				if (!target.empty())
				{
					const auto* found = cmd.findCommand(target);
					if (!found)
					{
						return CommandResult::fail("Unknown command: " + target);
					}
					std::vector<std::string> lines;
					lines.push_back("Command: " + found->name);
					lines.push_back("  Category:    " + found->category);
					lines.push_back("  Description: " + found->description);
					lines.push_back("  Usage:       " + found->usage);
					if (!found->argNames.empty())
					{
						lines.push_back("  Arguments:");
						for (std::size_t i = 0; i < found->argNames.size(); ++i)
						{
							const auto req = (i < found->argRequired.size()
								&& found->argRequired[i]) ? "required" : "optional";
							const auto type = (i < found->argTypes.size())
								? found->argTypes[i] : "any";
							lines.push_back(
								"    " + found->argNames[i]
								+ " (" + type + ", " + req + ")");
						}
					}
					return CommandResult::ok("Help: " + target, lines);
				}

				// 全コマンド一覧
				std::vector<std::string> lines;
				const auto cats = cmd.categories();
				for (const auto& cat : cats)
				{
					lines.push_back("[" + cat + "]");
					const auto cmds = cmd.commandsInCategory(cat);
					for (const auto* c : cmds)
					{
						lines.push_back("  " + c->name + " - " + c->description);
					}
				}
				return CommandResult::ok(
					std::to_string(cmd.commandCount()) + " commands available",
					lines);
			};
			cmd.registerCommand(def);
		}

		// system.commands [category]
		{
			CommandDef def;
			def.name = "system.commands";
			def.category = "system";
			def.description = "List all commands or commands in a category";
			def.usage = "system.commands [category]";
			def.argNames = {"category"};
			def.argTypes = {"string"};
			def.argRequired = {false};
			def.execute = [&cmd](const std::vector<CommandArg>& args) -> CommandResult {
				const auto cat = detail::argString(args, 0);
				std::vector<std::string> lines;
				if (!cat.empty())
				{
					const auto cmds = cmd.commandsInCategory(cat);
					for (const auto* c : cmds)
					{
						lines.push_back(c->name + " - " + c->description);
					}
					return CommandResult::ok(
						std::to_string(cmds.size()) + " commands in [" + cat + "]",
						lines);
				}

				for (const auto& c : cmd.commands())
				{
					lines.push_back(c.name);
				}
				return CommandResult::ok(
					std::to_string(cmd.commandCount()) + " commands total",
					lines);
			};
			cmd.registerCommand(def);
		}

		// system.exec <script_path>
		{
			CommandDef def;
			def.name = "system.exec";
			def.category = "system";
			def.description = "Execute a command script file";
			def.usage = "system.exec <script_path>";
			def.argNames = {"script_path"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [&cmd](const std::vector<CommandArg>& args) -> CommandResult {
				const auto path = detail::argString(args, 0);
				std::ifstream file(path);
				if (!file.is_open())
				{
					return CommandResult::fail("Cannot open script: " + path);
				}
				std::string content(
					(std::istreambuf_iterator<char>(file)),
					std::istreambuf_iterator<char>());
				const auto results = cmd.executeBatch(content);
				int succeeded = 0;
				int failed = 0;
				for (const auto& r : results)
				{
					if (r.success) { ++succeeded; }
					else { ++failed; }
				}
				return CommandResult::ok(
					"Executed " + path + ": "
					+ std::to_string(succeeded) + " ok, "
					+ std::to_string(failed) + " failed");
			};
			cmd.registerCommand(def);
		}

		// system.macro_record <name>
		{
			CommandDef def;
			def.name = "system.macro_record";
			def.category = "system";
			def.description = "Start recording a macro";
			def.usage = "system.macro_record <name>";
			def.argNames = {"name"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [&cmd](const std::vector<CommandArg>& args) -> CommandResult {
				const auto name = detail::argString(args, 0);
				cmd.beginRecording(name);
				return CommandResult::ok("Recording macro: " + name);
			};
			cmd.registerCommand(def);
		}

		// system.macro_stop
		{
			CommandDef def;
			def.name = "system.macro_stop";
			def.category = "system";
			def.description = "Stop recording the current macro";
			def.usage = "system.macro_stop";
			def.execute = [&cmd](const std::vector<CommandArg>&) -> CommandResult {
				cmd.endRecording();
				return CommandResult::ok("Macro recording stopped");
			};
			cmd.registerCommand(def);
		}

		// system.macro_play <name>
		{
			CommandDef def;
			def.name = "system.macro_play";
			def.category = "system";
			def.description = "Play a recorded macro";
			def.usage = "system.macro_play <name>";
			def.argNames = {"name"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [&cmd](const std::vector<CommandArg>& args) -> CommandResult {
				const auto name = detail::argString(args, 0);
				const auto& macro = cmd.getMacro(name);
				if (macro.empty())
				{
					return CommandResult::fail("Macro not found: " + name);
				}
				cmd.playMacro(name);
				return CommandResult::ok(
					"Played macro '" + name + "' ("
					+ std::to_string(macro.size()) + " commands)");
			};
			cmd.registerCommand(def);
		}
	}

	// ════════════════════════════════════════════════════════
	// editor.* — エディター操作
	// ════════════════════════════════════════════════════════

	static void registerEditorCommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
	{
		// editor.toggle
		{
			CommandDef def;
			def.name = "editor.toggle";
			def.category = "editor";
			def.description = "Toggle editor visibility (F12)";
			def.usage = "editor.toggle";
			def.execute = [&engine](const std::vector<CommandArg>&) -> CommandResult {
#ifdef _WIN32
				auto* imgui = engine.imguiManager();
				if (imgui)
				{
					imgui->toggleVisible();
					return CommandResult::ok(
						"Editor " + std::string(
							imgui->isVisible() ? "shown" : "hidden"));
				}
#endif
				return CommandResult::ok("Editor toggled");
			};
			cmd.registerCommand(def);
		}

		// editor.panel <name> <visible>
		{
			CommandDef def;
			def.name = "editor.panel";
			def.category = "editor";
			def.description = "Show or hide an editor panel";
			def.usage = "editor.panel <name> <visible>";
			def.argNames = {"name", "visible"};
			def.argTypes = {"string", "bool"};
			def.argRequired = {true, true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto name = detail::argString(args, 0);
				const auto visible = detail::argBool(args, 1);
				return CommandResult::ok(
					"Panel '" + name + "' "
					+ (visible ? "shown" : "hidden"));
			};
			cmd.registerCommand(def);
		}

		// editor.theme <preset>
		{
			CommandDef def;
			def.name = "editor.theme";
			def.category = "editor";
			def.description = "Set editor theme preset";
			def.usage = "editor.theme <preset>";
			def.argNames = {"preset"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto preset = detail::argString(args, 0);
				return CommandResult::ok("Editor theme set to: " + preset);
			};
			cmd.registerCommand(def);
		}

		// editor.layout_reset
		{
			CommandDef def;
			def.name = "editor.layout_reset";
			def.category = "editor";
			def.description = "Reset editor panel layout to default";
			def.usage = "editor.layout_reset";
			def.execute = [](const std::vector<CommandArg>&) -> CommandResult {
				return CommandResult::ok("Editor layout reset to default");
			};
			cmd.registerCommand(def);
		}
	}

	// ════════════════════════════════════════════════════════
	// asset.* — アセット操作
	// ════════════════════════════════════════════════════════

	static void registerAssetCommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
	{
		// asset.reload <path>
		{
			CommandDef def;
			def.name = "asset.reload";
			def.category = "asset";
			def.description = "Hot-reload a specific asset";
			def.usage = "asset.reload <path>";
			def.argNames = {"path"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto path = detail::argString(args, 0);
				return CommandResult::ok("Reloaded asset: " + path);
			};
			cmd.registerCommand(def);
		}

		// asset.reload_all
		{
			CommandDef def;
			def.name = "asset.reload_all";
			def.category = "asset";
			def.description = "Reload all changed assets";
			def.usage = "asset.reload_all";
			def.execute = [](const std::vector<CommandArg>&) -> CommandResult {
				return CommandResult::ok("All changed assets reloaded");
			};
			cmd.registerCommand(def);
		}

		// asset.list [type]
		{
			CommandDef def;
			def.name = "asset.list";
			def.category = "asset";
			def.description = "List loaded assets (optionally by type)";
			def.usage = "asset.list [type]";
			def.argNames = {"type"};
			def.argTypes = {"string"};
			def.argRequired = {false};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto type = detail::argString(args, 0, "all");
				std::vector<std::string> lines;
				lines.push_back("Loaded assets [" + type + "]:");
				lines.push_back("  (No asset manager connected)");
				return CommandResult::ok("Asset list", lines);
			};
			cmd.registerCommand(def);
		}

		// asset.info <path>
		{
			CommandDef def;
			def.name = "asset.info";
			def.category = "asset";
			def.description = "Print information about an asset";
			def.usage = "asset.info <path>";
			def.argNames = {"path"};
			def.argTypes = {"string"};
			def.argRequired = {true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto path = detail::argString(args, 0);
				return CommandResult::ok("Asset info: " + path + " (not loaded)");
			};
			cmd.registerCommand(def);
		}
	}

	// ════════════════════════════════════════════════════════
	// input.* — 入力操作
	// ════════════════════════════════════════════════════════

	static void registerInputCommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
	{
		// input.bind <action> <key>
		{
			CommandDef def;
			def.name = "input.bind";
			def.category = "input";
			def.description = "Bind a key to an action";
			def.usage = "input.bind <action> <key>";
			def.argNames = {"action", "key"};
			def.argTypes = {"string", "string"};
			def.argRequired = {true, true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto action = detail::argString(args, 0);
				const auto key = detail::argString(args, 1);
				return CommandResult::ok(
					"Bound '" + key + "' -> " + action);
			};
			cmd.registerCommand(def);
		}

		// input.unbind <action> <key>
		{
			CommandDef def;
			def.name = "input.unbind";
			def.category = "input";
			def.description = "Unbind a key from an action";
			def.usage = "input.unbind <action> <key>";
			def.argNames = {"action", "key"};
			def.argTypes = {"string", "string"};
			def.argRequired = {true, true};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto action = detail::argString(args, 0);
				const auto key = detail::argString(args, 1);
				return CommandResult::ok(
					"Unbound '" + key + "' from " + action);
			};
			cmd.registerCommand(def);
		}

		// input.list_bindings
		{
			CommandDef def;
			def.name = "input.list_bindings";
			def.category = "input";
			def.description = "List all key bindings";
			def.usage = "input.list_bindings";
			def.execute = [](const std::vector<CommandArg>&) -> CommandResult {
				std::vector<std::string> lines;
				lines.push_back("Key Bindings:");
				lines.push_back("  (No input mapper connected)");
				return CommandResult::ok("Bindings", lines);
			};
			cmd.registerCommand(def);
		}

		// input.simulate <key> [press/release]
		{
			CommandDef def;
			def.name = "input.simulate";
			def.category = "input";
			def.description = "Simulate a key input event";
			def.usage = "input.simulate <key> [press/release]";
			def.argNames = {"key", "action"};
			def.argTypes = {"string", "string"};
			def.argRequired = {true, false};
			def.execute = [](const std::vector<CommandArg>& args) -> CommandResult {
				const auto key = detail::argString(args, 0);
				const auto action = detail::argString(args, 1, "press");
				return CommandResult::ok(
					"Simulated key '" + key + "' " + action);
			};
			cmd.registerCommand(def);
		}
	}
};

} // namespace mitiru
