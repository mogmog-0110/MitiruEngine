#pragma once

/// @file GlfwWindow.hpp
/// @brief GLFWウィンドウ実装（OpenGL/Vulkan対応）
/// @details GLFWライブラリを使用したクロスプラットフォームウィンドウ管理。
///          OpenGL 3.3 Core Profile または Vulkanバックエンドに対応。
///          MITIRU_HAS_GLFWが定義されている場合のみコンパイルされる。

#ifdef MITIRU_HAS_GLFW

#include <stdexcept>
#include <string>
#include <string_view>

// Vulkanヘッダが先にincludeされていれば、GLFWがVulkan連携関数を宣言する
#ifdef MITIRU_HAS_VULKAN
#include <vulkan/vulkan.h>
#endif
#include <GLFW/glfw3.h>

#include <mitiru/platform/IWindow.hpp>

namespace mitiru
{

/// @brief GLFWウィンドウ グラフィクスモード
enum class GlfwGraphicsMode : std::uint8_t
{
	Vulkan,    ///< Vulkan用 (GLFW_NO_API)
	OpenGL,    ///< OpenGL 3.3 Core Profile
};

/// @brief GLFWウィンドウ実装
/// @details GLFWwindowをラップし、IWindowインターフェースを提供する。
///          OpenGL 3.3またはVulkanのどちらかのモードで動作する。
///
/// @code
/// // OpenGLモード
/// auto window = std::make_unique<GlfwWindow>("My Game", 1280, 720, GlfwGraphicsMode::OpenGL);
/// // Vulkanモード（デフォルト）
/// auto window = std::make_unique<GlfwWindow>("My Game", 1280, 720);
/// @endcode
class GlfwWindow final : public IWindow
{
public:
	/// @brief コンストラクタ
	/// @param title ウィンドウタイトル
	/// @param width クライアント領域の幅
	/// @param height クライアント領域の高さ
	/// @param mode グラフィクスモード (デフォルト: Vulkan)
	explicit GlfwWindow(std::string_view title, int width, int height,
	                    GlfwGraphicsMode mode = GlfwGraphicsMode::Vulkan)
		: m_width(width)
		, m_height(height)
		, m_graphicsMode(mode)
	{
		++s_instanceCount;

		/// GLFWの初期化
		if (!glfwInit())
		{
			throw std::runtime_error("glfwInit failed");
		}

		if (mode == GlfwGraphicsMode::OpenGL)
		{
			/// OpenGL 3.3 Core Profile
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
		}
		else
		{
			/// Vulkan用 — OpenGLコンテキストを無効化
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		}
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

		m_window = glfwCreateWindow(
			width, height,
			std::string(title).c_str(),
			nullptr, nullptr);

		if (!m_window)
		{
			throw std::runtime_error("glfwCreateWindow failed");
		}

		/// OpenGLモードの場合、コンテキストをカレントに設定
		if (mode == GlfwGraphicsMode::OpenGL)
		{
			glfwMakeContextCurrent(m_window);
			glfwSwapInterval(1); // VSync有効
		}

		/// リサイズコールバックを設定する
		glfwSetWindowUserPointer(m_window, this);
		glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
	}

	/// @brief デストラクタ
	~GlfwWindow() override
	{
		if (m_window)
		{
			glfwDestroyWindow(m_window);
			m_window = nullptr;
		}
		if (--s_instanceCount == 0)
		{
			glfwTerminate();
		}
	}

	/// コピー禁止
	GlfwWindow(const GlfwWindow&) = delete;
	GlfwWindow& operator=(const GlfwWindow&) = delete;

	/// ムーブ禁止
	GlfwWindow(GlfwWindow&&) = delete;
	GlfwWindow& operator=(GlfwWindow&&) = delete;

	/// @brief ウィンドウが閉じられるべきかどうか
	[[nodiscard]] bool shouldClose() const override
	{
		return glfwWindowShouldClose(m_window) != 0;
	}

	/// @brief GLFWイベントキューをポーリングする
	void pollEvents() override
	{
		glfwPollEvents();
	}

	/// @brief クライアント領域の幅を取得する
	[[nodiscard]] int width() const override
	{
		return m_width;
	}

	/// @brief クライアント領域の高さを取得する
	[[nodiscard]] int height() const override
	{
		return m_height;
	}

	/// @brief ウィンドウタイトルを設定する
	void setTitle(std::string_view title) override
	{
		if (m_window)
		{
			glfwSetWindowTitle(m_window, std::string(title).c_str());
		}
	}

	/// @brief ウィンドウの閉じ要求を設定する
	void requestClose() override
	{
		if (m_window)
		{
			glfwSetWindowShouldClose(m_window, GLFW_TRUE);
		}
	}

	/// @brief OpenGLモードでバッファをスワップする
	void swapBuffers()
	{
		if (m_graphicsMode == GlfwGraphicsMode::OpenGL && m_window)
		{
			glfwSwapBuffers(m_window);
		}
	}

	/// @brief グラフィクスモードを取得する
	[[nodiscard]] GlfwGraphicsMode graphicsMode() const noexcept
	{
		return m_graphicsMode;
	}

	/// @brief エンジンのInputStateを接続する
	/// @details GLFWコールバックからInputStateにマウス/キー入力を転送する
	void setInputState(InputState* state) override
	{
		m_inputState = state;
		if (m_window && state)
		{
			glfwSetCursorPosCallback(m_window, cursorPosInputCallback);
			glfwSetMouseButtonCallback(m_window, mouseButtonInputCallback);
			glfwSetKeyCallback(m_window, keyInputCallback);
		}
	}

	/// @brief ネイティブGLFWwindowを取得する
	/// @return GLFWwindowポインタ（Vulkanサーフェス生成に使用）
	/// @note GlfwInput、GlfwVulkanSurfaceと組み合わせて使用する。
	///       GlfwInputはこのウィンドウにコールバックを登録する。
	///       GlfwVulkanSurfaceはこのウィンドウからVkSurfaceKHRを生成する。
	[[nodiscard]] GLFWwindow* nativeWindow() const noexcept
	{
		return m_window;
	}

	/// @brief ウィンドウサイズを変更する
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void resize(int width, int height)
	{
		if (m_window)
		{
			glfwSetWindowSize(m_window, width, height);
			m_width = width;
			m_height = height;
		}
	}

	/// @brief フルスクリーンモードを切り替える
	/// @param fullscreen フルスクリーンにする場合true
	void setFullscreen(bool fullscreen)
	{
		if (!m_window)
		{
			return;
		}

		if (fullscreen)
		{
			/// 現在のウィンドウ位置とサイズを保存する
			glfwGetWindowPos(m_window, &m_windowedPosX, &m_windowedPosY);
			m_windowedWidth = m_width;
			m_windowedHeight = m_height;

			/// プライマリモニターのビデオモードを取得する
			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = glfwGetVideoMode(monitor);
			glfwSetWindowMonitor(
				m_window, monitor,
				0, 0, mode->width, mode->height, mode->refreshRate);
		}
		else
		{
			/// ウィンドウモードに戻す
			glfwSetWindowMonitor(
				m_window, nullptr,
				m_windowedPosX, m_windowedPosY,
				m_windowedWidth, m_windowedHeight, 0);
		}
		m_fullscreen = fullscreen;
	}

	/// @brief フルスクリーン状態を取得する
	/// @return フルスクリーンならtrue
	[[nodiscard]] bool isFullscreen() const noexcept
	{
		return m_fullscreen;
	}

private:
	/// @brief フレームバッファリサイズコールバック
	/// @param window GLFWウィンドウ
	/// @param width 新しい幅
	/// @param height 新しい高さ
	static void framebufferSizeCallback(
		GLFWwindow* window, int width, int height)
	{
		auto* self = static_cast<GlfwWindow*>(
			glfwGetWindowUserPointer(window));
		if (self)
		{
			self->m_width = width;
			self->m_height = height;
		}
	}

	/// @brief マウス移動コールバック（InputState連携用）
	static void cursorPosInputCallback(GLFWwindow* window, double xpos, double ypos)
	{
		auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
		if (self && self->m_inputState)
		{
			self->m_inputState->setMousePosition(
				static_cast<float>(xpos), static_cast<float>(ypos));
		}
	}

	/// @brief マウスボタンコールバック（InputState連携用）
	static void mouseButtonInputCallback(GLFWwindow* window, int button, int action, int /*mods*/)
	{
		auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
		if (self && self->m_inputState)
		{
			MouseButton mb = MouseButton::Left;
			if (button == GLFW_MOUSE_BUTTON_RIGHT) mb = MouseButton::Right;
			else if (button == GLFW_MOUSE_BUTTON_MIDDLE) mb = MouseButton::Middle;
			self->m_inputState->setMouseButtonDown(mb, action == GLFW_PRESS);
		}
	}

	/// @brief キーコールバック（InputState連携用）
	static void keyInputCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
	{
		auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
		if (self && self->m_inputState && key >= 0 && key < 512)
		{
			self->m_inputState->setKeyDown(key, action != GLFW_RELEASE);
		}
	}

	static inline int s_instanceCount = 0;

	GLFWwindow* m_window = nullptr;   ///< GLFWウィンドウハンドル
	InputState* m_inputState = nullptr; ///< エンジン入力状態（接続時のみ非null）
	int m_width = 0;                  ///< クライアント領域の幅
	int m_height = 0;                 ///< クライアント領域の高さ
	GlfwGraphicsMode m_graphicsMode = GlfwGraphicsMode::Vulkan;
	bool m_fullscreen = false;        ///< フルスクリーンフラグ
	int m_windowedPosX = 0;           ///< ウィンドウモード時のX位置
	int m_windowedPosY = 0;           ///< ウィンドウモード時のY位置
	int m_windowedWidth = 0;          ///< ウィンドウモード時の幅
	int m_windowedHeight = 0;         ///< ウィンドウモード時の高さ
};

} // namespace mitiru

#endif // MITIRU_HAS_GLFW
