#pragma once

/// @file MoviePlayer.hpp
/// @brief ムービー再生インターフェース・プレイヤー
/// @details OP/ED動画やイベントCGムービーの再生を抽象化する。
///          IMovieDecoderインターフェースを実装することで任意のコーデック
///          （FFmpeg, libvpx等）を差し替え可能。NullMovieDecoderにより
///          コーデック実装なしでもテスト・開発が可能。
///
/// @code
/// mitiru::vn::MoviePlayer player;
/// player.onFinished([](){ /* 次のシーンへ */ });
/// player.play("opening.mp4");
///
/// // メインループ内:
/// player.update(dt);
/// if (player.state() == MovieState::Playing) {
///     screen.drawSprite(player.currentFrame(), fullScreenRect);
/// }
///
/// // スキップ入力:
/// if (enterPressed) { player.skip(); }
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <mitiru/render/Texture.hpp>

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  データ型
// ════════════════════════════════════════════════════════════════════

/// @brief デコード済みの1フレーム
struct MovieFrame
{
	int width = 0;								///< フレーム幅（ピクセル）
	int height = 0;								///< フレーム高さ（ピクセル）
	std::vector<std::uint8_t> pixels;			///< RGBA8ピクセルバッファ
	double timestamp = 0.0;						///< フレームのプレゼンテーション時刻（秒）
};

/// @brief デコード済みのオーディオサンプルバッファ
struct MovieAudioBuffer
{
	std::vector<float> samples;					///< インターリーブPCMサンプル
	int sampleRate = 44100;						///< サンプリングレート
	int channels = 2;							///< チャンネル数
};

/// @brief ムービープレイヤーの状態
enum class MovieState : std::uint8_t
{
	Idle,										///< 初期状態・停止済み
	Loading,									///< ファイル読み込み中
	Playing,									///< 再生中
	Paused,										///< 一時停止中
	Finished,									///< 再生完了
};

/// @brief フェード設定
struct MovieFadeConfig
{
	float fadeInDuration = 0.5f;				///< フェードイン時間（秒）
	float fadeOutDuration = 0.5f;				///< フェードアウト時間（秒）
	bool enabled = true;						///< フェード有効/無効
};

// ════════════════════════════════════════════════════════════════════
//  IMovieDecoder。デコーダー抽象インターフェース
// ════════════════════════════════════════════════════════════════════

/// @brief ムービーデコーダー抽象インターフェース
/// @details FFmpeg, libvpx等の具体的コーデック実装はこのインターフェースを
///          実装して提供する。
class IMovieDecoder
{
public:
	virtual ~IMovieDecoder() = default;

	/// @brief 動画ファイルを開く
	/// @param path ファイルパス
	/// @return 成功ならtrue
	[[nodiscard]] virtual bool open(const std::string& path) = 0;

	/// @brief 次のフレームをデコードする
	/// @return デコード済みフレーム（末端に達した場合はnullopt）
	[[nodiscard]] virtual std::optional<MovieFrame> readFrame() = 0;

	/// @brief 指定時刻にシークする
	/// @param seconds シーク先の時刻（秒）
	virtual void seek(double seconds) = 0;

	/// @brief 動画の総再生時間を取得する
	/// @return 再生時間（秒）
	[[nodiscard]] virtual double duration() const = 0;

	/// @brief 現在の再生時刻を取得する
	/// @return 現在時刻（秒）
	[[nodiscard]] virtual double currentTime() const = 0;

	/// @brief 動画がすべてデコード済みか
	/// @return デコード完了ならtrue
	[[nodiscard]] virtual bool isFinished() const = 0;

	/// @brief オーディオサンプルを取得する（音声同期用）
	/// @return オーディオバッファ（音声トラックがない場合はnullopt）
	[[nodiscard]] virtual std::optional<MovieAudioBuffer> audioSamples() = 0;

	/// @brief デコーダーを閉じてリソースを解放する
	virtual void close() = 0;
};

// ════════════════════════════════════════════════════════════════════
//  NullMovieDecoder。テスト用スタブ実装
// ════════════════════════════════════════════════════════════════════

/// @brief テスト用ムービーデコーダースタブ
/// @details 実際のコーデックなしで動作する。単色フレームを生成し、
///          指定した擬似再生時間で完了する。
class NullMovieDecoder final : public IMovieDecoder
{
public:
	/// @brief コンストラクタ
	/// @param fakeDuration 擬似再生時間（秒）
	/// @param frameWidth フレーム幅
	/// @param frameHeight フレーム高さ
	/// @param fps 擬似FPS
	explicit NullMovieDecoder(double fakeDuration = 5.0,
	                          int frameWidth = 320,
	                          int frameHeight = 240,
	                          double fps = 30.0) noexcept
		: m_fakeDuration(fakeDuration)
		, m_frameWidth(frameWidth)
		, m_frameHeight(frameHeight)
		, m_frameInterval(fps > 0.0 ? 1.0 / fps : 1.0 / 30.0)
	{
	}

	[[nodiscard]] bool open([[maybe_unused]] const std::string& path) override
	{
		m_currentTime = 0.0;
		m_finished = false;
		m_opened = true;
		return true;
	}

	[[nodiscard]] std::optional<MovieFrame> readFrame() override
	{
		if (!m_opened || m_finished)
		{
			return std::nullopt;
		}

		if (m_currentTime >= m_fakeDuration)
		{
			m_finished = true;
			return std::nullopt;
		}

		// 時間経過に応じてグラデーションの色が変化する単色フレーム
		const float progress = static_cast<float>(m_currentTime / m_fakeDuration);
		const auto r = static_cast<std::uint8_t>(30 + progress * 50);
		const auto g = static_cast<std::uint8_t>(30 + progress * 30);
		const auto b = static_cast<std::uint8_t>(60 + progress * 80);

		MovieFrame frame;
		frame.width = m_frameWidth;
		frame.height = m_frameHeight;
		frame.timestamp = m_currentTime;
		frame.pixels.resize(
			static_cast<std::size_t>(m_frameWidth) * m_frameHeight * 4);

		for (int i = 0; i < m_frameWidth * m_frameHeight; ++i)
		{
			const auto idx = static_cast<std::size_t>(i) * 4;
			frame.pixels[idx + 0] = r;
			frame.pixels[idx + 1] = g;
			frame.pixels[idx + 2] = b;
			frame.pixels[idx + 3] = 255;
		}

		m_currentTime += m_frameInterval;
		return frame;
	}

	void seek(double seconds) override
	{
		m_currentTime = std::clamp(seconds, 0.0, m_fakeDuration);
		m_finished = (m_currentTime >= m_fakeDuration);
	}

	[[nodiscard]] double duration() const override { return m_fakeDuration; }
	[[nodiscard]] double currentTime() const override { return m_currentTime; }
	[[nodiscard]] bool isFinished() const override { return m_finished; }

	[[nodiscard]] std::optional<MovieAudioBuffer> audioSamples() override
	{
		return std::nullopt; // スタブ実装：音声なし
	}

	void close() override
	{
		m_opened = false;
		m_finished = true;
		m_currentTime = 0.0;
	}

private:
	double m_fakeDuration;
	int m_frameWidth;
	int m_frameHeight;
	double m_frameInterval;
	double m_currentTime = 0.0;
	bool m_finished = false;
	bool m_opened = false;
};

// ════════════════════════════════════════════════════════════════════
//  MoviePlayer。ムービー再生制御
// ════════════════════════════════════════════════════════════════════

/// @brief ムービー再生プレイヤー
/// @details IMovieDecoderを駆動し、フレーム更新・状態管理・フェード・
///          スキップ機能を提供する。currentFrame()でレンダリング用テクスチャを
///          取得し、Screen::drawSpriteで描画する。
///
/// ScenarioScript統合:
/// @code
/// @movie "opening.mp4"
/// @endcode
class MoviePlayer
{
public:
	/// @brief コールバック型
	using FrameReadyCallback = std::function<void(const render::Texture&)>;
	using FinishedCallback = std::function<void()>;

	/// @brief コンストラクタ
	/// @param decoder ムービーデコーダー（所有権を移動）
	explicit MoviePlayer(std::unique_ptr<IMovieDecoder> decoder = nullptr)
		: m_decoder(std::move(decoder))
	{
	}

	/// @brief デコーダーを設定する
	/// @param decoder ムービーデコーダー（所有権を移動）
	void setDecoder(std::unique_ptr<IMovieDecoder> decoder)
	{
		stop();
		m_decoder = std::move(decoder);
	}

	/// @brief フェード設定を変更する
	/// @param config フェード設定
	void setFadeConfig(const MovieFadeConfig& config) noexcept
	{
		m_fadeConfig = config;
	}

	/// @brief フェード設定を取得する
	/// @return 現在のフェード設定
	[[nodiscard]] const MovieFadeConfig& fadeConfig() const noexcept
	{
		return m_fadeConfig;
	}

	// ── 再生制御 ──────────────────────────────────────────────

	/// @brief ムービー再生を開始する
	/// @param path ファイルパス
	/// @return 開始に成功したらtrue
	bool play(const std::string& path)
	{
		if (!m_decoder)
		{
			return false;
		}

		stop();

		m_state = MovieState::Loading;
		if (!m_decoder->open(path))
		{
			m_state = MovieState::Idle;
			return false;
		}

		m_state = MovieState::Playing;
		m_elapsedTime = 0.0;
		m_fadeAlpha = 0.0f;
		m_currentPath = path;

		// 最初のフレームをデコード
		advanceFrame();
		return true;
	}

	/// @brief 再生を一時停止する
	void pause()
	{
		if (m_state == MovieState::Playing)
		{
			m_state = MovieState::Paused;
		}
	}

	/// @brief 一時停止から再開する
	void resume()
	{
		if (m_state == MovieState::Paused)
		{
			m_state = MovieState::Playing;
		}
	}

	/// @brief 再生を停止する
	void stop()
	{
		if (m_decoder && m_state != MovieState::Idle)
		{
			m_decoder->close();
		}
		m_state = MovieState::Idle;
		m_elapsedTime = 0.0;
		m_fadeAlpha = 1.0f;
		m_currentPath.clear();
	}

	/// @brief 指定時刻にシークする
	/// @param seconds シーク先の時刻（秒）
	void seek(double seconds)
	{
		if (!m_decoder) { return; }
		m_decoder->seek(seconds);
		m_elapsedTime = seconds;
		advanceFrame();
	}

	/// @brief ムービーをスキップする（Enter等で即座に終了）
	void skip()
	{
		if (m_state == MovieState::Playing || m_state == MovieState::Paused)
		{
			if (m_decoder)
			{
				m_decoder->close();
			}
			m_state = MovieState::Finished;
			notifyFinished();
		}
	}

	// ── 更新 ──────────────────────────────────────────────────

	/// @brief フレーム更新
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		if (m_state != MovieState::Playing)
		{
			return;
		}

		m_elapsedTime += static_cast<double>(dt);

		// フェードアルファの計算
		updateFadeAlpha();

		// デコーダーからフレームを読み進める
		if (m_decoder && !m_decoder->isFinished())
		{
			// 現在の再生時刻に追いつくまでフレームを読む
			while (m_decoder->currentTime() < m_elapsedTime && !m_decoder->isFinished())
			{
				advanceFrame();
			}
		}

		// 再生完了チェック
		if (m_decoder && m_decoder->isFinished())
		{
			m_state = MovieState::Finished;
			notifyFinished();
		}
	}

	// ── 状態取得 ──────────────────────────────────────────────

	/// @brief 現在の状態を取得する
	[[nodiscard]] MovieState state() const noexcept { return m_state; }

	/// @brief 現在のフレームテクスチャを取得する
	/// @return 現在フレームのテクスチャ参照
	[[nodiscard]] const render::Texture& currentFrame() const noexcept
	{
		return m_currentTexture;
	}

	/// @brief 現在のフェードアルファ値を取得する（0.0=透明, 1.0=不透明）
	[[nodiscard]] float fadeAlpha() const noexcept { return m_fadeAlpha; }

	/// @brief 再生中のファイルパスを取得する
	[[nodiscard]] const std::string& currentPath() const noexcept
	{
		return m_currentPath;
	}

	/// @brief 動画の総再生時間を取得する
	[[nodiscard]] double duration() const
	{
		return m_decoder ? m_decoder->duration() : 0.0;
	}

	/// @brief 現在の再生時刻を取得する
	[[nodiscard]] double currentTime() const noexcept { return m_elapsedTime; }

	/// @brief 再生が完了したか
	[[nodiscard]] bool isFinished() const noexcept
	{
		return m_state == MovieState::Finished;
	}

	// ── コールバック ──────────────────────────────────────────

	/// @brief フレーム更新時コールバックを設定する
	/// @param callback コールバック関数
	void onFrameReady(FrameReadyCallback callback)
	{
		m_onFrameReady = std::move(callback);
	}

	/// @brief 再生完了時コールバックを設定する
	/// @param callback コールバック関数
	void onFinished(FinishedCallback callback)
	{
		m_onFinished = std::move(callback);
	}

private:
	/// @brief 次のフレームをデコードしてテクスチャを更新する
	void advanceFrame()
	{
		if (!m_decoder) { return; }

		auto frame = m_decoder->readFrame();
		if (!frame.has_value()) { return; }

		m_currentTexture = render::Texture(
			frame->width, frame->height, frame->pixels);

		if (m_onFrameReady)
		{
			m_onFrameReady(m_currentTexture);
		}
	}

	/// @brief フェードアルファを更新する
	void updateFadeAlpha()
	{
		if (!m_fadeConfig.enabled || !m_decoder)
		{
			m_fadeAlpha = 1.0f;
			return;
		}

		const double totalDuration = m_decoder->duration();

		if (m_elapsedTime < m_fadeConfig.fadeInDuration)
		{
			// フェードイン中
			m_fadeAlpha = static_cast<float>(
				m_elapsedTime / m_fadeConfig.fadeInDuration);
		}
		else if (m_elapsedTime > totalDuration - m_fadeConfig.fadeOutDuration)
		{
			// フェードアウト中
			const double remaining = totalDuration - m_elapsedTime;
			m_fadeAlpha = static_cast<float>(
				std::max(0.0, remaining / m_fadeConfig.fadeOutDuration));
		}
		else
		{
			m_fadeAlpha = 1.0f;
		}

		m_fadeAlpha = std::clamp(m_fadeAlpha, 0.0f, 1.0f);
	}

	/// @brief 完了通知を発火する
	void notifyFinished()
	{
		if (m_onFinished)
		{
			m_onFinished();
		}
	}

	// ── メンバ ────────────────────────────────────────────────

	std::unique_ptr<IMovieDecoder> m_decoder;			///< デコーダー
	MovieState m_state = MovieState::Idle;				///< 現在の状態
	render::Texture m_currentTexture;					///< 現在フレームのテクスチャ
	double m_elapsedTime = 0.0;							///< 経過時間（秒）
	float m_fadeAlpha = 1.0f;							///< フェードアルファ
	std::string m_currentPath;							///< 再生中のファイルパス
	MovieFadeConfig m_fadeConfig;						///< フェード設定

	FrameReadyCallback m_onFrameReady;					///< フレーム更新コールバック
	FinishedCallback m_onFinished;						///< 完了コールバック
};

} // namespace mitiru::vn
