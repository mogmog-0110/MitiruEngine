#pragma once

/// @file ShaderCache.hpp
/// @brief シェーダーコンパイルキャッシュ
/// @details ソースコードのハッシュに基づいてコンパイル済みシェーダーをキャッシュし、
///          同一ソースの再コンパイルを回避する。スレッドセーフ。
///
/// @code
/// mitiru::asset::ShaderCache cache;
/// auto handle = cache.getOrCompile(vertexSource, gfx::ShaderType::Vertex);
/// if (handle.valid()) { /* use shader */ }
/// @endcode

#include <mitiru/gfx/IShader.hpp>

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mitiru::asset
{

/// @brief コンパイル済みシェーダーハンドル
struct ShaderHandle
{
	uint64_t id{0};          ///< ユニークID (0 = 無効)
	uint64_t sourceHash{0};  ///< ソースコードのハッシュ値

	/// @brief ハンドルが有効か判定する
	[[nodiscard]] constexpr bool valid() const noexcept { return id != 0; }

	/// @brief 比較演算
	[[nodiscard]] constexpr bool operator==(const ShaderHandle& other) const noexcept
	{
		return id == other.id && sourceHash == other.sourceHash;
	}

	[[nodiscard]] constexpr bool operator!=(const ShaderHandle& other) const noexcept
	{
		return !(*this == other);
	}
};

/// @brief シェーダーコンパイル結果
struct ShaderCompileResult
{
	bool success{false};          ///< コンパイル成功フラグ
	ShaderHandle handle{};        ///< 成功時のハンドル
	std::string errorMessage;     ///< 失敗時のエラーメッセージ
	std::vector<uint8_t> bytecode; ///< コンパイル済みバイトコード
};

/// @brief シェーダーコンパイラ関数型
/// @details source と type を受け取り、コンパイル結果を返す
using ShaderCompilerFn = std::function<ShaderCompileResult(
	std::string_view source, gfx::ShaderType type)>;

/// @brief シェーダーコンパイルキャッシュ（スレッドセーフ）
/// @details ソースハッシュで重複コンパイルを防止し、変更時にキャッシュを無効化する。
class ShaderCache
{
public:
	/// @brief デフォルトコンストラクタ（ダミーコンパイラ使用）
	ShaderCache()
		: m_compiler([this](std::string_view source, gfx::ShaderType type)
		  {
			  return defaultCompile(source, type);
		  })
	{
	}

	/// @brief コンパイラ指定コンストラクタ
	/// @param compiler シェーダーコンパイル関数
	explicit ShaderCache(ShaderCompilerFn compiler)
		: m_compiler(std::move(compiler))
	{
	}

	/// @brief ソースからシェーダーを取得（キャッシュ済みなら再利用）
	/// @param source シェーダーソースコード
	/// @param type シェーダー種別
	/// @return コンパイル結果
	ShaderCompileResult getOrCompile(std::string_view source, gfx::ShaderType type)
	{
		const auto hash = computeHash(source);

		{
			std::scoped_lock lock(m_mutex);

			auto it = m_cache.find(hash);
			if (it != m_cache.end())
			{
				++m_cacheHits;
				return it->second;
			}
		}

		// キャッシュミス — コンパイル実行（ロック外で行い並行性を確保）
		auto result = m_compiler(source, type);

		{
			std::scoped_lock lock(m_mutex);
			++m_cacheMisses;

			if (result.success)
			{
				result.handle.sourceHash = hash;
				if (result.handle.id == 0)
				{
					result.handle.id = ++m_nextId;
				}
				m_cache[hash] = result;
			}
		}

		return result;
	}

	/// @brief 特定のソースハッシュのキャッシュを無効化する
	/// @param sourceHash 無効化するソースのハッシュ
	/// @return 無効化成功ならtrue
	bool invalidate(uint64_t sourceHash)
	{
		std::scoped_lock lock(m_mutex);
		return m_cache.erase(sourceHash) > 0;
	}

	/// @brief ソースコードの文字列から直接無効化する
	/// @param source 無効化するシェーダーソース
	/// @return 無効化成功ならtrue
	bool invalidateBySource(std::string_view source)
	{
		return invalidate(computeHash(source));
	}

	/// @brief 全キャッシュをクリアする
	void clear()
	{
		std::scoped_lock lock(m_mutex);
		m_cache.clear();
		m_cacheHits = 0;
		m_cacheMisses = 0;
	}

	/// @brief キャッシュ済みエントリ数を取得する
	[[nodiscard]] std::size_t size() const noexcept
	{
		std::scoped_lock lock(m_mutex);
		return m_cache.size();
	}

	/// @brief キャッシュヒット数を取得する
	[[nodiscard]] uint64_t cacheHits() const noexcept
	{
		std::scoped_lock lock(m_mutex);
		return m_cacheHits;
	}

	/// @brief キャッシュミス数を取得する
	[[nodiscard]] uint64_t cacheMisses() const noexcept
	{
		std::scoped_lock lock(m_mutex);
		return m_cacheMisses;
	}

	/// @brief ソースハッシュがキャッシュに存在するか確認する
	/// @param sourceHash ハッシュ値
	/// @return キャッシュ済みならtrue
	[[nodiscard]] bool contains(uint64_t sourceHash) const
	{
		std::scoped_lock lock(m_mutex);
		return m_cache.count(sourceHash) > 0;
	}

	/// @brief ソースコードのハッシュ値を計算する（FNV-1a）
	/// @param source ソース文字列
	/// @return 64ビットハッシュ値
	[[nodiscard]] static uint64_t computeHash(std::string_view source) noexcept
	{
		// FNV-1a 64-bit
		uint64_t hash = 14695981039346656037ULL;
		for (const auto c : source)
		{
			hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	/// @brief コンパイラを設定する
	/// @param compiler 新しいコンパイラ関数
	void setCompiler(ShaderCompilerFn compiler)
	{
		std::scoped_lock lock(m_mutex);
		m_compiler = std::move(compiler);
	}

private:
	/// @brief デフォルトのダミーコンパイラ
	ShaderCompileResult defaultCompile(std::string_view source, gfx::ShaderType type)
	{
		ShaderCompileResult result;
		if (source.empty())
		{
			result.success = false;
			result.errorMessage = "empty shader source";
			return result;
		}

		result.success = true;
		result.handle.id = 0; // getOrCompile 内で採番される
		result.handle.sourceHash = computeHash(source);
		result.bytecode.assign(source.begin(), source.end());
		return result;
	}

	mutable std::mutex m_mutex;
	ShaderCompilerFn m_compiler;
	std::unordered_map<uint64_t, ShaderCompileResult> m_cache;
	uint64_t m_nextId{0};
	uint64_t m_cacheHits{0};
	uint64_t m_cacheMisses{0};
};

} // namespace mitiru::asset
