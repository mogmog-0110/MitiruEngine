#pragma once

/// @file Engine.hpp
/// @brief Mitiruエンジン本体
/// @details プラットフォーム・ウィンドウ・GPUデバイスを統合し、
///          ゲームループを実行するメインクラス。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/core/Clock.hpp>
#include <mitiru/core/Config.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/gfx/GfxFactory.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/null/NullDevice.hpp>
#include <mitiru/render/RenderPipeline2D.hpp>
#include <mitiru/input/InputInjector.hpp>
#include <mitiru/input/InputState.hpp>
#include <mitiru/observe/Snapshot.hpp>
#include <mitiru/ecs/MitiruWorld.hpp>
#include <mitiru/scene/MitiruScene.hpp>
#include <mitiru/platform/IPlatform.hpp>
#include <mitiru/platform/IWindow.hpp>
#include <mitiru/platform/headless/HeadlessPlatform.hpp>

#ifdef _WIN32
#include <mitiru/platform/win32/Win32Platform.hpp>
#endif

namespace mitiru
{

/// @brief Mitiruエンジン本体
/// @details ゲームループの実行・フレーム制御・スクリーンショット等を提供する。
class Engine
{
public:
	/// @brief デフォルトコンストラクタ
	Engine() = default;

	/// @brief エンジンを初期化しゲームループを実行する
	/// @param game ゲームインスタンス
	/// @param config エンジン設定
	void run(Game& game, const EngineConfig& config = {})
	{
		initialize(config);

		/// ゲームに論理サイズを問い合わせ
		const auto logicalSize = game.layout(
			m_window->width(), m_window->height());
		m_screen = std::make_unique<Screen>(logicalSize.width, logicalSize.height);

		/// レンダリングパイプラインを構築する
		createRenderPipeline(logicalSize.width, logicalSize.height);

		/// メインループ
		while (!m_window->shouldClose() && !m_shouldStop)
		{
			m_window->pollEvents();
			m_inputState.beginFrame();
			applyInjectedInput();

			const float dt = m_clock->tick();

			game.update(dt);

			/// シーンマネージャーが設定されている場合、現在シーンを更新
			if (m_sceneManager && m_sceneManager->currentScene())
			{
				m_sceneManager->currentScene()->onUpdate(dt);
			}

			m_screen->resetDrawCallCount();
			m_screen->clear();
			game.draw(*m_screen);

			/// シーンマネージャーが設定されている場合、現在シーンを描画
			if (m_sceneManager && m_sceneManager->currentScene())
			{
				m_sceneManager->currentScene()->onDraw(*m_screen);
			}

			if (m_device)
			{
				m_device->beginFrame();
				/// Screenの蓄積データをGPUに送信する
				m_screen->present();
				m_device->endFrame();
			}
		}
	}

	/// @brief 指定フレーム数だけバッチ実行する（ヘッドレス用）
	/// @param game ゲームインスタンス
	/// @param frameCount 実行フレーム数
	/// @param config エンジン設定
	void stepFrames(Game& game, std::uint64_t frameCount,
	                const EngineConfig& config = {})
	{
		if (!m_initialized)
		{
			auto headlessConfig = config;
			headlessConfig.headless = true;
			initialize(headlessConfig);

			const auto logicalSize = game.layout(
				m_window->width(), m_window->height());
			m_screen = std::make_unique<Screen>(
				logicalSize.width, logicalSize.height);
		}

		for (std::uint64_t i = 0; i < frameCount; ++i)
		{
			m_inputState.beginFrame();
			applyInjectedInput();

			const float dt = m_clock->tick();

			game.update(dt);

			/// シーンマネージャーが設定されている場合、現在シーンを更新
			if (m_sceneManager && m_sceneManager->currentScene())
			{
				m_sceneManager->currentScene()->onUpdate(dt);
			}

			m_screen->resetDrawCallCount();
			m_screen->clear();
			game.draw(*m_screen);

			/// シーンマネージャーが設定されている場合、現在シーンを描画
			if (m_sceneManager && m_sceneManager->currentScene())
			{
				m_sceneManager->currentScene()->onDraw(*m_screen);
			}
		}
	}

	/// @brief スクリーンショットを取得する
	/// @return RGBA8形式のピクセルデータ
	[[nodiscard]] std::vector<std::uint8_t> capture() const
	{
		if (!m_device || !m_screen)
		{
			return {};
		}
		return m_device->readPixels(m_screen->width(), m_screen->height());
	}

	/// @brief ECSワールドを設定する
	/// @param world MitiruWorldへのポインタ（nullptrで解除）
	void setWorld(ecs::MitiruWorld* world) noexcept
	{
		m_world = world;
	}

	/// @brief シーンマネージャーを設定する
	/// @param mgr MitiruSceneManagerへのポインタ（nullptrで解除）
	void setSceneManager(scene::MitiruSceneManager* mgr) noexcept
	{
		m_sceneManager = mgr;
	}

	/// @brief シーン状態のJSONスナップショットを取得する
	/// @return JSON文字列
	[[nodiscard]] std::string snapshot() const
	{
		SnapshotData data;
		if (m_clock)
		{
			data.frameNumber = m_clock->frameNumber();
			data.timestamp = m_clock->elapsed();
		}

		/// ワールドが設定されていればエンティティ情報を含める
		if (m_world)
		{
			data.entityCount = static_cast<int>(m_world->entityCount());
			data.worldSnapshot = m_world->snapshot();
		}

		/// シーンマネージャーが設定されていればシーン情報を含める
		if (m_sceneManager)
		{
			const auto* current = m_sceneManager->currentScene();
			if (current)
			{
				data.sceneInfo = current->name();
			}
		}

		/// スクリーンが存在すれば描画コール数を含める
		if (m_screen)
		{
			data.drawCallCount = m_screen->drawCallCount();
		}

		return data.toJson();
	}

	/// @brief エンジン停止を要求する
	void requestStop() noexcept
	{
		m_shouldStop = true;
	}

	/// @brief 現在のフレーム番号を取得する
	[[nodiscard]] std::uint64_t frameNumber() const noexcept
	{
		return m_clock ? m_clock->frameNumber() : 0;
	}

	/// @brief 入力インジェクターへの参照を取得する
	/// @return InputInjector への参照
	[[nodiscard]] InputInjector& inputInjector() noexcept
	{
		return m_inputInjector;
	}

	/// @brief 現在の入力状態を取得する
	[[nodiscard]] const InputState& inputState() const noexcept
	{
		return m_inputState;
	}

	/// @brief スクリーンへの参照を取得する（初期化後のみ有効）
	/// @return Screen へのポインタ（未初期化時は nullptr）
	[[nodiscard]] const Screen* screen() const noexcept
	{
		return m_screen.get();
	}

	/// @brief クロックへの参照を取得する（初期化後のみ有効）
	/// @return Clock へのポインタ（未初期化時は nullptr）
	[[nodiscard]] const Clock* clock() const noexcept
	{
		return m_clock.get();
	}

private:
	/// @brief エンジン内部を初期化する
	/// @param config 設定
	void initialize(const EngineConfig& config)
	{
		m_config = config;
		m_shouldStop = false;

		/// プラットフォーム生成
		if (config.headless)
		{
			m_platform = std::make_unique<HeadlessPlatform>();
		}
		else
		{
#ifdef _WIN32
			m_platform = std::make_unique<Win32Platform>();
#else
			/// 非Windows環境ではヘッドレスにフォールバック
			m_platform = std::make_unique<HeadlessPlatform>();
#endif
		}

		/// ウィンドウ生成
		m_window = m_platform->createWindow(
			config.title, config.windowWidth, config.windowHeight);

#ifdef _WIN32
		/// Win32ウィンドウに入力状態を接続する
		if (auto* win32Win = dynamic_cast<Win32Window*>(m_window.get()))
		{
			win32Win->setInputState(&m_inputState);
		}
#endif

		/// GPUデバイス生成
		if (config.headless || config.gfxBackend == gfx::Backend::Null)
		{
			m_device = std::make_unique<gfx::NullDevice>();
		}
		else
		{
			m_device = gfx::createDevice(
				config.gfxBackend, m_window.get());
		}

		/// クロック生成
		m_clock = std::make_unique<Clock>(config.targetTps, config.deterministic);

		m_initialized = true;
	}

	/// @brief レンダリングパイプラインを構築してScreenに接続する
	/// @param screenWidth スクリーン幅
	/// @param screenHeight スクリーン高さ
	void createRenderPipeline(int screenWidth, int screenHeight)
	{
#ifdef _WIN32
		/// DX11デバイスの場合、2Dパイプラインを構築する
		auto* dx11Device = dynamic_cast<gfx::Dx11Device*>(m_device.get());
		if (dx11Device)
		{
			m_renderPipeline = std::make_unique<render::RenderPipeline2D>(
				render::RenderPipeline2D::createFromDx11(
					dx11Device,
					static_cast<float>(screenWidth),
					static_cast<float>(screenHeight)));
			m_screen->setPipeline(m_renderPipeline.get());
			return;
		}
#else
		static_cast<void>(screenWidth);
		static_cast<void>(screenHeight);
#endif
		/// NullDevice等の場合はパイプラインなし（ヘッドレス動作）
	}

	/// @brief 注入された入力コマンドを適用する
	void applyInjectedInput()
	{
		const auto commands = m_inputInjector.consumePending();
		for (const auto& cmd : commands)
		{
			switch (cmd.type)
			{
			case InputCommandType::KeyDown:
				m_inputState.setKeyDown(cmd.keyCode, true);
				break;
			case InputCommandType::KeyUp:
				m_inputState.setKeyDown(cmd.keyCode, false);
				break;
			case InputCommandType::MouseMove:
				m_inputState.setMousePosition(cmd.mouseX, cmd.mouseY);
				break;
			case InputCommandType::MouseDown:
				m_inputState.setMouseButtonDown(
					static_cast<MouseButton>(cmd.mouseButton), true);
				break;
			case InputCommandType::MouseUp:
				m_inputState.setMouseButtonDown(
					static_cast<MouseButton>(cmd.mouseButton), false);
				break;
			}
		}
	}

	EngineConfig m_config;                          ///< エンジン設定
	std::unique_ptr<IPlatform> m_platform;          ///< プラットフォーム
	std::unique_ptr<IWindow> m_window;              ///< ウィンドウ
	std::unique_ptr<gfx::IDevice> m_device;         ///< GPUデバイス
	std::unique_ptr<Clock> m_clock;                 ///< ゲームクロック
	std::unique_ptr<Screen> m_screen;               ///< 描画サーフェス
	std::unique_ptr<render::RenderPipeline2D> m_renderPipeline; ///< 2Dレンダリングパイプライン
	InputInjector m_inputInjector;                   ///< 入力インジェクター
	InputState m_inputState;                         ///< 現在の入力状態
	ecs::MitiruWorld* m_world = nullptr;             ///< ECSワールド（非所有）
	scene::MitiruSceneManager* m_sceneManager = nullptr; ///< シーンマネージャー（非所有）
	bool m_shouldStop = false;                       ///< 停止要求フラグ
	bool m_initialized = false;                      ///< 初期化済みフラグ
};

} // namespace mitiru
