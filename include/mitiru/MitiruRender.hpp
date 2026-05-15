#pragma once

/// @file MitiruRender.hpp
/// @brief Render module

#include <mitiru/render/AlphaBlend.hpp>
#include <mitiru/render/AntiAlias.hpp>
#include <mitiru/render/Billboard3D.hpp>
#include <mitiru/render/BitmapFont.hpp>
#include <mitiru/render/Camera2D.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/DefaultShaders.hpp>
#include <mitiru/render/DefaultShaders3D.hpp>
#include <mitiru/render/FontRenderer.hpp>
#include <mitiru/render/GlmBridge.hpp>
#include <mitiru/render/GltfMaterialIntegration.hpp>
#include <mitiru/render/GpuSpriteBatch.hpp>
#include <mitiru/render/GradientRenderer.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/MeshNormalizer.hpp>
#include <mitiru/render/NPRShaders3D.hpp>
#include <mitiru/render/ObjLoader.hpp>
#include <mitiru/render/ObjLoaderTiny.hpp>
#include <mitiru/render/ParticleSystem3D.hpp>
#include <mitiru/render/Pipeline3D.hpp>
#include <mitiru/render/PostEffects.hpp>
#include <mitiru/render/PostProcess.hpp>
#include <mitiru/render/RenderGraph.hpp>
#include <mitiru/render/RenderPipeline2D.hpp>
#include <mitiru/render/RenderState3D.hpp>
#include <mitiru/render/Renderer2D.hpp>
#include <mitiru/render/Renderer2DBridge.hpp>
#include <mitiru/render/Renderer3D.hpp>
#include <mitiru/render/Scene3D.hpp>
#include <mitiru/render/ScenePlacer.hpp>
#include <mitiru/render/ScreenCapture.hpp>
#include <mitiru/render/ScreenEnhanced.hpp>
#include <mitiru/render/SdfFont.hpp>
#include <mitiru/render/ShadowPass3D.hpp>
#include <mitiru/render/ShapeRenderer.hpp>
#include <mitiru/render/SkeletalAnimation.hpp>
#include <mitiru/render/SpriteBatch.hpp>
#include <mitiru/render/TextRenderer.hpp>
#include <mitiru/render/TextureFilter.hpp>
#include <mitiru/render/TransparencySort.hpp>
#include <mitiru/render/TrueTypeRenderer.hpp>
#include <mitiru/render/UiShapes.hpp>
#include <mitiru/render/Vertex2D.hpp>
#include <mitiru/render/Vertex3D.hpp>
#include <mitiru/render/VisualPresets.hpp>
#include <mitiru/render/WorldUI.hpp>

#ifdef _WIN32
#include <mitiru/render/DxTextRenderer.hpp>
#endif
