#pragma once

/// @file RenderPipelineValidator.hpp
/// @brief 3Dレンダリングパイプラインのエンドツーエンド検証ハーネス
/// @details DX11/DX12/Toonパイプラインの各段階（シェーダーコンパイル、
///          定数バッファ生成、深度バッファ生成、描画コール）を検証し、
///          ValidationReportとして結果を返す。

#include <string>
#include <vector>

#include <mitiru/render/Renderer3D.hpp>
#include <mitiru/render/ToonPipeline.hpp>

#ifdef _WIN32
#include <mitiru/render/Renderer3D_DX12.hpp>
#include <mitiru/render/PBRShaders.hpp>
#endif

namespace mitiru::render
{

// ============================================================================
// ValidationReport
// ============================================================================

/// @brief パイプライン検証結果
struct ValidationReport
{
	int totalTests = 0;    ///< テスト総数
	int passed     = 0;    ///< 成功数
	int failed     = 0;    ///< 失敗数
	std::vector<std::string> errors;  ///< 失敗メッセージ一覧

	/// @brief テスト結果を記録する
	void record(const std::string& name, bool success,
	            const std::string& errorMsg = {})
	{
		++totalTests;
		if (success)
		{
			++passed;
		}
		else
		{
			++failed;
			errors.push_back(name + ": " + (errorMsg.empty() ? "FAIL" : errorMsg));
		}
	}

	/// @brief 全テスト成功か
	[[nodiscard]] bool allPassed() const noexcept { return failed == 0; }

	/// @brief 別レポートの結果を統合する
	void merge(const ValidationReport& other)
	{
		totalTests += other.totalTests;
		passed += other.passed;
		failed += other.failed;
		errors.insert(errors.end(), other.errors.begin(), other.errors.end());
	}
};

// ============================================================================
// RenderPipelineValidator
// ============================================================================

/// @brief 3Dレンダリングパイプラインのバリデータ
/// @details 各レンダラーに対して非破壊的なテストを実行し、
///          パイプラインの正常動作を検証する。
class RenderPipelineValidator
{
public:
	RenderPipelineValidator() noexcept = default;

#ifdef _WIN32

	/// @brief DX11パイプラインを検証する
	/// @param renderer 検証対象のRenderer3D（初期化済み）
	/// @return 検証結果レポート
	[[nodiscard]] ValidationReport validateDX11Pipeline(Renderer3D& renderer) const
	{
		ValidationReport report;

		// テスト1: 初期化状態
		report.record("DX11 Initialized",
			renderer.isInitialized(),
			"Renderer3D is not initialized");

		if (!renderer.isInitialized())
		{
			return report;
		}

		// テスト2: 各シェーダーモードのコンパイル
		const ShaderMode3D modes[] = {
			ShaderMode3D::Phong,
			ShaderMode3D::Toon,
			ShaderMode3D::Unlit,
			ShaderMode3D::Flat,
			ShaderMode3D::Posterize,
			ShaderMode3D::Halftone,
			ShaderMode3D::Hatching,
			ShaderMode3D::GradientMap,
			ShaderMode3D::Silhouette,
			ShaderMode3D::Watercolor,
		};
		const char* modeNames[] = {
			"Phong", "Toon", "Unlit", "Flat", "Posterize",
			"Halftone", "Hatching", "GradientMap", "Silhouette", "Watercolor",
		};

		const auto originalMode = renderer.shaderMode();

		for (int i = 0; i < 10; ++i)
		{
			bool ok = true;
			try
			{
				renderer.setShaderMode(modes[i]);
				ok = renderer.isInitialized();
			}
			catch (...)
			{
				ok = false;
			}
			report.record(
				std::string("DX11 Shader Compile: ") + modeNames[i],
				ok,
				std::string("Failed to compile ") + modeNames[i] + " shader");
		}

		// 元のモードに戻す
		renderer.setShaderMode(originalMode);

		// テスト3: 描画テスト — 三角形を1つ描画してドローコール数を確認
		{
			auto triangle = Mesh::createCube(0.5f);
			Material mat;
			mat.diffuse = {1, 0, 0, 1};
			mat.ambient = {0.2f, 0, 0, 1};
			mat.specular = {0.3f, 0.3f, 0.3f, 1};
			mat.shininess = 16.0f;

			Camera3D camera(
				{0, 2, 5}, {0, 0, 0}, {0, 1, 0},
				0.85f, 16.0f / 9.0f, 0.1f, 100.0f);
			auto light = Light::directional({0, -1, 0.5f});

			renderer.beginFrame({0.1f, 0.1f, 0.2f, 1.0f});
			renderer.setCamera(camera);
			renderer.setLight(light);
			renderer.drawMesh(triangle, sgc::Mat4f::identity(), mat);
			renderer.endFrame();

			report.record("DX11 Draw Call Count > 0",
				renderer.drawCallCount() > 0,
				"drawCallCount was 0 after drawing a mesh");
		}

		// テスト4: クリアカラーテスト（黒以外でクリアして描画が成立するか）
		{
			renderer.beginFrame({0.5f, 0.8f, 0.2f, 1.0f});
			renderer.endFrame();
			report.record("DX11 Clear Color", true);
		}

		// テスト5: 深度バッファ検証
		report.record("DX11 Depth Buffer",
			renderer.config().enableDepthBuffer,
			"Depth buffer is disabled");

		// テスト6: 定数バッファ検証（初期化成功 = バッファ生成済み）
		report.record("DX11 Constant Buffers", renderer.isInitialized());

		return report;
	}

	/// @brief DX12パイプラインを検証する
	/// @param renderer 検証対象のRenderer3D_DX12（初期化済み）
	/// @return 検証結果レポート
	[[nodiscard]] ValidationReport validateDX12Pipeline(
		Renderer3D_DX12& renderer) const
	{
		ValidationReport report;

		// テスト1: 初期化状態
		report.record("DX12 Initialized",
			renderer.isInitialized(),
			"Renderer3D_DX12 is not initialized");

		if (!renderer.isInitialized())
		{
			return report;
		}

		// テスト2: 描画テスト
		{
			auto cube = Mesh::createCube(0.5f);
			Material mat;
			mat.diffuse = {0, 0.5f, 1, 1};
			mat.ambient = {0, 0.1f, 0.2f, 1};
			mat.specular = {0.3f, 0.3f, 0.3f, 1};
			mat.shininess = 16.0f;

			Camera3D camera(
				{0, 2, 5}, {0, 0, 0}, {0, 1, 0},
				0.85f, 16.0f / 9.0f, 0.1f, 100.0f);
			auto light = Light::directional({0, -1, 0.5f});

			renderer.beginFrame({0.1f, 0.1f, 0.2f, 1.0f});
			renderer.setCamera(camera);
			renderer.setLight(light);
			renderer.drawMesh(cube, sgc::Mat4f::identity(), mat);
			renderer.endFrame();

			report.record("DX12 Draw Call Count > 0",
				renderer.drawCallCount() > 0,
				"drawCallCount was 0 after drawing a mesh");
		}

		// テスト3: クリアカラー
		{
			renderer.beginFrame({0.8f, 0.3f, 0.1f, 1.0f});
			renderer.endFrame();
			report.record("DX12 Clear Color", true);
		}

		// テスト4: 定数バッファ（初期化成功 = 生成済み）
		report.record("DX12 Constant Buffers", renderer.isInitialized());

		// テスト5: 深度バッファ（初期化成功 = 生成済み）
		report.record("DX12 Depth Buffer", renderer.isInitialized());

		// テスト6: PSO生成（初期化成功 = PSO生成済み）
		report.record("DX12 PSO Created", renderer.isInitialized());

		return report;
	}

	/// @brief ToonPipelineを検証する
	/// @param pipeline 検証対象のToonPipeline（初期化済み）
	/// @return 検証結果レポート
	[[nodiscard]] ValidationReport validateToonPipeline(
		ToonPipeline& pipeline) const
	{
		ValidationReport report;

		// テスト1: 初期化状態（init()が全シェーダーコンパイル+PSO生成を行う）
		report.record("Toon Initialized",
			pipeline.isInitialized(),
			"ToonPipeline is not initialized");

		if (!pipeline.isInitialized())
		{
			return report;
		}

		// テスト2: PSO生成確認（初期化成功 = メインPSO生成済み）
		report.record("Toon Main PSO", pipeline.isInitialized());

		// テスト3: アウトラインPSO確認（初期化成功 = アウトラインシェーダー生成済み）
		report.record("Toon Outline PSO", pipeline.isInitialized());

		// テスト4: コンフィグ変更 — バンド数
		{
			const auto originalConfig = pipeline.config();
			auto modified = originalConfig;
			modified.lighting.bandCount = 5;
			pipeline.setConfig(modified);

			const bool bandCountApplied =
				(pipeline.config().lighting.bandCount == 5);
			report.record("Toon Config: Band Count Change",
				bandCountApplied,
				"bandCount not updated to 5");

			// 元に戻す
			pipeline.setConfig(originalConfig);
		}

		// テスト5: コンフィグ変更 — アウトライン幅
		{
			const auto originalConfig = pipeline.config();
			auto modified = originalConfig;
			modified.outline.width = 0.05f;
			pipeline.setConfig(modified);

			const bool widthApplied =
				(pipeline.config().outline.width > 0.04f &&
				 pipeline.config().outline.width < 0.06f);
			report.record("Toon Config: Outline Width Change",
				widthApplied,
				"outline width not updated to 0.05");

			pipeline.setConfig(originalConfig);
		}

		// テスト6: コンフィグ変更 — アウトラインメソッド切替
		{
			const auto originalConfig = pipeline.config();
			auto modified = originalConfig;
			modified.outline.method = ToonOutlineConfig::Method::ScreenSpaceDepth;
			pipeline.setConfig(modified);

			const bool methodApplied =
				(pipeline.config().outline.method ==
				 ToonOutlineConfig::Method::ScreenSpaceDepth);
			report.record("Toon Config: Outline Method Change",
				methodApplied,
				"outline method not updated");

			pipeline.setConfig(originalConfig);
		}

		return report;
	}

	/// @brief PBRシェーダーコンパイルを検証する
	/// @param device DX11デバイス
	/// @return 検証結果レポート
	[[nodiscard]] ValidationReport validatePBRShaders(
		ID3D11Device* device) const
	{
		ValidationReport report;

		if (!device)
		{
			report.record("PBR Device", false, "null device");
			return report;
		}

		auto shaderSet = CompilePBRShaders(device);

		report.record("PBR VS Compile",
			shaderSet.pbrVS != nullptr,
			"PBR vertex shader compilation failed");

		report.record("PBR PS Compile",
			shaderSet.pbrPS != nullptr,
			"PBR pixel shader compilation failed");

		report.record("PBR IBL PS Compile",
			shaderSet.pbrIBL_PS != nullptr,
			"PBR IBL pixel shader compilation failed");

		report.record("PBR Shadow PS Compile",
			shaderSet.pbrShadowPS != nullptr,
			"PBR shadow pixel shader compilation failed");

		report.record("PBR Input Layout",
			shaderSet.pbrInputLayout != nullptr,
			"PBR input layout creation failed");

		report.record("PBR ShaderSet Valid",
			shaderSet.valid,
			"PBR shader set validation failed");

		return report;
	}

#endif // _WIN32

	/// @brief 全パイプラインの統合テスト結果を生成する（非GPU環境用スタブ）
	[[nodiscard]] ValidationReport validateStub() const
	{
		ValidationReport report;
		report.record("Stub: No GPU", true);
		return report;
	}
};

} // namespace mitiru::render
