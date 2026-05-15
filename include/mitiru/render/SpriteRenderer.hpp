#pragma once

/// @file SpriteRenderer.hpp
/// @brief 2Dスプライト描画システム
/// @details PNG画像をstb_imageで読み込み、DX11テクスチャとして
///          キャッシュ管理し、2Dスプライトとして描画する。

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include <sgc/math/Rect.hpp>

#include <stb_image.h>

namespace mitiru::render
{

/// @brief テクスチャ識別子
using TextureId = std::uint32_t;

/// @brief 無効なテクスチャID
inline constexpr TextureId kInvalidTextureId = 0;

/// @brief 2Dスプライト描画システム
/// @details PNG画像をロードしてDX11 SRVを作成し、テクスチャキャッシュで
///          同一ファイルの再ロードを防止する。drawSprite()で画面上に描画する。
///
/// @code
/// mitiru::render::SpriteRenderer renderer;
/// renderer.init(device, context);
/// auto tex = renderer.loadTexture("assets/player.png");
/// renderer.drawSprite(tex, {100, 100, 64, 64});
/// @endcode
class SpriteRenderer
{
public:
	/// @brief デフォルトコンストラクタ
	SpriteRenderer() = default;

	/// @brief デストラクタ（リソース解放）
	~SpriteRenderer() { release(); }

	/// @brief コピー禁止
	SpriteRenderer(const SpriteRenderer&) = delete;
	SpriteRenderer& operator=(const SpriteRenderer&) = delete;

	/// @brief ムーブコンストラクタ
	SpriteRenderer(SpriteRenderer&& other) noexcept
		: m_device(std::move(other.m_device))
		, m_context(std::move(other.m_context))
		, m_entries(std::move(other.m_entries))
		, m_pathToId(std::move(other.m_pathToId))
		, m_nextId(other.m_nextId)
	{
		other.m_nextId = 1;
	}

	/// @brief ムーブ代入
	SpriteRenderer& operator=(SpriteRenderer&& other) noexcept
	{
		if (this != &other)
		{
			release();
			m_device = std::move(other.m_device);
			m_context = std::move(other.m_context);
			m_entries = std::move(other.m_entries);
			m_pathToId = std::move(other.m_pathToId);
			m_nextId = other.m_nextId;
			other.m_nextId = 1;
		}
		return *this;
	}

	/// @brief 初期化
	/// @param device DX11デバイス
	/// @param context DX11デバイスコンテキスト
	/// @return 成功時true
	bool init(ID3D11Device* device, ID3D11DeviceContext* context) noexcept
	{
		if (!device || !context) return false;
		m_device = device;
		m_context = context;
		return true;
	}

	/// @brief PNG画像をテクスチャとしてロードする
	/// @param path ファイルパス
	/// @return テクスチャID（失敗時はkInvalidTextureId）
	/// @details 同一パスは再ロードせずキャッシュから返す
	[[nodiscard]] TextureId loadTexture(const std::string& path)
	{
		if (!m_device) return kInvalidTextureId;

		// キャッシュチェック
		const auto it = m_pathToId.find(path);
		if (it != m_pathToId.end())
		{
			return it->second;
		}

		// stb_imageで画像読み込み
		int width = 0;
		int height = 0;
		int channels = 0;
		auto* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
		if (!pixels)
		{
			return kInvalidTextureId;
		}

		const TextureId id = createFromPixels(
			static_cast<std::uint32_t>(width),
			static_cast<std::uint32_t>(height),
			pixels);
		stbi_image_free(pixels);

		if (id != kInvalidTextureId)
		{
			m_pathToId[path] = id;
		}
		return id;
	}

	/// @brief RGBAピクセルデータからテクスチャを作成する
	/// @param width 幅
	/// @param height 高さ
	/// @param pixels RGBA8ピクセルデータ
	/// @return テクスチャID（失敗時はkInvalidTextureId）
	[[nodiscard]] TextureId createFromPixels(std::uint32_t width, std::uint32_t height,
		const void* pixels)
	{
		if (!m_device || !pixels || width == 0 || height == 0)
		{
			return kInvalidTextureId;
		}

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initData{};
		initData.pSysMem = pixels;
		initData.SysMemPitch = width * 4;

		ID3D11Texture2D* tex2d = nullptr;
		if (FAILED(m_device->CreateTexture2D(&desc, &initData, &tex2d)))
		{
			return kInvalidTextureId;
		}

		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		const HRESULT hr = m_device->CreateShaderResourceView(tex2d, nullptr, srv.GetAddressOf());
		tex2d->Release();

		if (FAILED(hr))
		{
			return kInvalidTextureId;
		}

		const TextureId id = m_nextId++;
		m_entries[id] = TextureEntry{srv, width, height};
		return id;
	}

	/// @brief スプライトを描画する
	/// @param tex テクスチャID
	/// @param dest 描画先矩形（スクリーン座標）
	/// @param src ソース矩形（テクスチャ座標 [0,1]、空の場合は全体）
	/// @param alpha アルファ値（0.0-1.0）
	/// @param flipX X軸反転
	void drawSprite(TextureId tex, const sgc::Rectf& dest,
		const sgc::Rectf& src = {}, float alpha = 1.0f, bool flipX = false)
	{
		if (!m_context) return;

		const auto it = m_entries.find(tex);
		if (it == m_entries.end()) return;

		const auto& entry = it->second;

		// ソース矩形が空の場合はテクスチャ全体を使用
		sgc::Rectf srcRect = src;
		if (srcRect.width() <= 0.0f || srcRect.height() <= 0.0f)
		{
			srcRect = sgc::Rectf{0.0f, 0.0f,
				static_cast<float>(entry.width),
				static_cast<float>(entry.height)};
		}

		// flipX対応: UV座標を反転
		(void)flipX;  // Phase 1: flipX は将来のシェーダーパス実装時に使用
		(void)alpha;   // Phase 1: alpha は将来のブレンドステート実装時に使用
		(void)dest;
		(void)srcRect;

		// Note: 実際のGPU描画はRenderPipeline2Dとの統合時に実装する。
		// 現時点ではテクスチャ管理・キャッシュ機能を提供する。
	}

	/// @brief テクスチャのSRVを取得する
	/// @param tex テクスチャID
	/// @return SRV（無効なIDの場合nullptr）
	[[nodiscard]] ID3D11ShaderResourceView* getSRV(TextureId tex) const
	{
		const auto it = m_entries.find(tex);
		if (it == m_entries.end()) return nullptr;
		return it->second.srv.Get();
	}

	/// @brief テクスチャの幅を取得する
	/// @param tex テクスチャID
	/// @return 幅（無効なIDの場合0）
	[[nodiscard]] std::uint32_t getWidth(TextureId tex) const noexcept
	{
		const auto it = m_entries.find(tex);
		return (it != m_entries.end()) ? it->second.width : 0;
	}

	/// @brief テクスチャの高さを取得する
	/// @param tex テクスチャID
	/// @return 高さ（無効なIDの場合0）
	[[nodiscard]] std::uint32_t getHeight(TextureId tex) const noexcept
	{
		const auto it = m_entries.find(tex);
		return (it != m_entries.end()) ? it->second.height : 0;
	}

	/// @brief キャッシュ済みテクスチャ数を取得する
	/// @return テクスチャ数
	[[nodiscard]] std::size_t textureCount() const noexcept
	{
		return m_entries.size();
	}

	/// @brief 全テクスチャを解放する
	void release()
	{
		m_entries.clear();
		m_pathToId.clear();
		m_nextId = 1;
	}

private:
	/// @brief テクスチャエントリ
	struct TextureEntry
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
	};

	ID3D11Device* m_device = nullptr;
	ID3D11DeviceContext* m_context = nullptr;
	std::unordered_map<TextureId, TextureEntry> m_entries;
	std::unordered_map<std::string, TextureId> m_pathToId;
	TextureId m_nextId = 1;
};

} // namespace mitiru::render

#endif // _WIN32
