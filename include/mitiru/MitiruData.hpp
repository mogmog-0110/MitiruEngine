#pragma once

/// @file MitiruData.hpp
/// @brief Data, Resource, Asset modules

// Resource
#include <mitiru/resource/AssetHandle.hpp>
#include <mitiru/resource/AssetManager.hpp>
#include <mitiru/resource/AssetPath.hpp>
#include <mitiru/resource/EmbeddedAsset.hpp>
#include <mitiru/resource/FontLoader.hpp>
#include <mitiru/resource/HotReloadManager.hpp>
#include <mitiru/resource/IAssetLoader.hpp>
#include <mitiru/resource/ImageLoader.hpp>

// Asset
#include <mitiru/asset/AssetPipeline.hpp>
#include <mitiru/asset/AssetRegistry.hpp>
#include <mitiru/asset/GameAssetTemplates.hpp>
#include <mitiru/asset/MeshCache.hpp>
#include <mitiru/asset/SvgBuilder.hpp>
#include <mitiru/asset/SvgGenerator.hpp>
#include <mitiru/asset/UiThemeGenerator.hpp>

// Data
#include <mitiru/data/ConfigManager.hpp>
#include <mitiru/data/Json.hpp>
#include <mitiru/data/JsonBuilder.hpp>
#include <mitiru/data/PrefabSystem.hpp>
#include <mitiru/data/ProjectFile.hpp>
#include <mitiru/data/SchemaValidator.hpp>
#include <mitiru/data/TilemapLoader.hpp>
