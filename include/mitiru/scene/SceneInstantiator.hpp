#pragma once

/// @file SceneInstantiator.hpp
/// @brief SceneDocument (JSON編集用データモデル) と GameWorld (ECS) を繋ぐ変換層
/// @details PhysicsTrait/MeshTrait 等の Trait を対応する GameWorld Component へ写す。
///          対応する Component が存在しない Trait は warning に積んで無視する（落とさない）。

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "mitiru/core/SceneDocument.hpp"
#include "mitiru/physics/PhysicsSystem3D.hpp"
#include "mitiru/scene/GameWorld.hpp"

namespace mitiru::scene
{

/// @brief instantiate() の結果。生成したエンティティ数と未対応 Trait の警告
struct InstantiateResult
{
	std::size_t entityCount = 0;      ///< 生成したエンティティ数
	std::vector<std::string> warnings; ///< 対応する Component が無かった Trait の警告
};

namespace detail
{

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

/// @brief PhysicsTrait::colliderType 文字列を physics3d::ColliderType3D へ変換する
/// @details 未知の値（"mesh"/"none" 含む）は AABB にフォールバックし warning を積む。
///          ECS 側の RigidBodyComponent3D にメッシュコライダーや無コライダーの表現が無いため。
inline physics3d::ColliderType3D mapColliderType(
	const std::string& colliderType, const std::string& nodeName, std::vector<std::string>& warnings)
{
	if (colliderType == "box") return physics3d::ColliderType3D::AABB;
	if (colliderType == "sphere") return physics3d::ColliderType3D::Sphere;
	if (colliderType == "capsule") return physics3d::ColliderType3D::Capsule;

	warnings.push_back(
		"node '" + nodeName + "': colliderType '" + colliderType + "' は未対応。AABB として扱う");
	return physics3d::ColliderType3D::AABB;
}

/// @brief PhysicsTrait から RigidBodyComponent3D を組み立てて GameWorld へ追加する
inline void instantiatePhysics(const PhysicsTrait& trait, const std::string& nodeName,
	EntityId entityId, GameWorld& world, std::vector<std::string>& warnings)
{
	physics3d::RigidBodyComponent3D rb;

	rb.isKinematic = (trait.bodyType == "kinematic");
	rb.mass = (rb.isKinematic || trait.bodyType == "static") ? 0.0f : trait.mass;
	rb.restitution = trait.restitution;
	rb.friction = trait.friction;
	rb.isTrigger = trait.isTrigger;

	rb.colliderType = mapColliderType(trait.colliderType, nodeName, warnings);
	switch (rb.colliderType)
	{
	case physics3d::ColliderType3D::Sphere:
		rb.colliderRadius = trait.colliderSize[0];
		break;
	case physics3d::ColliderType3D::AABB:
		rb.colliderHalfExtents = sgc::Vec3f{
			trait.colliderSize[0], trait.colliderSize[1], trait.colliderSize[2]};
		break;
	case physics3d::ColliderType3D::Capsule:
		rb.colliderRadius = trait.colliderSize[0];
		rb.capsuleHeight = trait.colliderSize[1];
		break;
	}

	if (trait.colliderOffset[0] != 0.0f || trait.colliderOffset[1] != 0.0f ||
		trait.colliderOffset[2] != 0.0f)
	{
		warnings.push_back(
			"node '" + nodeName + "': colliderOffset は RigidBodyComponent3D に対応フィールドが無く無視した");
	}

	trait.applyCollisionLayerMask(rb);

	world.addComponent(entityId, rb);
}

} // namespace detail

/// @brief SceneDocument の全ノードを GameWorld のエンティティへ変換する
/// @details Transform は全ノードへ必ず付与する。Mesh/Light/Camera/Physics/Audio は
///          対応する Trait があれば同名 Component へ写す。Script/Custom Trait は
///          GameWorld 側に対応する Component が無いため warning に積んで無視する。
[[nodiscard]] inline InstantiateResult instantiate(const Scene& doc, GameWorld& world)
{
	InstantiateResult result;

	for (const auto& node : doc.nodes())
	{
		const EntityId entityId = world.createEntity(node.name);
		++result.entityCount;

		TransformComponent transform;
		transform.position = sgc::Vec3f{node.position[0], node.position[1], node.position[2]};
		transform.rotation = sgc::Vec3f{
			node.rotation[0] * detail::kDegToRad,
			node.rotation[1] * detail::kDegToRad,
			node.rotation[2] * detail::kDegToRad};
		transform.scale = sgc::Vec3f{node.scale[0], node.scale[1], node.scale[2]};
		world.addComponent(entityId, transform);

		if (const auto* mesh = node.getTrait<MeshTrait>())
		{
			world.addComponent(entityId, MeshComponent{mesh->meshPath});
		}

		if (const auto* light = node.getTrait<LightTrait>())
		{
			LightComponent lightComponent;
			lightComponent.type = (light->lightType == "point")  ? LightType::Point
			                     : (light->lightType == "spot")  ? LightType::Spot
			                                                      : LightType::Directional;
			lightComponent.color = sgc::Vec3f{light->color[0], light->color[1], light->color[2]};
			lightComponent.intensity = light->intensity;
			lightComponent.range = light->range;
			world.addComponent(entityId, lightComponent);
		}

		if (const auto* camera = node.getTrait<CameraTrait>())
		{
			CameraComponent cameraComponent;
			cameraComponent.fov = camera->fov;
			cameraComponent.nearClip = camera->nearClip;
			cameraComponent.farClip = camera->farClip;
			world.addComponent(entityId, cameraComponent);
		}

		if (const auto* physics = node.getTrait<PhysicsTrait>())
		{
			detail::instantiatePhysics(*physics, node.name, entityId, world, result.warnings);
		}

		if (const auto* audio = node.getTrait<AudioTrait>())
		{
			AudioSourceComponent audioComponent;
			audioComponent.soundId = audio->audioPath;
			audioComponent.volume = audio->volume;
			audioComponent.loop = audio->loop;
			world.addComponent(entityId, audioComponent);
		}

		if (node.hasTrait<ScriptTrait>())
		{
			result.warnings.push_back(
				"node '" + node.name + "': script trait に対応する GameWorld Component が無いため無視した");
		}

		if (const auto* custom = node.getTrait<CustomTrait>())
		{
			result.warnings.push_back(
				"node '" + node.name + "': custom trait '" + custom->traitType() +
				"' に対応する GameWorld Component が無いため無視した");
		}
	}

	return result;
}

/// @brief GameWorld のエンティティ状態を SceneDocument へ書き戻す（Transform / Physics のみ）
/// @details エンティティと Node の対応は instantiate() が付けたエンティティ名 = ノード名で引く
///          （明示的な id マッピングをまだ持たないため、名前重複がある場合は最初に見つかった
///          ノードへ書き戻す）。対応する Node が見つからないエンティティは無視する。
inline void capture(GameWorld& world, Scene& doc)
{
	world.forEach<TransformComponent>(
		[&world, &doc](EntityId entityId, TransformComponent& transform)
		{
			const std::string name = world.entityName(entityId);
			if (name.empty()) return;

			const int nodeId = doc.findByName(name);
			auto* node = doc.getNode(nodeId);
			if (!node) return;

			node->position[0] = transform.position.x;
			node->position[1] = transform.position.y;
			node->position[2] = transform.position.z;
			node->rotation[0] = transform.rotation.x * detail::kRadToDeg;
			node->rotation[1] = transform.rotation.y * detail::kRadToDeg;
			node->rotation[2] = transform.rotation.z * detail::kRadToDeg;
			node->scale[0] = transform.scale.x;
			node->scale[1] = transform.scale.y;
			node->scale[2] = transform.scale.z;

			if (const auto* rb = world.getComponent<physics3d::RigidBodyComponent3D>(entityId))
			{
				auto* physicsTrait = node->getTrait<PhysicsTrait>();
				if (!physicsTrait) physicsTrait = &node->addTrait<PhysicsTrait>();

				physicsTrait->bodyType =
					rb->isKinematic ? "kinematic" : (rb->mass <= 0.0f ? "static" : "dynamic");
				physicsTrait->mass = rb->mass;
				physicsTrait->friction = rb->friction;
				physicsTrait->restitution = rb->restitution;
				physicsTrait->isTrigger = rb->isTrigger;
				physicsTrait->captureCollisionLayerMask(*rb);
			}
		});
}

} // namespace mitiru::scene
