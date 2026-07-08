#pragma once

/// @file AnimatedSprite.hpp
/// @brief 再生 state machine 付きの frame-sequence sprite (G-06)
///
/// **動機。** character animation は per-frame delay 値 (Aseprite や GIF
/// 形式が生成するもの) で駆動される animated sprite を必要とする。engine に
/// 専用の animated-sprite class が無いと、各 game が accumulator logic と state
/// 管理を再実装する羽目になる。
/// `AnimatedSprite` が accumulator・state machine・speed scaling を所有するので、
/// game は `update(dt)` と `currentFrame()` を呼ぶだけでよい。
///
/// **設計判断:**
/// - Header-only。I/O 依存無し。caller が `GifDecoder` / `AsepriteDecoder`
///   callback を inject するか、`loadFromFrames()` で frame を直接供給する。
/// - State machine は 3 状態: `Stopped` (初期)・`Playing`・`Paused`。遷移は
///   明示的かつ網羅的。
/// - `update(dt)` は frame-rate 非依存: accumulator は `dt * 1000.f * speed`
///   (ミリ秒) で駆動される。
/// - `mutable std::mutex` が全 public method を保護するので、`update()` が game
///   thread で走る間でも render thread から `currentFrame()` を安全に呼べる。
///
/// **AsepriteLoader に関する注意:**
/// @note engine には独立した `AsepriteLoader` class は存在しない。
///       `mitiru::render::SpriteSheetParser` (include/mitiru/render/SpriteSheetParser.hpp)
///       は別の atlas ベース model を使っており直接の互換性は無い。
///       Aseprite の JSON export は外部で parse し、`setAsepriteDecoder()` に渡す
///       `AsepriteDecoder` callback 経由で inject できる。
///
/// **使い方 (スタブ frame・実 I/O 無し):**
/// ```cpp
///   using mitiru::sprite::AnimatedSprite;
///   using mitiru::render::Texture;
///
///   AnimatedSprite anim;
///   anim.loadFromFrames(
///       {Texture::solid(4,4,255,0,0), Texture::solid(4,4,0,255,0)},
///       {100, 200});
///   anim.setLoop(true);
///   anim.play();
///
///   // game loop:
///   anim.update(deltaSeconds);
///   const Texture& frame = anim.currentFrame();
/// ```
///
/// **State transitions:**
/// ```
///   Stopped --play()--> Playing --pause()--> Paused
///   Paused  --play()--> Playing
///   Playing --pause()-> Paused
///   Playing (loop=false, last frame reached) --> Stopped
/// ```

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <mitiru/render/Texture.hpp>

namespace mitiru::sprite
{

/// @brief 1 つの animation frame: texture とその表示時間。
struct FrameData
{
	mitiru::render::Texture tex;
	int                     delay_ms = 100; ///< 表示時間 (ミリ秒・> 0)。
};

/// @brief GIF file を FrameData 列に decode する callback。
using GifDecoder = std::function<std::vector<FrameData>(const std::filesystem::path&)>;

/// @brief Aseprite の JSON+sheet export を FrameData に decode する callback。
/// @note  engine は組み込み実装を提供しない。
///        caller が decoder を供給するか、`loadFromFrames()` を直接使う。
using AsepriteDecoder = std::function<std::vector<FrameData>(const std::filesystem::path&)>;

/// @brief play/pause/loop/speed 制御付きの frame-sequence animated sprite。
///
/// State machine:  Stopped (初期) -> Playing <-> Paused
/// Loop=false: 最終 frame を消費しきると Playing -> Stopped。
class AnimatedSprite
{
public:
	// ── 構築 ──────────────────────────────────────────────────

	/// @brief 空で停止状態の sprite を構築する。
	AnimatedSprite() = default;

	AnimatedSprite(const AnimatedSprite&)            = delete;
	AnimatedSprite& operator=(const AnimatedSprite&) = delete;
	// move も非対応: std::mutex member が non-movable のため。
	AnimatedSprite(AnimatedSprite&&)            = delete;
	AnimatedSprite& operator=(AnimatedSprite&&) = delete;

	// ── Decoder の inject ─────────────────────────────────────────────

	/// @brief GIF decoder を inject する。`loadGIF()` から呼ばれる。
	void setGifDecoder(GifDecoder decoder)
	{
		std::lock_guard lock(m_mutex);
		m_gifDecoder = std::move(decoder);
	}

	/// @brief Aseprite decoder を inject する。`loadAseprite()` から呼ばれる。
	/// @note  engine ネイティブの実装は存在しない。自前のものを供給すること。
	void setAsepriteDecoder(AsepriteDecoder decoder)
	{
		std::lock_guard lock(m_mutex);
		m_aseDecoder = std::move(decoder);
	}

	// ── Frame の読み込み ─────────────────────────────────────────────────

	/// @brief inject 済みの `GifDecoder` を使って GIF file から frame を読み込む。
	/// @throws std::logic_error  GIF decoder が inject されていない場合。
	/// @throws std::runtime_error decoder が frame を 1 つも返さなかった場合。
	void loadGIF(const std::filesystem::path& path)
	{
		std::lock_guard lock(m_mutex);
		if (!m_gifDecoder)
		{
			throw std::logic_error(
				"[AnimatedSprite] loadGIF: no GifDecoder injected — "
				"call setGifDecoder() before loadGIF()");
		}
		auto frames = m_gifDecoder(path);
		if (frames.empty())
		{
			throw std::runtime_error(
				"[AnimatedSprite] loadGIF: decoder returned 0 frames for: "
				+ path.string());
		}
		resetState(std::move(frames));
	}

	/// @brief inject 済みの `AsepriteDecoder` を使って Aseprite export から frame を読み込む。
	/// @throws std::logic_error  Aseprite decoder が inject されていない場合。
	/// @throws std::runtime_error decoder が frame を 1 つも返さなかった場合。
	void loadAseprite(const std::filesystem::path& path)
	{
		std::lock_guard lock(m_mutex);
		if (!m_aseDecoder)
		{
			throw std::logic_error(
				"[AnimatedSprite] loadAseprite: no AsepriteDecoder injected — "
				"call setAsepriteDecoder() before loadAseprite()");
		}
		auto frames = m_aseDecoder(path);
		if (frames.empty())
		{
			throw std::runtime_error(
				"[AnimatedSprite] loadAseprite: decoder returned 0 frames for: "
				+ path.string());
		}
		resetState(std::move(frames));
	}

	/// @brief 構築済みの texture 群と per-frame delay から frame を読み込む。
	/// @param textures  texture 列。1 frame に 1 つ。
	/// @param delays_ms per-frame の表示時間 (ミリ秒)。
	/// @throws std::invalid_argument size が一致しない場合。
	void loadFromFrames(std::vector<mitiru::render::Texture> textures,
	                    std::vector<int>                     delays_ms)
	{
		if (textures.size() != delays_ms.size())
		{
			throw std::invalid_argument(
				"[AnimatedSprite] loadFromFrames: textures.size() ("
				+ std::to_string(textures.size())
				+ ") != delays_ms.size() ("
				+ std::to_string(delays_ms.size()) + ")");
		}

		std::vector<FrameData> frames;
		frames.reserve(textures.size());
		for (std::size_t i = 0; i < textures.size(); ++i)
		{
			frames.push_back({std::move(textures[i]), delays_ms[i]});
		}

		std::lock_guard lock(m_mutex);
		resetState(std::move(frames));
	}

	// ── 再生制御 ──────────────────────────────────────────────

	/// @brief 再生を開始または再開する。
	/// @details 空の sprite または既に Playing の場合は no-op。
	void play()
	{
		std::lock_guard lock(m_mutex);
		if (m_frames.empty()) { return; }
		m_state = State::Playing;
	}

	/// @brief 再生を一時停止する。Stopped または既に Paused なら no-op。
	void pause()
	{
		std::lock_guard lock(m_mutex);
		if (m_state == State::Playing)
		{
			m_state = State::Paused;
		}
	}

	/// @brief loop を有効 / 無効にする。
	/// @details loop=false で最終 frame を消費しきると state -> Stopped。
	void setLoop(bool loop)
	{
		std::lock_guard lock(m_mutex);
		m_loop = loop;
	}

	/// @brief 再生速度の倍率を設定する。
	/// @param speed  1.0 = 通常; 2.0 = 2 倍速; 0.5 = 半分の速度。
	///               <= 0 の値は微小な正の epsilon に clamp される。
	void setSpeed(float speed)
	{
		std::lock_guard lock(m_mutex);
		m_speed = (speed > 0.f) ? speed : 1e-6f;
	}

	// ── Callback ─────────────────────────────────────────────────────

	/// @brief animation が frame 0 に loop back するたびに発火する callback を登録する。
	/// @details loop=true で sequence が wrap した時のみ発火。最初の play() 時や
	///          animation 停止時には呼ばれない。
	void setOnLoop(std::function<void()> cb)
	{
		std::lock_guard lock(m_mutex);
		m_onLoop = std::move(cb);
	}

	// ── Update ────────────────────────────────────────────────────────

	/// @brief animation を `dt` 秒だけ進める (frame-rate 非依存)。
	/// @param dt  前回呼び出しからの経過時間 (秒)。
	void update(float dt)
	{
		std::lock_guard lock(m_mutex);
		if (m_state != State::Playing) { return; }
		if (m_frames.empty()) { return; }
		if (m_frames.size() == 1) { return; } // single frame: 決して advance しない

		m_accumMs += dt * 1000.f * m_speed;

		while (m_accumMs >= static_cast<float>(currentDelay()))
		{
			m_accumMs -= static_cast<float>(currentDelay());
			advanceFrame();
			if (m_state != State::Playing) { break; } // advanceFrame() が stop しうる
		}
	}

	// ── 問い合わせ ───────────────────────────────────────────────────────

	/// @brief 現在の frame の texture を返す。
	/// @return frame が 1 つも読み込まれていなければ空 (default 構築) の Texture。
	[[nodiscard]] mitiru::render::Texture currentFrame() const
	{
		std::lock_guard lock(m_mutex);
		if (m_frames.empty()) { return {}; }
		return m_frames[m_frameIndex].tex;
	}

	/// @brief 現在の frame の 0 始まり index を返す。
	[[nodiscard]] std::size_t frameIndex() const
	{
		std::lock_guard lock(m_mutex);
		return m_frameIndex;
	}

	/// @brief state が Playing の間 true を返す。
	[[nodiscard]] bool isPlaying() const
	{
		std::lock_guard lock(m_mutex);
		return m_state == State::Playing;
	}

	// ── stb_image factory (optional) ─────────────────────────────────
#ifdef MITIRU_HAS_STB_IMAGE
	/// @brief stb_image で GIF を読み込んで AnimatedSprite を生成する。
	/// @note  compile 時に MITIRU_HAS_STB_IMAGE が定義されている必要がある。
	///        stbi_load_from_file を STBI_rgb_alpha で呼ぶ。
	/// @throws std::runtime_error decode 失敗時。
	[[nodiscard]] static AnimatedSprite fromStbGIF(const std::filesystem::path& path);
#endif

private:
	// ── 内部 state machine ────────────────────────────────────────

	enum class State { Stopped, Playing, Paused };

	/// @brief 先頭 frame に戻し accumulator を clear する。lock 下で呼ぶこと。
	void resetState(std::vector<FrameData> frames)
	{
		m_frames     = std::move(frames);
		m_frameIndex = 0;
		m_accumMs    = 0.f;
		m_state      = State::Stopped;
	}

	/// @brief 現在の frame の delay_ms を返す。lock 下で呼ぶこと。
	[[nodiscard]] int currentDelay() const
	{
		assert(!m_frames.empty());
		const int d = m_frames[m_frameIndex].delay_ms;
		return (d > 0) ? d : 1; // ゼロ / 負値に対するガード
	}

	/// @brief 次の frame へ移動し、sequence 終端を処理する。lock 下で呼ぶこと。
	void advanceFrame()
	{
		const std::size_t last = m_frames.size() - 1u;

		if (m_frameIndex < last)
		{
			++m_frameIndex;
		}
		else
		{
			// sequence の終端。
			if (m_loop)
			{
				m_frameIndex = 0;
				if (m_onLoop) { m_onLoop(); }
			}
			else
			{
				// 最終 frame で clamp して停止する。
				m_state = State::Stopped;
			}
		}
	}

	// ── データ ──────────────────────────────────────────────────────────

	std::vector<FrameData>   m_frames;
	std::size_t              m_frameIndex = 0;
	float                    m_accumMs    = 0.f;
	float                    m_speed      = 1.f;
	bool                     m_loop       = false;
	State                    m_state      = State::Stopped;

	GifDecoder               m_gifDecoder;
	AsepriteDecoder          m_aseDecoder;

	std::function<void()>    m_onLoop;

	mutable std::mutex       m_mutex;
};

} // namespace mitiru::sprite
