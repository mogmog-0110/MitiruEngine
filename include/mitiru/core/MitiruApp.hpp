#pragma once

/// @file MitiruApp.hpp
/// @brief Mitiruアプリケーションクラス
/// @details ウィンドウ・GPUデバイス・フレームタイマーを統合し、
///          IGameScene を駆動するゲームループを提供する。

#include <memory>
#include <string>

#include <mitiru/core/Config.hpp>
#include <mitiru/core/FrameTimer.hpp>
#include <mitiru/gfx/GfxFactory.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/platform/IWindow.hpp>
#include <mitiru/platform/WindowFactory.hpp>

namespace mitiru
{

/// @brief アプリケーション設定
/// @details MitiruApp の初期化パラメータを保持する。
struct AppConfig
{
	std::string windowTitle = "Mitiru Game";            ///< ウィンドウタイトル
	int width = 1280;                                   ///< ウィンドウ幅
	int height = 720;                                   ///< ウィンドウ高さ
	gfx::Backend gfxBackend = gfx::Backend::Null;       ///< GPUバックエンド
	bool vsync = true;                                  ///< 垂直同期
	float fixedTimestep = 1.0f / 60.0f;                 ///< 固定タイムステップ（秒）
};

class MitiruApp;

/// @brief ゲームシーンインターフェース
/// @details MitiruApp が駆動するシーンの抽象基底。
///          init/update/draw/shutdown のライフサイクルコールバックを定義する。
class IGameScene
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IGameScene() = default;

	/// @brief 初期化コールバック
	/// @param app アプリケーション参照
	virtual void onInit(MitiruApp& app) = 0;

	/// @brief 更新コールバック
	/// @param dt デルタタイム（秒）
	virtual void onUpdate(float dt) = 0;

	/// @brief 描画コールバック
	virtual void onDraw() = 0;

	/// @brief シャットダウンコールバック
	virtual void onShutdown() = 0;
};

/// @brief Mitiruアプリケーションクラス
/// @details ウィンドウ、GPUデバイス、フレームタイマーを統合し、
///          IGameScene を駆動するメインループを提供する。
///
/// @code
/// class MyScene : public mitiru::IGameScene { ... };
///
/// mitiru::AppConfig config;
/// config.windowTitle = "My Game";
/// mitiru::MitiruApp app(config);
/// if (app.init())
/// {
///     MyScene scene;
///     app.run(scene);
/// }
/// @endcode
class MitiruApp
{
public:
	/// @brief コンストラクタ
	/// @param config アプリケーション設定
	explicit MitiruApp(const AppConfig& config = {})
		: m_config(config)
	{
	}

	/// @brief デストラクタ
	~MitiruApp() = default;

	/// コピー禁止
	MitiruApp(const MitiruApp&) = delete;
	MitiruApp& operator=(const MitiruApp&) = delete;

	/// ムーブ許可
	MitiruApp(MitiruApp&&) noexcept = default;
	MitiruApp& operator=(MitiruApp&&) noexcept = default;

	/// @brief アプリケーションを初期化する
	/// @return 成功時 true
	/// @details WindowFactory でウィンドウを生成し、
	///          GfxFactory でGPUデバイスを生成する。
	///          外部から setWindow/setDevice で注入済みの場合はスキップする。
	[[nodiscard]] bool init()
	{
		/// ウィンドウが未設定なら生成する
		if (!m_window)
		{
			m_ownedWindow = createWindow(
				WindowBackend::Auto,
				m_config.windowTitle,
				m_config.width,
				m_config.height);
			m_window = m_ownedWindow.get();
		}

		/// デバイスが未設定なら生成する
		if (!m_device)
		{
			m_ownedDevice = gfx::createDevice(
				m_config.gfxBackend, m_window);
			m_device = m_ownedDevice.get();
		}

		/// フレームタイマーの固定ステップを設定
		m_timer.setFixedDeltaTime(m_config.fixedTimestep);

		m_initialized = true;
		return true;
	}

	/// @brief ゲームループを実行する
	/// @param scene 駆動するゲームシーン
	/// @details ウィンドウが開いている間、
	///          timer.tick → scene.update → scene.draw → device.endFrame を繰り返す。
	void run(IGameScene& scene)
	{
		if (!m_initialized)
		{
			return;
		}

		scene.onInit(*this);

		while (m_window && !m_window->shouldClose())
		{
			m_window->pollEvents();

			const float dt = m_timer.tick();
			scene.onUpdate(dt);
			scene.onDraw();

			if (m_device)
			{
				m_device->beginFrame();
				m_device->endFrame();
			}
		}

		scene.onShutdown();
	}

	/// @brief シャットダウン（リソース解放）
	void shutdown()
	{
		m_device = nullptr;
		m_window = nullptr;
		m_ownedDevice.reset();
		m_ownedWindow.reset();
		m_initialized = false;
	}

	/// @brief ウィンドウを取得する
	/// @return IWindow へのポインタ（未初期化時は nullptr）
	[[nodiscard]] IWindow* getWindow() const noexcept
	{
		return m_window;
	}

	/// @brief GPUデバイスを取得する
	/// @return IDevice へのポインタ（未初期化時は nullptr）
	[[nodiscard]] gfx::IDevice* getDevice() const noexcept
	{
		return m_device;
	}

	/// @brief フレームタイマーへの参照を取得する
	/// @return FrameTimer への参照
	[[nodiscard]] FrameTimer& getTimer() noexcept
	{
		return m_timer;
	}

	/// @brief フレームタイマーへの const 参照を取得する
	/// @return FrameTimer への const 参照
	[[nodiscard]] const FrameTimer& getTimer() const noexcept
	{
		return m_timer;
	}

	/// @brief 設定を取得する
	/// @return AppConfig への const 参照
	[[nodiscard]] const AppConfig& getConfig() const noexcept
	{
		return m_config;
	}

	/// @brief ウィンドウを外部から注入する（テスト用）
	/// @param window IWindow へのポインタ（所有権は呼び出し元が保持）
	void setWindow(IWindow* window) noexcept
	{
		m_window = window;
	}

	/// @brief GPUデバイスを外部から注入する（テスト用）
	/// @param device IDevice へのポインタ（所有権は呼び出し元が保持）
	void setDevice(gfx::IDevice* device) noexcept
	{
		m_device = device;
	}

private:
	AppConfig m_config;                              ///< アプリケーション設定
	FrameTimer m_timer;                              ///< フレームタイマー
	std::unique_ptr<IWindow> m_ownedWindow;          ///< 所有ウィンドウ
	std::unique_ptr<gfx::IDevice> m_ownedDevice;     ///< 所有デバイス
	IWindow* m_window = nullptr;                     ///< ウィンドウ（非所有ポインタ）
	gfx::IDevice* m_device = nullptr;                ///< デバイス（非所有ポインタ）
	bool m_initialized = false;                      ///< 初期化済みフラグ
};

} // namespace mitiru
