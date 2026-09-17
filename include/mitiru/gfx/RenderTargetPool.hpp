#pragma once

/// @file RenderTargetPool.hpp
/// @brief レンダーターゲット再利用プール
/// @details 各 pass が中間 RT を自前で new/resize していた（BloomPass/GaussianBlurPass 等）
///          のを、(width,height,format,flags) キーの貸し出しに寄せるための土台。
///          RT の実生成はバックエンド固有 (Dx11RenderTarget/Dx12RenderTarget/...) だが、
///          プール自体はどのバックエンドヘッダにも依存しない。生成関数は
///          `IDevice&` を渡すコンストラクタ（内部で `IDevice::createRenderTarget` を呼ぶ）か、
///          任意の Factory を直接注入するコンストラクタのどちらかで渡せる。

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <algorithm>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>

namespace mitiru::gfx
{

/// @brief プールが払い出す不透明ハンドル。0 は無効値。
/// @details 上位 16bit を世代、下位 16bit をスロット index として詰める。index は 1 から
///          始まる（0 は Invalid 用の予約）ため、世代が 0 でも全体の値が 0 になることはない。
///          release() 後に世代を進めることで、release 済み/別 desc へ再利用されたハンドルを
///          resolve() で判別できる（世代なしだと index の使い回しがそのまま別実体へのエイリアス
///          事故になる）。
enum class RtHandle : std::uint32_t { Invalid = 0 };

/// @brief レンダーターゲットプール
/// @details acquire() は同キーの返却済みエントリがあれば使い回し、無ければ Factory で生成する。
///          プール上限（既定 32）を超える生成要求は RtHandle::Invalid を返し warnOnce する。
class RenderTargetPool
{
public:
	/// @brief RT 実体を生成する関数。バックエンドごとの createRenderTarget 相当を呼び出し側が渡す。
	using Factory = std::function<std::unique_ptr<IRenderTarget>(const RenderTargetDesc&)>;

	/// @brief コンストラクタ
	/// @param factory RT 生成関数
	/// @param maxEntries プール上限（同時に生成できるエントリの総数）
	explicit RenderTargetPool(Factory factory, std::size_t maxEntries = 32)
		: m_factory(std::move(factory))
		, m_maxEntries(std::min<std::size_t>(maxEntries, kIndexMask))  // handle の index は 16bit
	{
	}

	/// @brief IDevice::createRenderTarget を Factory として使うコンストラクタ
	/// @details バックエンド固有ヘッダに依存せず `IDevice&` だけで組み立てられる。
	///          未対応バックエンド（createRenderTarget 未実装）では acquire が常に Invalid を返す。
	/// @param device RT 生成に使う IDevice（プールより長生きすること）
	/// @param maxEntries プール上限
	explicit RenderTargetPool(IDevice& device, std::size_t maxEntries = 32)
		: RenderTargetPool(
			[&device](const RenderTargetDesc& desc) { return device.createRenderTarget(desc); },
			maxEntries)
	{
	}

	RenderTargetPool(const RenderTargetPool&) = delete;
	RenderTargetPool& operator=(const RenderTargetPool&) = delete;

	/// @brief RT を借りる
	/// @return 上限到達 / 生成失敗時は RtHandle::Invalid
	[[nodiscard]] RtHandle acquire(int width, int height, PixelFormat format, std::uint32_t flags = 0)
	{
		const RenderTargetDesc desc{width, height, format, flags};

		auto& freeList = m_freeByDesc[desc];
		if (!freeList.empty())
		{
			const auto id = freeList.back();
			freeList.pop_back();
			auto& entry = m_entries[id];
			entry.inUse = true;
			return packHandle(id, entry.generation);
		}

		if (m_entries.size() >= m_maxEntries)
		{
			debug::warnOnceFix("gfx.render_target_pool.exhausted",
				"RenderTargetPool: 上限に達したため RT を生成できない",
				"同時に in-use な RT の種類数が m_maxEntries を超えた",
				"maxEntries を増やすか、release し忘れている acquire 呼び出しがないか確認する");
			return RtHandle::Invalid;
		}

		auto rt = m_factory(desc);
		if (!rt)
		{
			return RtHandle::Invalid;
		}

		const std::uint32_t id = m_nextId++;
		const auto [it, inserted] = m_entries.emplace(id, Entry{desc, std::move(rt), true, 1});
		return packHandle(id, it->second.generation);
	}

	/// @brief RT を返却する。次の同キー acquire で使い回される。
	/// @details 世代を進めるため、この handle の古いコピーはこれ以降 resolve() で
	///          nullptr になる（release 済み実体への誤アクセスや、再利用先の実体への
	///          エイリアスを防ぐ）。
	void release(RtHandle handle)
	{
		const auto id = handleIndex(handle);
		auto it = m_entries.find(id);
		if (it == m_entries.end() || !it->second.inUse || it->second.generation != handleGeneration(handle))
		{
			return;
		}

		it->second.inUse = false;
		++it->second.generation;
		m_freeByDesc[it->second.desc].push_back(id);
	}

	/// @brief ハンドルから実体を取得する
	/// @return 未知のハンドル / release 済みハンドル / 世代不一致（release 後に
	///         再利用されたハンドル）なら nullptr
	[[nodiscard]] IRenderTarget* resolve(RtHandle handle) const noexcept
	{
		const auto id = handleIndex(handle);
		const auto it = m_entries.find(id);
		if (it == m_entries.end() || !it->second.inUse || it->second.generation != handleGeneration(handle))
		{
			return nullptr;
		}
		return it->second.target.get();
	}

	/// @brief フレーム末処理。release し忘れたエントリを警告 1 回のうえ自動回収する。
	/// @details 回収時に世代も進める。release() 漏れの呼び出し元が古いハンドルを
	///          握ったままでも、回収後の resolve() は nullptr を返す（release() と
	///          同じ「古いハンドルは無効化する」契約に揃える）。
	void endFrame()
	{
		for (auto& [id, entry] : m_entries)
		{
			if (!entry.inUse) { continue; }
			debug::warnOnceFix("gfx.render_target_pool.leaked",
				"RenderTargetPool: release されていない RT を endFrame で自動回収した",
				"acquire した RT に対応する release 呼び出しが抜けている呼び出し元がある",
				"acquire/release を対で呼ぶ (RAII ラッパを使うと漏れを防げる)");
			entry.inUse = false;
			++entry.generation;
			m_freeByDesc[entry.desc].push_back(id);
		}
	}

	/// @brief 生成済みエントリ総数（テスト用）
	[[nodiscard]] std::size_t entryCount() const noexcept { return m_entries.size(); }

	/// @brief 使用中エントリ数（テスト用）
	[[nodiscard]] std::size_t activeCount() const noexcept
	{
		std::size_t n = 0;
		for (const auto& [id, entry] : m_entries) { if (entry.inUse) { ++n; } }
		return n;
	}

private:
	/// @brief プール内 1 エントリ
	struct Entry
	{
		RenderTargetDesc desc;
		std::unique_ptr<IRenderTarget> target;
		bool inUse = false;
		std::uint16_t generation = 1;   ///< release() のたびに進む。0 スタートにしないのは他の意味はなく単なる慣習
	};

	static constexpr std::uint32_t kIndexBits = 16;
	static constexpr std::uint32_t kIndexMask = 0xFFFFu;

	/// @brief index と世代からハンドルを組み立てる。index は 1 以上前提（0 は Invalid）
	[[nodiscard]] static RtHandle packHandle(std::uint32_t id, std::uint16_t generation) noexcept
	{
		return static_cast<RtHandle>((static_cast<std::uint32_t>(generation) << kIndexBits) | (id & kIndexMask));
	}

	[[nodiscard]] static std::uint32_t handleIndex(RtHandle handle) noexcept
	{
		return static_cast<std::uint32_t>(handle) & kIndexMask;
	}

	[[nodiscard]] static std::uint16_t handleGeneration(RtHandle handle) noexcept
	{
		return static_cast<std::uint16_t>(static_cast<std::uint32_t>(handle) >> kIndexBits);
	}

	Factory m_factory;
	std::size_t m_maxEntries;
	std::uint32_t m_nextId = 1;   ///< 0 は Invalid 用に予約。index は 16bit に詰めるため 65535 が上限
	std::unordered_map<std::uint32_t, Entry> m_entries;
	std::unordered_map<RenderTargetDesc, std::vector<std::uint32_t>, RenderTargetDescHash> m_freeByDesc;
};

} // namespace mitiru::gfx
