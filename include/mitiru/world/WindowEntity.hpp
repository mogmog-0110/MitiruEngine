#pragma once

/// @file WindowEntity.hpp
/// @brief 実 OS ウィンドウ 1 枚をゲーム entity として扱う (ADR 0030)。
/// @details `Win32Window` + `gfx::Dx12Device` + `render::RenderPipeline2D` を 1 組にまとめ、
///          位置/速度/重なり/z-order/最小化/close を追跡しつつ自分の描画面へ 2D 矩形を描ける。
///          複数インスタンスの同時存在は `tests/mitiru/TestDx12MultiWindow.cpp` で確認済み。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>    // 重なり判定の min/max
#include <functional>   // tick callback 用
#include <string>       // タイトル文字列
#include <string_view>  // タイトル引数

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/gfx/dx12/Dx12Device.hpp>
#include <mitiru/platform/win32/Win32Window.hpp>
#include <mitiru/render/RenderPipeline2D.hpp>
#include <mitiru/render/pixel/PixelFont.hpp>
#include <mitiru/render/Style2D.hpp>
#include <mitiru/render/StyledRectBatch.hpp>
#include <mitiru/render/Transform2D.hpp>

namespace mitiru::world
{

/// @brief 2 つの矩形が重なっているか
[[nodiscard]] inline bool rectsOverlap(const sgc::Rectf& a, const sgc::Rectf& b) noexcept
{
	return a.x() < b.x() + b.width() && a.x() + a.width() > b.x() && a.y() < b.y() + b.height() &&
	       a.y() + a.height() > b.y();
}

/// @brief 重なり領域。重なっていなければ false
[[nodiscard]] inline bool intersectRect(const sgc::Rectf& a, const sgc::Rectf& b,
                                         sgc::Rectf& out) noexcept
{
	const float x0 = (std::max)(a.x(), b.x());
	const float y0 = (std::max)(a.y(), b.y());
	const float x1 = (std::min)(a.x() + a.width(), b.x() + b.width());
	const float y1 = (std::min)(a.y() + a.height(), b.y() + b.height());
	if (x1 <= x0 || y1 <= y0)
	{
		return false;
	}
	out = sgc::Rectf{x0, y0, x1 - x0, y1 - y0};
	return true;
}

/// @brief 実ウィンドウ 1 枚 = 1 エンティティ
class WindowEntity
{
public:
	WindowEntity(std::string_view title, int width, int height, int posX = CW_USEDEFAULT,
	             int posY = CW_USEDEFAULT, bool resizable = true)
		: m_window(title, width, height, DisplayMode::Windowed, resizable, posX, posY)
		, m_device(&m_window)
		, m_pipeline(render::RenderPipeline2D::createFromDx12(
		      &m_device, static_cast<float>(m_window.width()), static_cast<float>(m_window.height())))
	{
		// 複数の窓を開いては閉じる使い方が前提なので、閉じた窓の WM_QUIT が次の窓に効かないようにする
		m_window.setQuitOnDestroy(false);
		m_curRect = queryRect();
		m_prevRect = m_curRect;
		m_backBufferW = static_cast<int>(m_curRect.width());
		m_backBufferH = static_cast<int>(m_curRect.height());
		m_clientW = m_backBufferW;
		m_clientH = m_backBufferH;
	}

	WindowEntity(const WindowEntity&) = delete;
	WindowEntity& operator=(const WindowEntity&) = delete;

	/// @brief 毎フレーム先頭で 1 回呼ぶ
	void update()
	{
		m_window.pollEvents();
		m_prevRect = m_curRect;
		m_curRect = queryRect();
	}

	/// 画面座標での**クライアント領域** (描ける面)。drawRect/drawCircle の (0,0) がこの左上に
	/// 対応するので、世界の座標と描画の座標が一致する。枠込みの外形は @ref frameRect
	[[nodiscard]] const sgc::Rectf& rect() const noexcept { return m_curRect; }
	[[nodiscard]] float x() const noexcept { return m_curRect.x(); }
	[[nodiscard]] float y() const noexcept { return m_curRect.y(); }
	[[nodiscard]] float width() const noexcept { return m_curRect.width(); }
	[[nodiscard]] float height() const noexcept { return m_curRect.height(); }

	/// 枠・タイトルバーを含む外形 (画面座標)。窓同士を隙間なく並べる判断はこちら
	[[nodiscard]] sgc::Rectf frameRect() const
	{
		int fx = 0, fy = 0, fw = 0, fh = 0;
		if (m_window.getWindowRect(fx, fy, fw, fh))
		{
			return sgc::Rectf{static_cast<float>(fx), static_cast<float>(fy),
			                  static_cast<float>(fw), static_cast<float>(fh)};
		}
		return m_curRect;
	}

	/// @brief **クライアント領域**の左上を画面座標 (x, y) に合わせる
	/// @details 生成時の posX/posY は枠の位置なので、描画面は枠幅とタイトルバーの分だけずれる。
	///          描画面の座標が世界の座標である使い方 (ADR 0030) では、そのずれが初期状態の
	///          ずれとしてそのまま出る。差分で動かすので枠幅を知る必要はない。
	void moveClientTo(int x, int y)
	{
		const sgc::Rectf frame = frameRect();
		const sgc::Rectf client = queryRect();
		m_window.moveWindow(static_cast<int>(frame.x()) + (x - static_cast<int>(client.x())),
		                    static_cast<int>(frame.y()) + (y - static_cast<int>(client.y())),
		                    static_cast<int>(frame.width()), static_cast<int>(frame.height()));
		m_curRect = queryRect();
		m_prevRect = m_curRect;
	}

	/// @brief **クライアント領域**を指定寸法にする
	/// @details 生成時の width/height と同じ意味の寸法で指定できる。枠幅は現在の外形と描画面の
	///          差から求めるので、DPI や窓スタイルごとの枠幅を呼び側が知る必要はない。
	///          位置も直すときは、こちらを先に呼んでから @ref moveClientTo を呼ぶ。
	void resizeClientTo(int width, int height)
	{
		const sgc::Rectf frame = frameRect();
		const sgc::Rectf client = queryRect();
		const int borderW = static_cast<int>(frame.width()) - static_cast<int>(client.width());
		const int borderH = static_cast<int>(frame.height()) - static_cast<int>(client.height());
		m_window.moveWindow(static_cast<int>(frame.x()), static_cast<int>(frame.y()),
		                    width + borderW, height + borderH);
		m_curRect = queryRect();
		m_prevRect = m_curRect;
	}

	/// @brief フレーム間の位置差分 (px/frame)
	[[nodiscard]] float velocityX() const noexcept { return m_curRect.x() - m_prevRect.x(); }
	[[nodiscard]] float velocityY() const noexcept { return m_curRect.y() - m_prevRect.y(); }

	[[nodiscard]] bool overlaps(const WindowEntity& other) const noexcept
	{
		return rectsOverlap(m_curRect, other.m_curRect);
	}

	[[nodiscard]] bool overlapRect(const WindowEntity& other, sgc::Rectf& out) const noexcept
	{
		return intersectRect(m_curRect, other.m_curRect, out);
	}

	/// @brief このウィンドウが other より前面か
	[[nodiscard]] bool isInFrontOf(const WindowEntity& other) const noexcept
	{
		const HWND self = reinterpret_cast<HWND>(m_window.nativeHandle());
		const HWND theirs = reinterpret_cast<HWND>(other.m_window.nativeHandle());
		for (HWND cur = GetTopWindow(nullptr); cur != nullptr; cur = GetWindow(cur, GW_HWNDNEXT))
		{
			if (cur == self)
			{
				return true;
			}
			if (cur == theirs)
			{
				return false;
			}
		}
		return false;
	}

	void bringToFront() noexcept
	{
		SetWindowPos(reinterpret_cast<HWND>(m_window.nativeHandle()), HWND_TOP, 0, 0, 0, 0,
		             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}

	[[nodiscard]] bool isMinimized() const noexcept
	{
		return IsIconic(reinterpret_cast<HWND>(m_window.nativeHandle())) != 0;
	}

	/// @brief タスクバーへしまう / 戻す。ユーザが枠のボタンでやることをコードから行う
	void minimize() noexcept
	{
		ShowWindow(reinterpret_cast<HWND>(m_window.nativeHandle()), SW_MINIMIZE);
	}

	void restore() noexcept
	{
		ShowWindow(reinterpret_cast<HWND>(m_window.nativeHandle()), SW_RESTORE);
	}

	[[nodiscard]] bool shouldClose() const noexcept { return m_window.shouldClose(); }
	void requestClose() noexcept { m_window.requestClose(); }

	void beginFrame(const sgc::Colorf& clear)
	{
		syncBackBufferToClient();
		syncPresentToDragState();
		m_device.setClearColor(clear.r, clear.g, clear.b, clear.a);
		m_device.beginFrame();
	}

	/// @param localRect クライアント座標系 (左上原点、px)
	void drawRect(const sgc::Rectf& localRect, const sgc::Colorf& color)
	{
		if (!m_pipeline.isValid())
		{
			return;
		}
		render::Style style;
		style.fill = color;
		m_rectBatch.begin();
		m_rectBatch.addRect(localRect, style);
		m_rectBatch.end();
		m_pipeline.submitStyledRectBatch(m_rectBatch.vertices(), m_rectBatch.indices(),
		                                  m_rectBatch.currentStyle());
	}

	/// @param localCenter クライアント座標系 (左上原点、px)
	void drawCircle(const sgc::Vec2f& localCenter, float radius, const sgc::Colorf& color)
	{
		if (!m_pipeline.isValid())
		{
			return;
		}
		render::Style style;
		style.fill = color;
		m_circleBatch.begin();
		m_circleBatch.addCircle(localCenter, radius, style);
		m_circleBatch.end();
		m_pipeline.submitStyledCircleBatch(m_circleBatch.vertices(), m_circleBatch.indices(),
		                                    m_circleBatch.currentStyle());
	}

	/// @brief 8x8 ドットフォントで文字を矩形内に描く (UTF-8、'\n' 可)
	/// @param bounds クライアント座標系 (左上原点、px)。この矩形から出る点は描かない
	void drawTextInRect(const sgc::Rectf& bounds, std::string_view text, float scale,
	                    const sgc::Colorf& color)
	{
		if (!m_pipeline.isValid())
		{
			return;
		}
		const int s = (scale < 1.0f) ? 1 : static_cast<int>(scale);
		render::Style style;
		style.fill = color;
		const float x0 = bounds.x();
		const float y0 = bounds.y();
		const float x1 = x0 + bounds.width();
		const float y1 = y0 + bounds.height();

		m_rectBatch.begin();
		render::pixel::forEachPixel(
		    text, static_cast<int>(x0), static_cast<int>(y0), s, [&](int px, int py, int size) {
			    const float fx = static_cast<float>(px);
			    const float fy = static_cast<float>(py);
			    const float fs = static_cast<float>(size);
			    if (fx < x0 || fy < y0 || fx + fs > x1 || fy + fs > y1)
			    {
				    return;
			    }
			    m_rectBatch.addRect(sgc::Rectf{fx, fy, fs, fs}, style);
		    });
		m_rectBatch.end();
		m_pipeline.submitStyledRectBatch(m_rectBatch.vertices(), m_rectBatch.indices(),
		                                  m_rectBatch.currentStyle());
	}

	/// @brief 上の描画が占める寸法 (px)。中央寄せの計算に使う
	[[nodiscard]] static sgc::Vec2f measureText(std::string_view text, float scale)
	{
		const int s = (scale < 1.0f) ? 1 : static_cast<int>(scale);
		const auto size = render::pixel::measurePixelText(text, s);
		return sgc::Vec2f{static_cast<float>(size.w), static_cast<float>(size.h)};
	}

	void endFrame() { m_device.endFrame(); }

	/// @brief 枠 drag 中 (Win32 の modal move/size loop) も呼ばれ続けるフレーム処理を登録する。
	///        登録しないと、タイトルバーを掴んでいる間ゲーム全体が止まる。
	void setTickCallback(std::function<void()> cb) { m_window.setTickCallback(std::move(cb)); }

	[[nodiscard]] Win32Window& window() noexcept { return m_window; }
	[[nodiscard]] gfx::Dx12Device& device() noexcept { return m_device; }

private:
	/// クライアント領域を画面座標で返す。外枠から枠幅を引く方法は DPI と窓スタイルごとに
	/// 変わるので使わず、ClientToScreen と GetClientRect から直接求める。
	[[nodiscard]] sgc::Rectf queryRect() const
	{
		const HWND hwnd = reinterpret_cast<HWND>(m_window.nativeHandle());
		RECT client{};
		POINT origin{0, 0};
		if (hwnd == nullptr || !GetClientRect(hwnd, &client) || !ClientToScreen(hwnd, &origin))
		{
			return m_curRect;
		}
		return sgc::Rectf{static_cast<float>(origin.x), static_cast<float>(origin.y),
		                  static_cast<float>(client.right - client.left),
		                  static_cast<float>(client.bottom - client.top)};
	}

	/// @brief 枠を掴んでいる間は vblank を待たずに出す
	/// @details 窓はマウスの報告レート (125Hz 以上) で動くのに、present が vblank で止まると
	///          出せる絵は最速でも 1 リフレッシュ分だけ古い位置のものになる。描画面の座標が
	///          世界の座標である使い方では、その遅れが中身のずれとして見える。掴んでいる間だけの
	///          話なので、ティアリングより位置の一致を採る。離せば元へ戻る。
	void syncPresentToDragState()
	{
		m_device.setVSync(!m_window.inModalLoop());
	}

	/// @brief 描画をクライアント領域の今の寸法に合わせる
	/// @details 投影は毎回クライアントに合わせるが、バッファは**育てるだけ**で縮めない。
	///          スワップチェーンの作り直しは GPU の完了待ちを含み、リサイズ中に毎回やると
	///          その間 modal loop が次のマウス報告を配れずがたつく。バッファを窓より大きめに
	///          取り (256px 刻み)、swap chain は DXGI_SCALING_NONE で左上を 1:1 に見せるので、
	///          縁を掴んで広げている間も作り直しは刻みを跨ぐ時しか起きない。
	void syncBackBufferToClient()
	{
		const sgc::Rectf client = queryRect();
		const int w = static_cast<int>(client.width());
		const int h = static_cast<int>(client.height());
		if (w <= 0 || h <= 0)
		{
			return;
		}
		if (w > m_backBufferW || h > m_backBufferH)
		{
			const auto roomy = [](int size) { return ((size / 256) + 1) * 256; };
			m_backBufferW = (std::max)(m_backBufferW, roomy(w));
			m_backBufferH = (std::max)(m_backBufferH, roomy(h));
			m_device.onResize(m_backBufferW, m_backBufferH);
		}
		if (w != m_clientW || h != m_clientH)
		{
			m_pipeline.resize(static_cast<float>(w), static_cast<float>(h));
			m_clientW = w;
			m_clientH = h;
		}
	}

	/// @brief 破棄で残った WM_QUIT を捨てる
	/// @details WM_QUIT はスレッド単位なので、閉じた窓が残したものを次に作った窓の pollEvents が
	///          自分宛の終了として拾ってしまう。窓自体は setQuitOnDestroy(false) で投げないが、
	///          スワップチェーンの解放でも積まれる。最初のメンバなので破棄は最後に走り、
	///          m_window と m_device の後始末より後に掃除できる。
	struct QuitDrain
	{
		~QuitDrain()
		{
			MSG msg{};
			while (PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
		}
	};
	QuitDrain m_quitDrain;

	Win32Window m_window;
	gfx::Dx12Device m_device;
	render::RenderPipeline2D m_pipeline;
	render::StyledRectBatch m_rectBatch;
	render::StyledCircleBatch m_circleBatch;
	sgc::Rectf m_prevRect{};
	sgc::Rectf m_curRect{};
	int m_backBufferW = 0;
	int m_backBufferH = 0;
	int m_clientW = 0;
	int m_clientH = 0;
};

} // namespace mitiru::world

#endif // _WIN32
