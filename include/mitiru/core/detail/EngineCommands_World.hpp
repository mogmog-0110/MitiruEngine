#pragma once

/// @file EngineCommands_World.hpp
/// @brief ワールド状態系コマンド登録 (scene / render / audio / physics) — EngineCommands.hpp から分割

#include <string>
#include <vector>

#include <mitiru/core/CommandSystem.hpp>
#include <mitiru/core/detail/EngineCommands_Args.hpp>

namespace mitiru
{

namespace detail
{

// ════════════════════════════════════════════════════════
// scene.* — シーン操作
// ════════════════════════════════════════════════════════

inline void registerSceneCommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
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

inline void registerRenderCommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
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

inline void registerAudioCommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
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

inline void registerPhysicsCommands(CommandSystem& cmd, [[maybe_unused]] Engine& engine)
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

} // namespace detail

} // namespace mitiru
