#pragma once

/// @file Mitiru.hpp
/// @brief Mitiruエンジン アンブレラヘッダー
/// @details Mitiruエンジンの全公開ヘッダーを一括インクルードする。

// Core
#include <mitiru/core/Clock.hpp>
#include <mitiru/core/Config.hpp>
#include <mitiru/core/Engine.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/Screen.hpp>

// Platform
#include <mitiru/platform/IPlatform.hpp>
#include <mitiru/platform/IWindow.hpp>
#include <mitiru/platform/headless/HeadlessPlatform.hpp>

// Platform — Emscripten
#ifdef __EMSCRIPTEN__
#include <mitiru/platform/emscripten/EmscriptenPlatform.hpp>
#include <mitiru/platform/emscripten/EmscriptenWindow.hpp>
#endif

// GFX
#include <mitiru/gfx/GfxFactory.hpp>
#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/IShader.hpp>
#include <mitiru/gfx/ISwapChain.hpp>
#include <mitiru/gfx/ITexture.hpp>
#include <mitiru/gfx/null/NullDevice.hpp>

// GFX — Vulkan
#ifdef MITIRU_HAS_VULKAN
#include <mitiru/gfx/vulkan/VulkanBuffer.hpp>
#include <mitiru/gfx/vulkan/VulkanDevice.hpp>
#include <mitiru/gfx/vulkan/VulkanSwapChain.hpp>
#endif

// GFX — WebGL
#ifdef __EMSCRIPTEN__
#include <mitiru/gfx/webgl/WebGLDevice.hpp>
#endif

// Render
#include <mitiru/render/Camera2D.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/DefaultShaders.hpp>
#include <mitiru/render/RenderGraph.hpp>
#include <mitiru/render/Renderer2D.hpp>
#include <mitiru/render/RenderPipeline2D.hpp>
#include <mitiru/render/ScreenCapture.hpp>
#include <mitiru/render/ShapeRenderer.hpp>
#include <mitiru/render/SpriteBatch.hpp>
#include <mitiru/render/BitmapFont.hpp>
#include <mitiru/render/TextRenderer.hpp>
#include <mitiru/render/Vertex2D.hpp>
#include <mitiru/render/Vertex3D.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Scene3D.hpp>

// Input
#include <mitiru/input/InputInjector.hpp>
#include <mitiru/input/InputRecorder.hpp>
#include <mitiru/input/InputReplayer.hpp>
#include <mitiru/input/InputState.hpp>
#include <mitiru/input/KeyCode.hpp>

// Observe
#include <mitiru/observe/DiffTracker.hpp>
#include <mitiru/observe/Inspector.hpp>
#include <mitiru/observe/ObserveServer.hpp>
#include <mitiru/observe/SemanticLabel.hpp>
#include <mitiru/observe/Snapshot.hpp>
#include <mitiru/observe/SnapshotSchema.hpp>

// Control
#include <mitiru/control/ActionExecutor.hpp>
#include <mitiru/control/CommandQueue.hpp>
#include <mitiru/control/CommandSchema.hpp>
#include <mitiru/control/ReplaySystem.hpp>
#include <mitiru/control/Scripting.hpp>

// Validate
#include <mitiru/validate/CoverageTracker.hpp>
#include <mitiru/validate/HealthCheck.hpp>
#include <mitiru/validate/Invariant.hpp>
#include <mitiru/validate/TestHarness.hpp>

// ECS
#include <mitiru/ecs/ComponentRegistry.hpp>
#include <mitiru/ecs/MitiruWorld.hpp>
#include <mitiru/ecs/Prefab.hpp>
#include <mitiru/ecs/SystemScheduler.hpp>

// Scene
#include <mitiru/scene/MitiruScene.hpp>
#include <mitiru/scene/SceneGraph.hpp>
#include <mitiru/scene/SceneSerializer.hpp>

// Audio
#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/MitiruAudioPlayer.hpp>
#include <mitiru/audio/NullAudioEngine.hpp>
#include <mitiru/audio/SoftAudioEngine.hpp>

// Resource
#include <mitiru/resource/AssetHandle.hpp>
#include <mitiru/resource/AssetManager.hpp>
#include <mitiru/resource/HotReloadManager.hpp>
#include <mitiru/resource/IAssetLoader.hpp>

// Network
#include <mitiru/network/NetworkTypes.hpp>
#include <mitiru/network/INetworkTransport.hpp>
#include <mitiru/network/LocalTransport.hpp>
#include <mitiru/network/Lobby.hpp>
#include <mitiru/network/StateSync.hpp>

// Scripting
#include <mitiru/scripting/IScriptEngine.hpp>
#include <mitiru/scripting/NullScriptEngine.hpp>
#include <mitiru/scripting/ScriptBindings.hpp>

// Debug
#include <mitiru/debug/IDebugOverlay.hpp>
#include <mitiru/debug/NullDebugOverlay.hpp>
#include <mitiru/debug/DebugPanel.hpp>
#include <mitiru/debug/ImGuiOverlay.hpp>

// Bridge
#include <mitiru/bridge/SgcBridge.hpp>
#include <mitiru/bridge/AnimationBridge.hpp>
#include <mitiru/bridge/DialogueBridge.hpp>
#include <mitiru/bridge/EventBridge.hpp>
#include <mitiru/bridge/ParticleBridge.hpp>
#include <mitiru/bridge/SaveBridge.hpp>
#include <mitiru/bridge/TransitionBridge.hpp>
