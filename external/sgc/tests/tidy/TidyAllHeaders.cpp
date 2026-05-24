/// @file TidyAllHeaders.cpp
/// @brief clang-tidy検証用の単一翻訳ユニット
///
/// sgcの全公開ヘッダをインクルードし、clang-tidyの対象とする。
/// DxLib.h依存ヘッダおよびSiv3D依存ヘッダは除外する。

// Core
#include "sgc/core/Assert.hpp"
#include "sgc/core/Coroutine.hpp"
#include "sgc/core/Flags.hpp"
#include "sgc/core/Handle.hpp"
#include "sgc/core/HandleMap.hpp"
#include "sgc/core/Hash.hpp"
#include "sgc/core/JsonReader.hpp"
#include "sgc/core/JsonWriter.hpp"
#include "sgc/core/Logger.hpp"
#include "sgc/core/Profiler.hpp"
#include "sgc/core/Random.hpp"
#include "sgc/core/RingBuffer.hpp"
#include "sgc/core/ScopeGuard.hpp"
#include "sgc/core/Serialize.hpp"
#include "sgc/core/SerializeConcepts.hpp"
#include "sgc/core/StringUtils.hpp"
#include "sgc/core/ThreadPool.hpp"
#include "sgc/core/Timer.hpp"
#include "sgc/core/TypeId.hpp"
#include "sgc/Core.hpp"

// Types
#include "sgc/types/Color.hpp"
#include "sgc/types/Concepts.hpp"
#include "sgc/types/Result.hpp"
#include "sgc/types/TypeList.hpp"
#include "sgc/types/TypeTraits.hpp"
#include "sgc/types/ErrorCode.hpp"

// Math
#include "sgc/math/Easing.hpp"
#include "sgc/math/Frustum.hpp"
#include "sgc/math/Geometry.hpp"
#include "sgc/math/Interpolation.hpp"
#include "sgc/math/Intersection.hpp"
#include "sgc/math/Mat3.hpp"
#include "sgc/math/Mat4.hpp"
#include "sgc/math/MathConstants.hpp"
#include "sgc/math/Noise.hpp"
#include "sgc/math/Plane.hpp"
#include "sgc/math/Quaternion.hpp"
#include "sgc/math/Ray3.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/math/SimdVec.hpp"
#include "sgc/math/Vec2.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/math/Vec4.hpp"

// Memory
#include "sgc/memory/AllocatorConcepts.hpp"
#include "sgc/memory/ArenaAllocator.hpp"
#include "sgc/memory/FreeListAllocator.hpp"
#include "sgc/memory/PoolAllocator.hpp"
#include "sgc/memory/StackAllocator.hpp"
#include "sgc/memory/StlAllocatorAdapter.hpp"

// Patterns
#include "sgc/patterns/Command.hpp"
#include "sgc/patterns/EventDispatcher.hpp"
#include "sgc/patterns/ObjectPool.hpp"
#include "sgc/patterns/Observer.hpp"
#include "sgc/patterns/ServiceLocator.hpp"
#include "sgc/patterns/StateMachine.hpp"

// Animation
#include "sgc/animation/AnimationState.hpp"
#include "sgc/animation/AnimationStateMachine.hpp"
#include "sgc/animation/Tween.hpp"
#include "sgc/animation/TweenTimeline.hpp"

// Spatial
#include "sgc/spatial/BVH2D.hpp"
#include "sgc/spatial/BVH3D.hpp"
#include "sgc/spatial/Grid.hpp"
#include "sgc/spatial/Octree.hpp"
#include "sgc/spatial/Quadtree.hpp"

// AI
#include "sgc/ai/BehaviorTree.hpp"
#include "sgc/ai/Flocking.hpp"
#include "sgc/ai/SteeringBehaviors.hpp"

// ECS
#include "sgc/ecs/CommandBuffer.hpp"
#include "sgc/ecs/ComponentStorage.hpp"
#include "sgc/ecs/Entity.hpp"
#include "sgc/ecs/EntityTemplate.hpp"
#include "sgc/ecs/System.hpp"
#include "sgc/ecs/SystemTraits.hpp"
#include "sgc/ecs/View.hpp"
#include "sgc/ecs/World.hpp"

// Audio (interfaces)
#include "sgc/audio/IAudioPlayer.hpp"

// Graphics (interfaces)
#include "sgc/graphics/IRenderer.hpp"
#include "sgc/graphics/ITextMeasure.hpp"
#include "sgc/graphics/ITextRenderer.hpp"

// Input
#include "sgc/input/ActionMap.hpp"
#include "sgc/input/IInputProvider.hpp"
#include "sgc/input/InputBuffer.hpp"
#include "sgc/input/InputModeManager.hpp"

// Physics
#include "sgc/physics/AABB2DCollision.hpp"
#include "sgc/physics/CollisionFilter.hpp"
#include "sgc/physics/CollisionUtils.hpp"
#include "sgc/physics/FixedTimestep.hpp"
#include "sgc/physics/RayCast2D.hpp"
#include "sgc/physics/RigidBody2D.hpp"
#include "sgc/physics/CollisionResponse.hpp"
#include "sgc/physics/Constraints.hpp"
#include "sgc/physics/PhysicsWorld.hpp"

// Effects
#include "sgc/effects/ParticleSystem.hpp"

// Resource
#include "sgc/resource/AsyncLoadBatch.hpp"
#include "sgc/resource/HotReload.hpp"
#include "sgc/resource/LoadPriority.hpp"
#include "sgc/resource/ResourceHandle.hpp"
#include "sgc/resource/ResourceManager.hpp"

// Scene
#include "sgc/scene/App.hpp"
#include "sgc/scene/Camera.hpp"
#include "sgc/scene/Scene.hpp"
#include "sgc/scene/SceneManager.hpp"
#include "sgc/scene/Transform.hpp"

// Config
#include "sgc/config/ConfigManager.hpp"

// Containers
#include "sgc/containers/FreeList.hpp"
#include "sgc/containers/SparseSet.hpp"

// Debug
#include "sgc/debug/DebugDraw.hpp"
#include "sgc/debug/DebugOverlay.hpp"
#include "sgc/debug/FpsCounter.hpp"
#include "sgc/debug/Tweakable.hpp"

// Net
#include "sgc/net/MessageChannel.hpp"
#include "sgc/net/StateSync.hpp"

// UI
#include "sgc/ui/Anchor.hpp"
#include "sgc/ui/Button.hpp"
#include "sgc/ui/HudLayout.hpp"
#include "sgc/ui/PendingAction.hpp"
#include "sgc/ui/Theme.hpp"
#include "sgc/ui/Slider.hpp"
#include "sgc/ui/Checkbox.hpp"
#include "sgc/ui/ProgressBar.hpp"
#include "sgc/ui/WidgetState.hpp"
#include "sgc/ui/Badge.hpp"
#include "sgc/ui/Panel.hpp"
#include "sgc/ui/RadioButton.hpp"
#include "sgc/ui/Scrollbar.hpp"
#include "sgc/ui/StackLayout.hpp"
#include "sgc/ui/TextLayout.hpp"
#include "sgc/ui/Toast.hpp"
#include "sgc/ui/ToggleSwitch.hpp"
#include "sgc/ui/Tooltip.hpp"
#include "sgc/ui/FocusableWidget.hpp"
#include "sgc/ui/FocusManager.hpp"

// Tilemap
#include "sgc/tilemap/Tilemap.hpp"
#include "sgc/tilemap/AutoTile.hpp"

// Dialogue
#include "sgc/dialogue/DialogueSystem.hpp"
#include "sgc/dialogue/DialogueBuilder.hpp"

// Visual Novel
#include "sgc/vn/TextDisplay.hpp"
#include "sgc/vn/TextEffects.hpp"
#include "sgc/vn/Character.hpp"
#include "sgc/vn/CharacterManager.hpp"
#include "sgc/vn/Backlog.hpp"
#include "sgc/vn/ChoicePresenter.hpp"
#include "sgc/vn/VNScene.hpp"

// Save
#include "sgc/save/SaveSystem.hpp"
#include "sgc/save/SaveMigration.hpp"

// DxLib (TypeConvert only - DxLib.h不要)
#include "sgc/dxlib/TypeConvert.hpp"

// Procedural
#include "sgc/procedural/DungeonGenerator.hpp"
#include "sgc/procedural/TerrainGenerator.hpp"
#include "sgc/procedural/WaveFunctionCollapse.hpp"
#include "sgc/procedural/LSystem.hpp"

// このファイルはclang-tidy検証のためだけに存在する。
int main() { return 0; }
