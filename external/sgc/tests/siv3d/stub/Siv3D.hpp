#pragma once

/// @file Siv3D.hpp
/// @brief Siv3Dスタブ（テスト用）
///
/// Siv3Dの型・関数をスタブとして定義する。
/// 描画コールを記録し、入力状態をモックできる。

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// ── スタブ記録・モック状態 ──────────────────────────────

namespace siv3d_stub
{

/// @brief 描画コールの種別
enum class DrawType
{
	RectFill,
	RectFrame,
	CircleFill,
	CircleFrame,
	Line,
	TriangleFill,
	SceneRectFill,
	SetBackground,
	FontDraw,
	FontDrawAt,
	AudioPlay,
	AudioStop,
	AudioPause,
	AudioSetVolume,
	AudioPlayOneShot
};

/// @brief 描画コール記録
struct DrawCall
{
	DrawType type;
	double params[8]{};      ///< パラメータ（位置、サイズ等）
	double r{0}, g{0}, b{0}, a{1}; ///< カラー値（RGBA [0,1]）
};

/// @brief 描画コール記録リストを取得する
inline std::vector<DrawCall>& drawCalls()
{
	static std::vector<DrawCall> calls;
	return calls;
}

/// @brief フォント作成記録
struct FontRecord
{
	int size;       ///< フォントサイズ
	int typeface;   ///< タイプフェース値
};

/// @brief フォント記録リストを取得する
inline std::vector<FontRecord>& fontRecords()
{
	static std::vector<FontRecord> records;
	return records;
}

/// @brief モック用マウス座標
inline double& mousePosX()
{
	static double v = 0.0;
	return v;
}
inline double& mousePosY()
{
	static double v = 0.0;
	return v;
}

/// @brief モック用マウスデルタ
inline double& mouseDeltaX()
{
	static double v = 0.0;
	return v;
}
inline double& mouseDeltaY()
{
	static double v = 0.0;
	return v;
}

/// @brief モック用キー押下状態（pressed）
/// キーIDをインデックスに使用する
inline bool* keyPressed()
{
	static bool state[256] = {};
	return state;
}

/// @brief モック用キー押下状態（down = このフレームで新たに押された）
inline bool* keyDown()
{
	static bool state[256] = {};
	return state;
}

/// @brief モック用キー離上状態（up = このフレームで離された）
inline bool* keyUp()
{
	static bool state[256] = {};
	return state;
}

/// @brief オーディオ記録
struct AudioRecord
{
	std::string path;     ///< ファイルパス
	double volume{1.0};   ///< ボリューム
	bool playing{false};  ///< 再生中か
	bool valid{true};     ///< 有効か（operator bool用）
};

/// @brief オーディオ記録リストを取得する
inline std::vector<AudioRecord>& audioRecords()
{
	static std::vector<AudioRecord> records;
	return records;
}

/// @brief System::Update()の戻り値を制御する
inline bool& systemUpdateResult()
{
	static bool r = true;
	return r;
}

/// @brief System::Update()の呼び出し回数を取得する
inline int& systemUpdateCallCount()
{
	static int c = 0;
	return c;
}

/// @brief System::Update()がfalseを返すまでの残りフレーム数
inline int& systemUpdateRemainingFrames()
{
	static int n = -1;  ///< -1 = 無制限
	return n;
}

/// @brief 背景色記録
struct BackgroundColor
{
	double r{0}, g{0}, b{0}, a{1};
};

/// @brief 最後に設定された背景色
inline BackgroundColor& lastBackground()
{
	static BackgroundColor bg;
	return bg;
}

/// @brief シーンの幅
inline int& sceneWidth()
{
	static int w = 800;
	return w;
}

/// @brief シーンの高さ
inline int& sceneHeight()
{
	static int h = 600;
	return h;
}

/// @brief 全スタブ状態をリセットする
inline void reset()
{
	drawCalls().clear();
	fontRecords().clear();
	audioRecords().clear();
	mousePosX() = 0.0;
	mousePosY() = 0.0;
	mouseDeltaX() = 0.0;
	mouseDeltaY() = 0.0;
	std::memset(keyPressed(), 0, 256);
	std::memset(keyDown(), 0, 256);
	std::memset(keyUp(), 0, 256);
	systemUpdateResult() = true;
	systemUpdateCallCount() = 0;
	systemUpdateRemainingFrames() = -1;
	lastBackground() = {};
	sceneWidth() = 800;
	sceneHeight() = 600;
}

} // namespace siv3d_stub

// ── Siv3D型スタブ ──────────────────────────────────────

namespace s3d
{

/// @brief 2Dベクトル（double精度）
struct Vec2
{
	double x = 0.0;
	double y = 0.0;
};

/// @brief 3Dベクトル（double精度）
struct Vec3
{
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
};

/// @brief 浮動小数点カラー
struct ColorF
{
	double r = 1.0;
	double g = 1.0;
	double b = 1.0;
	double a = 1.0;
};

/// @brief 整数カラー
struct Color
{
	uint8_t r = 255;
	uint8_t g = 255;
	uint8_t b = 255;
	uint8_t a = 255;
};

/// @brief 文字列型（UTF-32相当をstd::u32stringで代用）
using String = std::u32string;

/// @brief Unicode変換ユーティリティ
struct Unicode
{
	/// @brief UTF-8文字列をSiv3D Stringに変換する
	static String FromUTF8(std::string_view s)
	{
		// テスト用の簡易変換（ASCII部分のみ正しく変換）
		String result;
		result.reserve(s.size());
		for (char c : s)
		{
			result.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
		}
		return result;
	}
};

/// @brief 矩形（double精度）
struct RectF
{
	double x = 0.0;
	double y = 0.0;
	double w = 0.0;
	double h = 0.0;

	/// @brief サイズをVec2として取得する
	Vec2 size{};

	/// @brief コンストラクタ
	constexpr RectF() = default;
	constexpr RectF(double x_, double y_, double w_, double h_)
		: x(x_), y(y_), w(w_), h(h_), size{w_, h_} {}

	/// @brief 塗りつぶし描画する
	void draw(const ColorF& color) const
	{
		siv3d_stub::drawCalls().push_back({
			siv3d_stub::DrawType::RectFill,
			{x, y, w, h},
			color.r, color.g, color.b, color.a
		});
	}

	/// @brief 枠線を描画する
	void drawFrame(double thickness, const ColorF& color) const
	{
		siv3d_stub::drawCalls().push_back({
			siv3d_stub::DrawType::RectFrame,
			{x, y, w, h, thickness},
			color.r, color.g, color.b, color.a
		});
	}
};

/// @brief 整数矩形
struct Rect
{
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;

	/// @brief 塗りつぶし描画する
	void draw(const ColorF& color) const
	{
		siv3d_stub::drawCalls().push_back({
			siv3d_stub::DrawType::SceneRectFill,
			{static_cast<double>(x), static_cast<double>(y),
			 static_cast<double>(w), static_cast<double>(h)},
			color.r, color.g, color.b, color.a
		});
	}
};

/// @brief 円
struct Circle
{
	Vec2 center{};
	double r = 0.0;

	Circle() = default;
	Circle(const Vec2& c, double radius) : center(c), r(radius) {}

	/// @brief 塗りつぶし描画する
	void draw(const ColorF& color) const
	{
		siv3d_stub::drawCalls().push_back({
			siv3d_stub::DrawType::CircleFill,
			{center.x, center.y, r},
			color.r, color.g, color.b, color.a
		});
	}

	/// @brief 枠線を描画する
	void drawFrame(double thickness, const ColorF& color) const
	{
		siv3d_stub::drawCalls().push_back({
			siv3d_stub::DrawType::CircleFrame,
			{center.x, center.y, r, thickness},
			color.r, color.g, color.b, color.a
		});
	}
};

/// @brief 線分
struct Line
{
	Vec2 begin{};
	Vec2 end{};

	Line() = default;
	Line(const Vec2& b, const Vec2& e) : begin(b), end(e) {}

	/// @brief 描画する
	void draw(double thickness, const ColorF& color) const
	{
		siv3d_stub::drawCalls().push_back({
			siv3d_stub::DrawType::Line,
			{begin.x, begin.y, end.x, end.y, thickness},
			color.r, color.g, color.b, color.a
		});
	}
};

/// @brief 三角形
struct Triangle
{
	Vec2 p0{};
	Vec2 p1{};
	Vec2 p2{};

	Triangle() = default;
	Triangle(const Vec2& a, const Vec2& b, const Vec2& c) : p0(a), p1(b), p2(c) {}

	/// @brief 塗りつぶし描画する
	void draw(const ColorF& color) const
	{
		siv3d_stub::drawCalls().push_back({
			siv3d_stub::DrawType::TriangleFill,
			{p0.x, p0.y, p1.x, p1.y, p2.x, p2.y},
			color.r, color.g, color.b, color.a
		});
	}
};

/// @brief タイプフェース列挙
enum class Typeface
{
	Regular = 0,
	Medium = 1,
	Heavy = 2
};

// 前方宣言
struct DrawableText;

/// @brief フォント
struct Font
{
	int m_size = 16;
	Typeface m_typeface = Typeface::Regular;

	Font() = default;
	Font(int size, Typeface typeface = Typeface::Regular)
		: m_size(size), m_typeface(typeface)
	{
		siv3d_stub::fontRecords().push_back({size, static_cast<int>(typeface)});
	}

	/// @brief フォントの高さを返す
	[[nodiscard]] int height() const { return m_size; }

	/// @brief テキストからDrawableTextを生成する
	[[nodiscard]] DrawableText operator()(const String& text) const;
};

/// @brief 描画可能テキスト（Font::operator()の戻り値）
struct DrawableText
{
	const Font* font = nullptr;
	String text;

	/// @brief 左上基準で描画する
	void draw(const Vec2& pos, const ColorF& color) const
	{
		siv3d_stub::drawCalls().push_back({
			siv3d_stub::DrawType::FontDraw,
			{pos.x, pos.y, static_cast<double>(font ? font->m_size : 0)},
			color.r, color.g, color.b, color.a
		});
	}

	/// @brief 中央基準で描画する
	void drawAt(const Vec2& center, const ColorF& color) const
	{
		siv3d_stub::drawCalls().push_back({
			siv3d_stub::DrawType::FontDrawAt,
			{center.x, center.y, static_cast<double>(font ? font->m_size : 0)},
			color.r, color.g, color.b, color.a
		});
	}

	/// @brief テキスト描画領域を返す
	[[nodiscard]] RectF region() const
	{
		// 近似値: 文字数 * フォントサイズ * 0.6 幅、フォントサイズ高さ
		const double charWidth = (font ? font->m_size : 16) * 0.6;
		const double width = static_cast<double>(text.size()) * charWidth;
		const double height = font ? static_cast<double>(font->m_size) : 16.0;
		return RectF{0.0, 0.0, width, height};
	}
};

/// @brief Font::operator()の実装
inline DrawableText Font::operator()(const String& t) const
{
	return DrawableText{this, t};
}

/// @brief テクスチャ領域（描画可能）
struct TextureRegion
{
	double m_x = 0.0;
	double m_y = 0.0;
	double m_w = 64.0;
	double m_h = 64.0;

	/// @brief リサイズした領域を返す
	TextureRegion resized(double w, double h) const
	{
		return TextureRegion{m_x, m_y, w, h};
	}

	/// @brief 回転した領域を返す
	TextureRegion rotatedAt(const Vec2& /*origin*/, double /*angle*/) const
	{
		return *this;
	}

	/// @brief 描画する
	void draw(const Vec2& pos, const ColorF& /*color*/) const
	{
		siv3d_stub::drawCalls().push_back({
			siv3d_stub::DrawType::RectFill,
			{pos.x, pos.y, m_w, m_h},
			1.0, 1.0, 1.0, 1.0
		});
	}
};

/// @brief テクスチャ
struct Texture
{
	int m_width = 64;
	int m_height = 64;
	bool m_valid = false;

	Texture() = default;

	/// @brief パス指定でロードする
	explicit Texture(const String& /*path*/)
		: m_width(64), m_height(64), m_valid(true) {}

	/// @brief 有効判定
	explicit operator bool() const { return m_valid; }

	/// @brief テクスチャサイズを取得する
	struct Size { double x; double y; };
	[[nodiscard]] Size size() const { return Size{static_cast<double>(m_width), static_cast<double>(m_height)}; }

	/// @brief ソース矩形を指定してテクスチャ領域を取得する
	[[nodiscard]] TextureRegion operator()(const RectF& src) const
	{
		return TextureRegion{src.x, src.y, src.w, src.h};
	}

	/// @brief リサイズしたテクスチャ領域を取得する
	[[nodiscard]] TextureRegion resized(double w, double h) const
	{
		return TextureRegion{0.0, 0.0, w, h};
	}
};

/// @brief 入力オブジェクト（キー・マウスボタン共用）
struct Input
{
	int id = 0;

	/// @brief 押下中か
	[[nodiscard]] bool pressed() const { return siv3d_stub::keyPressed()[id]; }

	/// @brief このフレームで押されたか
	[[nodiscard]] bool down() const { return siv3d_stub::keyDown()[id]; }

	/// @brief このフレームで離されたか
	[[nodiscard]] bool up() const { return siv3d_stub::keyUp()[id]; }
};

// ── キー定数 ──────────────────────────────────────────

inline const Input KeyEscape{1};
inline const Input KeyEnter{2};
inline const Input KeySpace{3};
inline const Input KeyLeft{4};
inline const Input KeyRight{5};
inline const Input KeyUp{6};
inline const Input KeyDown{7};
inline const Input KeyBackspace{8};
inline const Input KeyW{9};
inline const Input KeyA{10};
inline const Input KeyS{11};
inline const Input KeyD{12};
inline const Input KeyR{13};
inline const Input Key1{14};
inline const Input Key2{15};
inline const Input Key3{16};

// ── マウスボタン定数 ──────────────────────────────────

inline const Input MouseL{100};
inline const Input MouseR{101};
inline const Input MouseM{102};

/// @brief カーソルユーティリティ
struct Cursor
{
	/// @brief カーソル座標を返す
	static Vec2 PosF()
	{
		return {siv3d_stub::mousePosX(), siv3d_stub::mousePosY()};
	}

	/// @brief カーソル移動量を返す
	static Vec2 DeltaF()
	{
		return {siv3d_stub::mouseDeltaX(), siv3d_stub::mouseDeltaY()};
	}
};

/// @brief シーンユーティリティ
struct Scene
{
	/// @brief 背景色を設定する
	static void SetBackground(const ColorF& color)
	{
		siv3d_stub::lastBackground() = {color.r, color.g, color.b, color.a};
		siv3d_stub::drawCalls().push_back({
			siv3d_stub::DrawType::SetBackground,
			{},
			color.r, color.g, color.b, color.a
		});
	}

	/// @brief シーン矩形を返す
	static Rect Rect()
	{
		return s3d::Rect{0, 0, siv3d_stub::sceneWidth(), siv3d_stub::sceneHeight()};
	}
};

/// @brief システムユーティリティ
struct System
{
	/// @brief メインループ更新
	static bool Update()
	{
		++siv3d_stub::systemUpdateCallCount();

		// 残りフレーム制御
		auto& remaining = siv3d_stub::systemUpdateRemainingFrames();
		if (remaining > 0)
		{
			--remaining;
			return true;
		}
		else if (remaining == 0)
		{
			return false;
		}

		// 無制限モード（-1）: systemUpdateResultを使用
		return siv3d_stub::systemUpdateResult();
	}

	/// @brief 終了トリガーを設定する
	static void SetTerminationTriggers(int /*triggers*/) {}
};

/// @brief ユーザーアクション定数
struct UserAction
{
	static constexpr int CloseButtonClicked = 1;
};

/// @brief 時間表現（秒）
struct SecondsF
{
	double count = 0.0;
	SecondsF() = default;
	explicit SecondsF(double s) : count(s) {}
};

/// @brief ミックスバス定数
inline constexpr int MixBus0 = 0;

/// @brief オーディオ
struct Audio
{
	int m_index = -1;  ///< audioRecords内のインデックス

	Audio() = default;

	/// @brief パス指定でロードする
	explicit Audio(const String& /*path*/)
	{
		m_index = static_cast<int>(siv3d_stub::audioRecords().size());
		siv3d_stub::audioRecords().push_back({"audio", 1.0, false, true});
	}

	/// @brief 有効判定
	explicit operator bool() const
	{
		return m_index >= 0
			&& m_index < static_cast<int>(siv3d_stub::audioRecords().size())
			&& siv3d_stub::audioRecords()[m_index].valid;
	}

	/// @brief ボリュームを設定する
	void setVolume(double vol)
	{
		if (isValid())
		{
			siv3d_stub::audioRecords()[m_index].volume = vol;
			siv3d_stub::drawCalls().push_back({
				siv3d_stub::DrawType::AudioSetVolume,
				{vol}, 0, 0, 0, 0
			});
		}
	}

	/// @brief ミックスバス指定で再生する
	void play(int /*mixBus*/)
	{
		if (isValid())
		{
			siv3d_stub::audioRecords()[m_index].playing = true;
			siv3d_stub::drawCalls().push_back({
				siv3d_stub::DrawType::AudioPlay,
				{}, 0, 0, 0, 0
			});
		}
	}

	/// @brief 再生する
	void play()
	{
		if (isValid())
		{
			siv3d_stub::audioRecords()[m_index].playing = true;
			siv3d_stub::drawCalls().push_back({
				siv3d_stub::DrawType::AudioPlay,
				{}, 0, 0, 0, 0
			});
		}
	}

	/// @brief 停止する
	void stop()
	{
		if (isValid())
		{
			siv3d_stub::audioRecords()[m_index].playing = false;
			siv3d_stub::drawCalls().push_back({
				siv3d_stub::DrawType::AudioStop,
				{}, 0, 0, 0, 0
			});
		}
	}

	/// @brief フェードアウト停止する
	void stop(const SecondsF& /*fadeOut*/)
	{
		if (isValid())
		{
			siv3d_stub::audioRecords()[m_index].playing = false;
			siv3d_stub::drawCalls().push_back({
				siv3d_stub::DrawType::AudioStop,
				{}, 0, 0, 0, 0
			});
		}
	}

	/// @brief 一時停止する
	void pause()
	{
		if (isValid())
		{
			siv3d_stub::audioRecords()[m_index].playing = false;
			siv3d_stub::drawCalls().push_back({
				siv3d_stub::DrawType::AudioPause,
				{}, 0, 0, 0, 0
			});
		}
	}

	/// @brief 再生中か
	[[nodiscard]] bool isPlaying() const
	{
		return isValid() && siv3d_stub::audioRecords()[m_index].playing;
	}

	/// @brief ワンショット再生する
	void playOneShot()
	{
		if (isValid())
		{
			siv3d_stub::audioRecords()[m_index].playing = true;
			siv3d_stub::drawCalls().push_back({
				siv3d_stub::DrawType::AudioPlayOneShot,
				{}, 0, 0, 0, 0
			});
		}
	}

private:
	[[nodiscard]] bool isValid() const
	{
		return m_index >= 0
			&& m_index < static_cast<int>(siv3d_stub::audioRecords().size());
	}
};

// ── XInput スタブ ─────────────────────────────────────

/// @brief XInputコントローラーのスタブ実装
struct XInput_impl
{
	bool m_connected = false;

	Input buttonA{200};
	Input buttonB{201};
	Input buttonX{202};
	Input buttonY{203};
	Input buttonLB{204};
	Input buttonRB{205};
	Input buttonBack{206};
	Input buttonStart{207};
	Input buttonMenu{208};
	Input buttonLThumb{209};
	Input buttonRThumb{210};
	Input buttonUp{211};
	Input buttonDown{212};
	Input buttonLeft{213};
	Input buttonRight{214};

	double leftThumbX = 0.0;
	double leftThumbY = 0.0;
	double rightThumbX = 0.0;
	double rightThumbY = 0.0;
	double leftTrigger = 0.0;
	double rightTrigger = 0.0;

	double m_vibLeft = 0.0;
	double m_vibRight = 0.0;

	/// @brief 接続状態を取得する
	[[nodiscard]] bool isConnected() const { return m_connected; }

	/// @brief 左バイブレーションを設定する
	void setLeftVibration(double v) { m_vibLeft = v; }

	/// @brief 右バイブレーションを設定する
	void setRightVibration(double v) { m_vibRight = v; }
};

/// @brief XInputスタブインスタンスの配列を取得する
inline XInput_impl& XInput(int index)
{
	static XInput_impl controllers[4];
	return controllers[index];
}

} // namespace s3d

namespace siv3d_stub
{

/// @brief XInputスタブをリセットする
inline void resetXInput()
{
	for (int i = 0; i < 4; ++i)
	{
		auto& xi = s3d::XInput(i);
		xi.m_connected = false;
		xi.leftThumbX = 0.0;
		xi.leftThumbY = 0.0;
		xi.rightThumbX = 0.0;
		xi.rightThumbY = 0.0;
		xi.leftTrigger = 0.0;
		xi.rightTrigger = 0.0;
		xi.m_vibLeft = 0.0;
		xi.m_vibRight = 0.0;
		// ボタン状態のリセット（keyPressed配列を使用）
		for (int id = 200; id <= 214; ++id)
		{
			siv3d_stub::keyPressed()[id] = false;
			siv3d_stub::keyDown()[id] = false;
			siv3d_stub::keyUp()[id] = false;
		}
	}
}

} // namespace siv3d_stub
