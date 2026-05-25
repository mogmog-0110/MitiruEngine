#pragma once

/// @file Live2DModel.hpp
/// @brief Live2D model の読み込み / 描画用に CubismUserModel を wrap する
/// @details model3.json の読み込み、texture 管理、physics/motion 更新、描画を
///          高レベル RAII interface として提供する。

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

/// @brief motion priority のレベル
enum class MotionPriority : int
{
    None = 0,
    Idle = 1,
    Normal = 2,
    Force = 3
};

/// @brief 高レベル Live2D model wrapper
/// @details CubismUserModel を所有し、model3.json からの読み込み、
///          texture binding、update/draw cycle、motion、expression を扱う。
class Live2DModel final : public Csm::CubismUserModel
{
public:
    /// @brief model3.json の path から model を構築・読み込みする
    /// @param modelJsonPath .model3.json ファイルへの絶対 path
    explicit Live2DModel(const std::string& modelJsonPath)
    {
        namespace fs = std::filesystem;
        const auto jsonPath = fs::path(modelJsonPath);
        m_modelDir = jsonPath.parent_path().string();
        if (!m_modelDir.empty() && m_modelDir.back() != '/' && m_modelDir.back() != '\\')
        {
            m_modelDir += '/';
        }

        // model setting JSON を読む
        Csm::csmSizeInt jsonSize = 0;
        auto* jsonBuf = LoadFileAsBytes(modelJsonPath, &jsonSize);
        if (!jsonBuf)
        {
            throw std::runtime_error("Failed to load model3.json: " + modelJsonPath);
        }

        m_modelSetting = CSM_NEW Csm::CubismModelSettingJson(jsonBuf, jsonSize);
        ReleaseBytes(jsonBuf);

        // MOC を読む
        loadMoc();

        // texture を読む
        loadTextures();

        // renderer を生成
        CreateRenderer();
        setupRenderer();

        // optional data を読む
        loadPhysics();
        loadPose();
        loadExpressions();
        loadMotions();
        loadUserData();

        // eye blink を設定
        setupEyeBlink();

        // breath を設定
        setupBreath();

        // layout を設定
        setupLayout();

        // model の opacity を設定
        _opacity = 1.0f;
    }

    ~Live2DModel() override
    {
        // texture を解放
        for (auto texId : m_textureIds)
        {
            glDeleteTextures(1, &texId);
        }

        // motion を解放
        for (auto iter = m_motions.Begin(); iter != m_motions.End(); ++iter)
        {
            Csm::ACubismMotion::Delete(iter->Second);
        }
        m_motions.Clear();

        // expression を解放
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

    // copy 禁止
    Live2DModel(const Live2DModel&) = delete;
    Live2DModel& operator=(const Live2DModel&) = delete;

    /// @brief model の状態を更新する
    /// @param deltaTime 前回 update からの経過秒数
    void Update(float deltaTime)
    {
        m_deltaTime = deltaTime;

        _model->LoadParameters();

        // motion を更新
        const bool motionUpdated = _motionManager->IsFinished()
            ? false
            : _motionManager->UpdateMotion(_model, deltaTime);

        _model->SaveParameters();

        // eye blink
        if (!motionUpdated && _eyeBlink)
        {
            _eyeBlink->UpdateParameters(_model, deltaTime);
        }

        // expression
        if (_expressionManager)
        {
            _expressionManager->UpdateMotion(_model, deltaTime);
        }

        // breath
        if (_breath)
        {
            _breath->UpdateParameters(_model, deltaTime);
        }

        // physics
        if (_physics)
        {
            _physics->Evaluate(_model, deltaTime);
        }

        // lip sync
        if (m_lipSyncEnabled)
        {
            const auto count = m_modelSetting->GetLipSyncParameterCount();
            for (Csm::csmInt32 i = 0; i < count; ++i)
            {
                _model->AddParameterValue(
                    m_modelSetting->GetLipSyncParameterId(i), m_lipSyncValue);
            }
        }

        // pose
        if (_pose)
        {
            _pose->UpdateParameters(_model, deltaTime);
        }

        _model->Update();
    }

    /// @brief 与えられた view-projection matrix で model を描画する
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

    /// @brief 名前で expression を設定する
    /// @param name expression 名 (例: "F01")
    void SetExpression(const std::string& name)
    {
        auto* motion = m_expressions[Csm::csmString(name.c_str())];
        if (motion)
        {
            _expressionManager->StartMotionPriority(motion, false,
                static_cast<int>(MotionPriority::Force));
        }
    }

    /// @brief group と index で motion を開始する
    /// @param group motion group 名 (例: "Idle", "TapBody")
    /// @param index group 内の motion index
    /// @param priority motion priority
    /// @return motion queue entry ID
    Csm::CubismMotionQueueEntryHandle StartMotion(
        const std::string& group, int index, MotionPriority priority)
    {
        const auto key = Csm::csmString(group.c_str()) + "_" +
                         std::to_string(index).c_str();
        auto* motion = m_motions[key];

        if (!motion)
        {
            // 必要時に遅延読み込み
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

    /// @brief hit test を行う
    /// @param areaName hit area 名 (例: "Head", "Body")
    /// @param x model 空間の X 座標
    /// @param y model 空間の Y 座標
    /// @return hit area に当たれば true
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

    /// @brief 名前で parameter 値を設定する
    /// @param paramId parameter ID 文字列
    /// @param value parameter 値
    void SetParameterValue(const std::string& paramId, float value)
    {
        auto* id = Csm::CubismFramework::GetIdManager()->GetId(paramId.c_str());
        _model->SetParameterValue(id, value);
    }

    /// @brief model の canvas size を取得する
    /// @return model 単位での (width, height) の pair
    [[nodiscard]] std::pair<float, float> GetCanvasSize() const
    {
        if (!_model) return { 0.0f, 0.0f };
        return { _model->GetCanvasWidth(), _model->GetCanvasHeight() };
    }

    /// @brief lip sync の有効/無効を切り替える
    void SetLipSyncEnabled(bool enabled) noexcept { m_lipSyncEnabled = enabled; }

    /// @brief lip sync 値を設定する (0.0 - 1.0)
    void SetLipSyncValue(float value) noexcept { m_lipSyncValue = value; }

    /// @brief model setting を取得する
    [[nodiscard]] Csm::ICubismModelSetting* GetModelSetting() const noexcept
    {
        return m_modelSetting;
    }

    /// @brief model の directory path を取得する
    [[nodiscard]] const std::string& GetModelDir() const noexcept
    {
        return m_modelDir;
    }

    /// @brief group 名に対する motion 数を取得する
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
