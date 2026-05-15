#pragma once

/// @file Live2DModel.hpp
/// @brief Wraps CubismUserModel for loading and rendering Live2D models
/// @details Provides a high-level RAII interface to load model3.json,
///          manage textures, update physics/motions, and render.

#ifdef MITIRU_HAS_CUBISM

#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <GL/gl.h>

#include <CubismFramework.hpp>
#include <CubismModelSettingJson.hpp>
#include <CubismDefaultParameterId.hpp>
#include <Model/CubismUserModel.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionQueueManager.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <Math/CubismMatrix44.hpp>
#include <Id/CubismIdManager.hpp>
#include <Effect/CubismBreath.hpp>
#include <Effect/CubismEyeBlink.hpp>
#include <Type/csmVector.hpp>
#include <Type/csmMap.hpp>

#include <mitiru/live2d/Live2DRenderer.hpp>

namespace mitiru::live2d
{

/// @brief Motion priority levels
enum class MotionPriority : int
{
    None = 0,
    Idle = 1,
    Normal = 2,
    Force = 3
};

/// @brief High-level Live2D model wrapper
/// @details Owns a CubismUserModel, handles loading from model3.json,
///          texture binding, update/draw cycle, motions, and expressions.
class Live2DModel final : public Csm::CubismUserModel
{
public:
    /// @brief Construct and load model from model3.json path
    /// @param modelJsonPath Full path to .model3.json file
    explicit Live2DModel(const std::string& modelJsonPath)
    {
        namespace fs = std::filesystem;
        const auto jsonPath = fs::path(modelJsonPath);
        m_modelDir = jsonPath.parent_path().string();
        if (!m_modelDir.empty() && m_modelDir.back() != '/' && m_modelDir.back() != '\\')
        {
            m_modelDir += '/';
        }

        // Load model setting JSON
        Csm::csmSizeInt jsonSize = 0;
        auto* jsonBuf = LoadFileAsBytes(modelJsonPath, &jsonSize);
        if (!jsonBuf)
        {
            throw std::runtime_error("Failed to load model3.json: " + modelJsonPath);
        }

        m_modelSetting = CSM_NEW Csm::CubismModelSettingJson(jsonBuf, jsonSize);
        ReleaseBytes(jsonBuf);

        // Load MOC
        loadMoc();

        // Load textures
        loadTextures();

        // Create renderer
        CreateRenderer();
        setupRenderer();

        // Load optional data
        loadPhysics();
        loadPose();
        loadExpressions();
        loadMotions();
        loadUserData();

        // Setup eye blink
        setupEyeBlink();

        // Setup breath
        setupBreath();

        // Setup layout
        setupLayout();

        // Set model opacity
        _opacity = 1.0f;
    }

    ~Live2DModel() override
    {
        // Release textures
        for (auto texId : m_textureIds)
        {
            glDeleteTextures(1, &texId);
        }

        // Release motions
        for (auto iter = m_motions.Begin(); iter != m_motions.End(); ++iter)
        {
            Csm::ACubismMotion::Delete(iter->Second);
        }
        m_motions.Clear();

        // Release expressions
        for (auto iter = m_expressions.Begin(); iter != m_expressions.End(); ++iter)
        {
            Csm::ACubismMotion::Delete(iter->Second);
        }
        m_expressions.Clear();

        if (m_modelSetting)
        {
            CSM_DELETE(m_modelSetting);
            m_modelSetting = nullptr;
        }

        DeleteRenderer();
    }

    // Non-copyable
    Live2DModel(const Live2DModel&) = delete;
    Live2DModel& operator=(const Live2DModel&) = delete;

    /// @brief Update model state
    /// @param deltaTime Time elapsed since last update in seconds
    void Update(float deltaTime)
    {
        m_deltaTime = deltaTime;

        _model->LoadParameters();

        // Update motions
        const bool motionUpdated = _motionManager->IsFinished()
            ? false
            : _motionManager->UpdateMotion(_model, deltaTime);

        _model->SaveParameters();

        // Eye blink
        if (!motionUpdated && _eyeBlink)
        {
            _eyeBlink->UpdateParameters(_model, deltaTime);
        }

        // Expression
        if (_expressionManager)
        {
            _expressionManager->UpdateMotion(_model, deltaTime);
        }

        // Breath
        if (_breath)
        {
            _breath->UpdateParameters(_model, deltaTime);
        }

        // Physics
        if (_physics)
        {
            _physics->Evaluate(_model, deltaTime);
        }

        // Lip sync
        if (m_lipSyncEnabled)
        {
            const auto count = m_modelSetting->GetLipSyncParameterCount();
            for (Csm::csmInt32 i = 0; i < count; ++i)
            {
                _model->AddParameterValue(
                    m_modelSetting->GetLipSyncParameterId(i), m_lipSyncValue);
            }
        }

        // Pose
        if (_pose)
        {
            _pose->UpdateParameters(_model, deltaTime);
        }

        _model->Update();
    }

    /// @brief Draw the model with given view-projection matrix
    /// @param viewProjection 4x4 view-projection matrix
    void Draw(Csm::CubismMatrix44& viewProjection)
    {
        auto* renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
        if (!renderer) return;

        Csm::CubismMatrix44 projection;
        projection.SetMatrix(viewProjection.GetArray());

        if (_modelMatrix)
        {
            projection.MultiplyByMatrix(_modelMatrix);
        }

        renderer->SetMvpMatrix(&projection);
        renderer->DrawModel();
    }

    /// @brief Set an expression by name
    /// @param name Expression name (e.g. "F01")
    void SetExpression(const std::string& name)
    {
        auto* motion = m_expressions[Csm::csmString(name.c_str())];
        if (motion)
        {
            _expressionManager->StartMotionPriority(motion, false,
                static_cast<int>(MotionPriority::Force));
        }
    }

    /// @brief Start a motion by group and index
    /// @param group Motion group name (e.g. "Idle", "TapBody")
    /// @param index Motion index within group
    /// @param priority Motion priority
    /// @return Motion queue entry ID
    Csm::CubismMotionQueueEntryHandle StartMotion(
        const std::string& group, int index, MotionPriority priority)
    {
        const auto key = Csm::csmString(group.c_str()) + "_" +
                         std::to_string(index).c_str();
        auto* motion = m_motions[key];

        if (!motion)
        {
            // Load on demand
            const auto fileName = m_modelSetting->GetMotionFileName(
                group.c_str(), index);
            if (!fileName || std::strlen(fileName) == 0) return nullptr;

            const auto path = m_modelDir + fileName;
            Csm::csmSizeInt size = 0;
            auto* buf = LoadFileAsBytes(path, &size);
            if (!buf) return nullptr;

            motion = LoadMotion(buf, size, key.GetRawString());
            ReleaseBytes(buf);

            if (motion)
            {
                const auto fadeIn = m_modelSetting->GetMotionFadeInTimeValue(
                    group.c_str(), index);
                const auto fadeOut = m_modelSetting->GetMotionFadeOutTimeValue(
                    group.c_str(), index);
                if (fadeIn >= 0.0f) motion->SetFadeInTime(fadeIn);
                if (fadeOut >= 0.0f) motion->SetFadeOutTime(fadeOut);

                m_motions[key] = motion;
            }
        }

        return _motionManager->StartMotionPriority(
            motion, false, static_cast<int>(priority));
    }

    /// @brief Perform hit test
    /// @param areaName Hit area name (e.g. "Head", "Body")
    /// @param x X coordinate in model space
    /// @param y Y coordinate in model space
    /// @return true if the hit area was hit
    bool HitTest(const std::string& areaName, float x, float y)
    {
        const auto count = m_modelSetting->GetHitAreasCount();
        for (Csm::csmInt32 i = 0; i < count; ++i)
        {
            const auto name = m_modelSetting->GetHitAreaName(i);
            if (areaName == name)
            {
                const auto drawId = m_modelSetting->GetHitAreaId(i);
                return IsHit(drawId, x, y);
            }
        }
        return false;
    }

    /// @brief Set a parameter value by name
    /// @param paramId Parameter ID string
    /// @param value Parameter value
    void SetParameterValue(const std::string& paramId, float value)
    {
        auto* id = Csm::CubismFramework::GetIdManager()->GetId(paramId.c_str());
        _model->SetParameterValue(id, value);
    }

    /// @brief Get canvas size of the model
    /// @return Pair of (width, height) in model units
    [[nodiscard]] std::pair<float, float> GetCanvasSize() const
    {
        if (!_model) return { 0.0f, 0.0f };
        return { _model->GetCanvasWidth(), _model->GetCanvasHeight() };
    }

    /// @brief Enable or disable lip sync
    void SetLipSyncEnabled(bool enabled) noexcept { m_lipSyncEnabled = enabled; }

    /// @brief Set lip sync value (0.0 - 1.0)
    void SetLipSyncValue(float value) noexcept { m_lipSyncValue = value; }

    /// @brief Get the model setting
    [[nodiscard]] Csm::ICubismModelSetting* GetModelSetting() const noexcept
    {
        return m_modelSetting;
    }

    /// @brief Get the model directory path
    [[nodiscard]] const std::string& GetModelDir() const noexcept
    {
        return m_modelDir;
    }

    /// @brief Get motion group count for a group name
    [[nodiscard]] int GetMotionCount(const std::string& group) const
    {
        return m_modelSetting->GetMotionCount(group.c_str());
    }

private:
    void loadMoc()
    {
        const auto mocFileName = m_modelSetting->GetModelFileName();
        if (!mocFileName || std::strlen(mocFileName) == 0)
        {
            throw std::runtime_error("No MOC file specified in model3.json");
        }

        const auto mocPath = m_modelDir + mocFileName;
        Csm::csmSizeInt size = 0;
        auto* buf = LoadFileAsBytes(mocPath, &size);
        if (!buf)
        {
            throw std::runtime_error("Failed to load MOC file: " + mocPath);
        }

        LoadModel(buf, size, true);
        ReleaseBytes(buf);
    }

    void loadTextures()
    {
        const auto textureCount = m_modelSetting->GetTextureCount();
        m_textureIds.reserve(static_cast<std::size_t>(textureCount));

        for (Csm::csmInt32 i = 0; i < textureCount; ++i)
        {
            const auto texFileName = m_modelSetting->GetTextureFileName(i);
            if (!texFileName || std::strlen(texFileName) == 0) continue;

            const auto texPath = m_modelDir + texFileName;
            auto texId = LoadTextureFromFile(texPath);
            m_textureIds.push_back(texId);
        }
    }

    void setupRenderer()
    {
        auto* renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
        if (!renderer) return;

        renderer->Initialize(_model);
        renderer->IsPremultipliedAlpha(true);

        for (std::size_t i = 0; i < m_textureIds.size(); ++i)
        {
            renderer->BindTexture(static_cast<Csm::csmUint32>(i), m_textureIds[i]);
        }
    }

    void loadPhysics()
    {
        const auto physicsFileName = m_modelSetting->GetPhysicsFileName();
        if (!physicsFileName || std::strlen(physicsFileName) == 0) return;

        const auto path = m_modelDir + physicsFileName;
        Csm::csmSizeInt size = 0;
        auto* buf = LoadFileAsBytes(path, &size);
        if (!buf) return;

        LoadPhysics(buf, size);
        ReleaseBytes(buf);
    }

    void loadPose()
    {
        const auto poseFileName = m_modelSetting->GetPoseFileName();
        if (!poseFileName || std::strlen(poseFileName) == 0) return;

        const auto path = m_modelDir + poseFileName;
        Csm::csmSizeInt size = 0;
        auto* buf = LoadFileAsBytes(path, &size);
        if (!buf) return;

        LoadPose(buf, size);
        ReleaseBytes(buf);
    }

    void loadExpressions()
    {
        const auto count = m_modelSetting->GetExpressionCount();
        for (Csm::csmInt32 i = 0; i < count; ++i)
        {
            const auto name = m_modelSetting->GetExpressionName(i);
            const auto fileName = m_modelSetting->GetExpressionFileName(i);
            if (!fileName || std::strlen(fileName) == 0) continue;

            const auto path = m_modelDir + fileName;
            Csm::csmSizeInt size = 0;
            auto* buf = LoadFileAsBytes(path, &size);
            if (!buf) continue;

            auto* expression = LoadExpression(buf, size, name);
            ReleaseBytes(buf);

            if (expression)
            {
                m_expressions[Csm::csmString(name)] = expression;
            }
        }
    }

    void loadMotions()
    {
        const auto groupCount = m_modelSetting->GetMotionGroupCount();
        for (Csm::csmInt32 g = 0; g < groupCount; ++g)
        {
            const auto groupName = m_modelSetting->GetMotionGroupName(g);
            const auto motionCount = m_modelSetting->GetMotionCount(groupName);

            for (Csm::csmInt32 i = 0; i < motionCount; ++i)
            {
                const auto fileName = m_modelSetting->GetMotionFileName(groupName, i);
                if (!fileName || std::strlen(fileName) == 0) continue;

                const auto path = m_modelDir + fileName;
                Csm::csmSizeInt size = 0;
                auto* buf = LoadFileAsBytes(path, &size);
                if (!buf) continue;

                const auto key = Csm::csmString(groupName) + "_" +
                                 std::to_string(i).c_str();
                auto* motion = LoadMotion(buf, size, key.GetRawString());
                ReleaseBytes(buf);

                if (motion)
                {
                    const auto fadeIn = m_modelSetting->GetMotionFadeInTimeValue(
                        groupName, i);
                    const auto fadeOut = m_modelSetting->GetMotionFadeOutTimeValue(
                        groupName, i);
                    if (fadeIn >= 0.0f) motion->SetFadeInTime(fadeIn);
                    if (fadeOut >= 0.0f) motion->SetFadeOutTime(fadeOut);

                    m_motions[key] = motion;
                }
            }
        }
    }

    void loadUserData()
    {
        const auto userDataFile = m_modelSetting->GetUserDataFile();
        if (!userDataFile || std::strlen(userDataFile) == 0) return;

        const auto path = m_modelDir + userDataFile;
        Csm::csmSizeInt size = 0;
        auto* buf = LoadFileAsBytes(path, &size);
        if (!buf) return;

        LoadUserData(buf, size);
        ReleaseBytes(buf);
    }

    void setupEyeBlink()
    {
        const auto count = m_modelSetting->GetEyeBlinkParameterCount();
        if (count <= 0) return;

        _eyeBlink = Csm::CubismEyeBlink::Create(m_modelSetting);
    }

    void setupBreath()
    {
        _breath = Csm::CubismBreath::Create();

        Csm::csmVector<Csm::CubismBreath::BreathParameterData> breathParams;

        auto* idParamAngleX = Csm::CubismFramework::GetIdManager()->GetId(
            Csm::DefaultParameterId::ParamAngleX);
        auto* idParamAngleY = Csm::CubismFramework::GetIdManager()->GetId(
            Csm::DefaultParameterId::ParamAngleY);
        auto* idParamAngleZ = Csm::CubismFramework::GetIdManager()->GetId(
            Csm::DefaultParameterId::ParamAngleZ);
        auto* idParamBodyAngleX = Csm::CubismFramework::GetIdManager()->GetId(
            Csm::DefaultParameterId::ParamBodyAngleX);
        auto* idParamBreath = Csm::CubismFramework::GetIdManager()->GetId(
            Csm::DefaultParameterId::ParamBreath);

        breathParams.PushBack(Csm::CubismBreath::BreathParameterData(
            idParamAngleX, 0.0f, 15.0f, 6.5345f, 0.5f));
        breathParams.PushBack(Csm::CubismBreath::BreathParameterData(
            idParamAngleY, 0.0f, 8.0f, 3.5345f, 0.5f));
        breathParams.PushBack(Csm::CubismBreath::BreathParameterData(
            idParamAngleZ, 0.0f, 10.0f, 5.5345f, 0.5f));
        breathParams.PushBack(Csm::CubismBreath::BreathParameterData(
            idParamBodyAngleX, 0.0f, 4.0f, 15.5345f, 0.5f));
        breathParams.PushBack(Csm::CubismBreath::BreathParameterData(
            idParamBreath, 0.5f, 0.5f, 3.2345f, 0.5f));

        _breath->SetParameters(breathParams);
    }

    void setupLayout()
    {
        Csm::csmMap<Csm::csmString, Csm::csmFloat32> layout;
        if (m_modelSetting->GetLayoutMap(layout) && _modelMatrix)
        {
            _modelMatrix->SetupFromLayout(layout);
        }
    }

    std::string m_modelDir;
    Csm::CubismModelSettingJson* m_modelSetting = nullptr;
    std::vector<GLuint> m_textureIds;
    Csm::csmMap<Csm::csmString, Csm::ACubismMotion*> m_motions;
    Csm::csmMap<Csm::csmString, Csm::ACubismMotion*> m_expressions;
    float m_deltaTime = 0.0f;
    bool m_lipSyncEnabled = false;
    float m_lipSyncValue = 0.0f;
};

} // namespace mitiru::live2d

#endif // MITIRU_HAS_CUBISM
