#pragma once

/// @file VideoHost.hpp
/// @brief host が持つ動画置き場の宣言。Screen::drawVideo が指定した id の時点の画像を render::Texture で返す (ADR 0104)。
/// @details ゲームの DLL は host オブジェクトを持たないため、Screen::drawVideo は `video:` の id を SpriteCache::resolve に渡し、host が VideoHost へ回す。Texture は動画ごとに 1 つあり、アドレスを保ったまま中身だけを入れ替える。GPU 側は同じ key のまま行だけ更新できる。動画を使わないときは VideoHost を作らない。

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/video/IVideoSource.hpp>
#include <mitiru/video/VideoStream.hpp>
#include <mitiru/video/VideoTypes.hpp>

namespace mitiru::video
{

/// bytes から動画を開く。失敗したときは nullptr を返し、error に理由を入れる
using VideoOpener = std::unique_ptr<IVideoSource> (*)(std::vector<std::uint8_t>&& bytes, std::string* error);

/// 画像が入れ替わったときに host が呼ぶ。GPU 側に画像があれば、その場で行を更新する
using VideoUploadHook = void (*)(void* ctx, const render::Texture& tex);

struct VideoHostConfig
{
	std::string      dir = "assets/video";
	std::string      ext = ".mpg";
	bool             exact = false;   ///< 撮影・headless・リプレイ。間に合うまで待つ
	std::uint64_t    idleFrames = 180;   ///< これだけ引かれない動画は閉じる (thread と先読みの絵を手放す)
	VideoStreamConfig stream;
};

class VideoHost
{
public:
	VideoHost(VideoOpener opener, VideoHostConfig cfg) : m_opener(opener), m_cfg(std::move(cfg)) {}

	/// dir はゲームの DLL の隣にある assets/video を指す。sprite と同じ規約で、pack 配布ではこの相対パスが pack の鍵になる。
	/// dir が変わったときは、別の DLL にある同名の動画と取り違えないよう、開いていた動画を閉じる
	void setDir(std::string dir)
	{
		if (dir == m_cfg.dir) { return; }
		m_cfg.dir = std::move(dir);
		m_entries.clear();
	}
	void setExact(bool exact) noexcept { m_cfg.exact = exact; }
	[[nodiscard]] bool exact() const noexcept { return m_cfg.exact; }
	void setUploadHook(VideoUploadHook fn, void* ctx) noexcept { m_upload = fn; m_uploadCtx = ctx; }

	/// SpriteCache::setVideoResolver に渡す。ctx は VideoHost* を指す
	[[nodiscard]] static const render::Texture* resolveThunk(void* ctx, const char* id)
	{
		return ctx != nullptr ? static_cast<VideoHost*>(ctx)->resolve(id) : nullptr;
	}

	/// @brief id が指す時点の画像を返す。開けないときや、まだ 1 枚も出せないときは nullptr を返す。返した Texture は次の resolve まで有効
	[[nodiscard]] const render::Texture* resolve(const char* id)
	{
		ParsedVideoId parsed;
		if (!parseVideoId(id, parsed))
		{
			debug::warnOnce("video.bad_id", "動画の頼み方が壊れているので描きません (drawVideo の名前は 96 byte まで)");
			return nullptr;
		}
		Entry* entry = entryFor(parsed.name);
		if (entry == nullptr || !entry->stream) { return nullptr; }
		const int index = frameIndexAt(static_cast<double>(parsed.seconds), entry->stream->info().fps);
		const auto frame = entry->stream->acquire(index, m_cfg.exact);
		if (!frame || !frame->tex.valid()) { return nullptr; }
		entry->lastUsed = m_frameCounter;
		if (alphaOf(parsed.tint) == 0) { return nullptr; }
		store(*entry, *frame, parsed.tint);
		return &entry->out;
	}

	/// @brief フレームの終わりに呼ぶ。しばらく resolve されなかった動画を閉じ、間に合わなかった回数があれば 1 度だけ知らせる
	void endFrame()
	{
		++m_frameCounter;
		for (auto it = m_entries.begin(); it != m_entries.end();)
		{
			Entry& e = it->second;
			if (e.stream && e.stream->droppedFrames() > 0 && !m_cfg.exact)
			{
				debug::warnOnce("video.dropped", "動画の復号が間に合わず、直前の絵を出し続けました (動画を小さくしてください)");
			}
			if (m_frameCounter > e.lastUsed + m_cfg.idleFrames) { it = m_entries.erase(it); }
			else { ++it; }
		}
	}

	[[nodiscard]] std::size_t openCount() const noexcept { return m_entries.size(); }

private:
	struct Entry
	{
		std::unique_ptr<VideoStream> stream;   ///< nullptr は開けなかった (知らせ済み。毎フレームは開き直さない)
		render::Texture out;
		int             outIndex = -1;
		std::uint32_t   outTint = kTintWhite;
		std::uint64_t   lastUsed = 0;
	};

	[[nodiscard]] static std::uint32_t alphaOf(std::uint32_t tint) noexcept { return tint & 0xFFu; }

	Entry* entryFor(const std::string& name)
	{
		if (const auto it = m_entries.find(name); it != m_entries.end())
		{
			it->second.lastUsed = m_frameCounter;
			return &it->second;
		}
		Entry entry;
		entry.lastUsed = m_frameCounter;
		entry.stream = open(name);
		return &m_entries.emplace(name, std::move(entry)).first->second;
	}

	[[nodiscard]] std::unique_ptr<VideoStream> open(const std::string& name)
	{
		if (m_opener == nullptr) { return nullptr; }
		const std::string path = m_cfg.dir + "/" + name + m_cfg.ext;
		auto bytes = vfs::readGlobal(path);
		if (!bytes || bytes->empty())
		{
			debug::warnOnce("video.missing." + name, "動画 " + path + " が読めません (assets/video に .mpg を置く)");
			return nullptr;
		}
		std::string error;
		auto source = m_opener(std::move(*bytes), &error);
		if (!source || source->info().width <= 0 || source->info().height <= 0 || !(source->info().fps > 0.0))
		{
			debug::warnOnce("video.open." + name, "動画 " + path + " を開けません (" + (error.empty() ? std::string("映像が見つかりません") : error) + ")");
			return nullptr;
		}
		return std::make_unique<VideoStream>(std::move(source), m_cfg.stream);
	}

	void store(Entry& e, const VideoFrame& frame, std::uint32_t tint)
	{
		const auto& src = frame.tex.pixels();
		if (!e.out.valid())
		{
			e.out = render::Texture(frame.tex.width(), frame.tex.height(), src);
			e.outIndex = -1;
		}
		if (e.outIndex == frame.index && e.outTint == tint) { return; }
		if (tint == kTintWhite)
		{
			e.out.overwritePixels(src.data());
		}
		else
		{
			std::vector<std::uint8_t> baked(src.size());
			const std::uint32_t tr = (tint >> 24) & 0xFFu;
			const std::uint32_t tg = (tint >> 16) & 0xFFu;
			const std::uint32_t tb = (tint >> 8) & 0xFFu;
			const std::uint32_t ta = tint & 0xFFu;
			for (std::size_t i = 0; i + 3 < src.size(); i += 4)
			{
				baked[i + 0] = static_cast<std::uint8_t>((src[i + 0] * tr + 127u) / 255u);
				baked[i + 1] = static_cast<std::uint8_t>((src[i + 1] * tg + 127u) / 255u);
				baked[i + 2] = static_cast<std::uint8_t>((src[i + 2] * tb + 127u) / 255u);
				baked[i + 3] = static_cast<std::uint8_t>((src[i + 3] * ta + 127u) / 255u);
			}
			e.out.overwritePixels(baked.data());
		}
		e.outIndex = frame.index;
		e.outTint = tint;
		if (m_upload != nullptr) { m_upload(m_uploadCtx, e.out); }
	}

	VideoOpener      m_opener;
	VideoHostConfig  m_cfg;
	VideoUploadHook  m_upload = nullptr;
	void*            m_uploadCtx = nullptr;
	std::uint64_t    m_frameCounter = 0;
	std::unordered_map<std::string, Entry> m_entries;
};

}  // namespace mitiru::video
