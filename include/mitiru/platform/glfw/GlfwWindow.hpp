#pragma once

/// @file GlfwWindow.hpp
/// @brief GLFWウィンドウ実装（スタブ）
/// @details GLFWライブラリを使用したクロスプラットフォームウィンドウ管理。
///          OpenGL/Vulkバックエンドとの統合に適している。
///          MITIRU_HAS_GLFWが定義されている場合のみコンパイルされる。

#ifdef MITIRU_HAS_GLFW

#include <stdexcept>
#include <string>
#include <string_view>

#include <GLFW/glfw3.h>

#include <mitiru/platform/IWindow.hpp>

namespace mitiru
{

/// @brief GLFWウィンドウ実装
/// @details GLFWwindowをラップし、IWindowインターフェースを提供する。
///          Vulkanサーフェス生成やOpenGLコンテキスト作成に使用する。
///
/// @code
/// auto window = std::make_unique<GlfwWindow>("My Game", 1280, 720);
/// while (!window->shouldClose())
/// {
///     window->pollEvents();
///     // ... 描画 ...
/// }
/// @endcode
class GlfwWindow final : public IWindow
{
public:
	/// @brief コンストラクタ
	/// @param title ウィンドウタイトル
	/// @param width クライアント領域の幅
	/// @param height クライアント領域の高さ
	explicit GlfwWindow(std::string_view title, int width, int height)
		: m_width(width)
		, m_height(height)
	{
		/// GLFWの初期化
		if (!glfwInit())
		{
			throw std::runtime_error("glfwInit failed");
		}

		/// OpenGLコンテキストを無効化（Vulkan用）
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

		m_window = glfwCreateWindow(
			width, height,
			std::string(title).c_str(),
			nullptr, nullptr);

		if (!m_window)
		{
			throw std::runtime_error("glfwCreateWindow failed");
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

	/// @brief ネイティブGLFWwindowを取得する
	/// @return GLFWwindowポインタ（Vulkanサーフェス生成に使用）
	[[nodiscard]] GLFWwindow* nativeWindow() const noexcept
	{
		return m_window;
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

	GLFWwindow* m_window = nullptr;   ///< GLFWウィンドウハンドル
	int m_width = 0;                  ///< クライアント領域の幅
	int m_height = 0;                 ///< クライアント領域の高さ
};

} // namespace mitiru

#endif // MITIRU_HAS_GLFW
