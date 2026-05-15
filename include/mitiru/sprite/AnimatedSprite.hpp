#pragma once

/// @file AnimatedSprite.hpp
/// @brief Frame-sequence sprite with playback state machine (G-06)
///
/// **Motivation.** pandd-dodo characters need animated sprites driven by
/// per-frame delay values (as produced by Aseprite and GIF formats). The
/// engine provides no dedicated animated-sprite class, forcing each game to
/// re-implement accumulator logic and state management.
/// `AnimatedSprite` owns the accumulator, state machine, and speed scaling
/// so games only call `update(dt)` and `currentFrame()`.
///
/// **Design decisions:**
/// - Header-only; no I/O dependency. Callers inject `GifDecoder` /
///   `AsepriteDecoder` callbacks, or supply frames directly via
///   `loadFromFrames()`.
/// - State machine has three states: `Stopped` (initial), `Playing`,
///   `Paused`. Transitions are explicit and exhaustive.
/// - `update(dt)` is frame-rate independent: accumulator is driven by
///   `dt * 1000.f * speed` (milliseconds).
/// - `mutable std::mutex` guards all public methods so `currentFrame()`
///   is safe to call from a render thread while `update()` runs on the
///   game thread.
///
/// **AsepriteLoader note:**
/// @note No standalone `AsepriteLoader` class exists in the engine.
///       `mitiru::render::SpriteSheetParser` (include/mitiru/render/SpriteSheetParser.hpp)
///       uses a different atlas-based model and is not directly compatible.
///       Aseprite JSON export can be parsed externally and injected via
///       the `AsepriteDecoder` callback passed to `setAsepriteDecoder()`.
///
/// **Usage (stub frames, no real I/O):**
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

/// @brief One animation frame: a texture and its display duration.
struct FrameData
{
	mitiru::render::Texture tex;
	int                     delay_ms = 100; ///< Display duration in milliseconds (> 0).
};

/// @brief Callback that decodes a GIF file into a sequence of FrameData.
using GifDecoder = std::function<std::vector<FrameData>(const std::filesystem::path&)>;

/// @brief Callback that decodes an Aseprite JSON+sheet export into FrameData.
/// @note  No built-in implementation is provided by the engine.
///        Callers must supply a decoder or use `loadFromFrames()` directly.
using AsepriteDecoder = std::function<std::vector<FrameData>(const std::filesystem::path&)>;

/// @brief Frame-sequence animated sprite with play/pause/loop/speed control.
///
/// State machine:  Stopped (initial) -> Playing <-> Paused
/// Loop=false: Playing -> Stopped when the last frame is exhausted.
class AnimatedSprite
{
public:
	// ── Construction ──────────────────────────────────────────────────

	/// @brief Constructs an empty, stopped sprite.
	AnimatedSprite() = default;

	AnimatedSprite(const AnimatedSprite&)            = delete;
	AnimatedSprite& operator=(const AnimatedSprite&) = delete;
	// Move is also not supported: std::mutex member is non-movable.
	AnimatedSprite(AnimatedSprite&&)            = delete;
	AnimatedSprite& operator=(AnimatedSprite&&) = delete;

	// ── Decoder injection ─────────────────────────────────────────────

	/// @brief Inject a GIF decoder. Called by `loadGIF()`.
	void setGifDecoder(GifDecoder decoder)
	{
		std::lock_guard lock(m_mutex);
		m_gifDecoder = std::move(decoder);
	}

	/// @brief Inject an Aseprite decoder. Called by `loadAseprite()`.
	/// @note  No engine-native implementation exists; supply your own.
	void setAsepriteDecoder(AsepriteDecoder decoder)
	{
		std::lock_guard lock(m_mutex);
		m_aseDecoder = std::move(decoder);
	}

	// ── Frame loading ─────────────────────────────────────────────────

	/// @brief Load frames from a GIF file using the injected `GifDecoder`.
	/// @throws std::logic_error  if no GIF decoder has been injected.
	/// @throws std::runtime_error if the decoder returns no frames.
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

	/// @brief Load frames from an Aseprite export using the injected `AsepriteDecoder`.
	/// @throws std::logic_error  if no Aseprite decoder has been injected.
	/// @throws std::runtime_error if the decoder returns no frames.
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

	/// @brief Load frames from pre-constructed textures and per-frame delays.
	/// @param textures  Sequence of textures, one per frame.
	/// @param delays_ms Per-frame display duration in milliseconds.
	/// @throws std::invalid_argument if sizes differ.
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

	// ── Playback control ──────────────────────────────────────────────

	/// @brief Start or resume playback.
	/// @details No-op on an empty sprite or if already Playing.
	void play()
	{
		std::lock_guard lock(m_mutex);
		if (m_frames.empty()) { return; }
		m_state = State::Playing;
	}

	/// @brief Pause playback. No-op if Stopped or already Paused.
	void pause()
	{
		std::lock_guard lock(m_mutex);
		if (m_state == State::Playing)
		{
			m_state = State::Paused;
		}
	}

	/// @brief Enable or disable looping.
	/// @details When loop=false and the last frame is exhausted, state -> Stopped.
	void setLoop(bool loop)
	{
		std::lock_guard lock(m_mutex);
		m_loop = loop;
	}

	/// @brief Set playback speed multiplier.
	/// @param speed  1.0 = normal; 2.0 = twice as fast; 0.5 = half speed.
	///               Values <= 0 are clamped to a small positive epsilon.
	void setSpeed(float speed)
	{
		std::lock_guard lock(m_mutex);
		m_speed = (speed > 0.f) ? speed : 1e-6f;
	}

	// ── Callbacks ─────────────────────────────────────────────────────

	/// @brief Register a callback fired each time the animation loops back to frame 0.
	/// @details Only fires when loop=true and the sequence wraps. Not called on
	///          the first play() or when the animation stops.
	void setOnLoop(std::function<void()> cb)
	{
		std::lock_guard lock(m_mutex);
		m_onLoop = std::move(cb);
	}

	// ── Update ────────────────────────────────────────────────────────

	/// @brief Advance the animation by `dt` seconds (frame-rate independent).
	/// @param dt  Elapsed time in seconds since the last call.
	void update(float dt)
	{
		std::lock_guard lock(m_mutex);
		if (m_state != State::Playing) { return; }
		if (m_frames.empty()) { return; }
		if (m_frames.size() == 1) { return; } // single frame: never advances

		m_accumMs += dt * 1000.f * m_speed;

		while (m_accumMs >= static_cast<float>(currentDelay()))
		{
			m_accumMs -= static_cast<float>(currentDelay());
			advanceFrame();
			if (m_state != State::Playing) { break; } // advanceFrame() may stop
		}
	}

	// ── Queries ───────────────────────────────────────────────────────

	/// @brief Returns the texture for the current frame.
	/// @return Empty (default-constructed) Texture if no frames are loaded.
	[[nodiscard]] mitiru::render::Texture currentFrame() const
	{
		std::lock_guard lock(m_mutex);
		if (m_frames.empty()) { return {}; }
		return m_frames[m_frameIndex].tex;
	}

	/// @brief Returns the zero-based index of the current frame.
	[[nodiscard]] std::size_t frameIndex() const
	{
		std::lock_guard lock(m_mutex);
		return m_frameIndex;
	}

	/// @brief Returns true while state is Playing.
	[[nodiscard]] bool isPlaying() const
	{
		std::lock_guard lock(m_mutex);
		return m_state == State::Playing;
	}

	// ── stb_image factory (optional) ─────────────────────────────────
#ifdef MITIRU_HAS_STB_IMAGE
	/// @brief Create an AnimatedSprite by loading a GIF with stb_image.
	/// @note  Requires MITIRU_HAS_STB_IMAGE to be defined at compile time.
	///        Calls stbi_load_from_file with STBI_rgb_alpha.
	/// @throws std::runtime_error on decode failure.
	[[nodiscard]] static AnimatedSprite fromStbGIF(const std::filesystem::path& path);
#endif

private:
	// ── Internal state machine ────────────────────────────────────────

	enum class State { Stopped, Playing, Paused };

	/// @brief Reset to first frame and clear accumulator. Must be called under lock.
	void resetState(std::vector<FrameData> frames)
	{
		m_frames     = std::move(frames);
		m_frameIndex = 0;
		m_accumMs    = 0.f;
		m_state      = State::Stopped;
	}

	/// @brief Return the delay_ms for the current frame. Must be called under lock.
	[[nodiscard]] int currentDelay() const
	{
		assert(!m_frames.empty());
		const int d = m_frames[m_frameIndex].delay_ms;
		return (d > 0) ? d : 1; // guard against zero/negative
	}

	/// @brief Move to the next frame; handle end-of-sequence. Must be called under lock.
	void advanceFrame()
	{
		const std::size_t last = m_frames.size() - 1u;

		if (m_frameIndex < last)
		{
			++m_frameIndex;
		}
		else
		{
			// End of sequence.
			if (m_loop)
			{
				m_frameIndex = 0;
				if (m_onLoop) { m_onLoop(); }
			}
			else
			{
				// Clamp at last frame and stop.
				m_state = State::Stopped;
			}
		}
	}

	// ── Data ──────────────────────────────────────────────────────────

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
