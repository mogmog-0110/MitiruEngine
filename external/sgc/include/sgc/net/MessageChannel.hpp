#pragma once

/// @file MessageChannel.hpp
/// @brief ネットワークメッセージ直列化・ディスパッチ
///
/// ソケット実装を持たず、メッセージのシリアライズ/デシリアライズと
/// タイプベースのディスパッチ機能を提供する。
///
/// @code
/// sgc::MessageBuffer buf;
/// buf.write<uint32_t>(42);
/// buf.write<float>(3.14f);
/// buf.write("hello", 5);
///
/// auto reader = buf.createReader();
/// auto val = reader.read<uint32_t>();  // 42
/// auto fv = reader.read<float>();      // 3.14f
/// @endcode

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "sgc/types/Result.hpp"

namespace sgc
{

/// @brief メッセージヘッダ
///
/// すべてのメッセージの先頭に付与されるヘッダ情報。
struct MessageHeader
{
	std::uint32_t type{0};  ///< メッセージ種別ID
	std::uint32_t size{0};  ///< ペイロードサイズ（バイト）
};

/// @brief メッセージリーダー
///
/// MessageBufferから作成され、データを順次読み出す。
class MessageReader
{
public:
	/// @brief リーダーを構築する
	/// @param data 読み出し元データ
	explicit MessageReader(std::span<const std::byte> data)
		: m_data(data)
	{
	}

	/// @brief トリビアルコピー可能な型を読み出す
	/// @tparam T 読み出す型（trivially_copyable必須）
	/// @return 成功時は読み出した値、失敗時はエラー
	template <typename T>
		requires std::is_trivially_copyable_v<T>
	[[nodiscard]] Result<T> read()
	{
		if (m_offset + sizeof(T) > m_data.size())
		{
			return {ERROR_TAG, Error{"MessageReader: buffer out of range"}};
		}

		T value{};
		std::memcpy(&value, m_data.data() + m_offset, sizeof(T));
		m_offset += sizeof(T);
		return value;
	}

	/// @brief 生バイト列を読み出す
	/// @param dest 書き込み先
	/// @param size 読み出しバイト数
	/// @return 成功時は読み出したバイト数、失敗時はエラー
	[[nodiscard]] Result<std::size_t> read(void* dest, std::size_t size)
	{
		if (m_offset + size > m_data.size())
		{
			return {ERROR_TAG, Error{"MessageReader: buffer out of range"}};
		}

		std::memcpy(dest, m_data.data() + m_offset, size);
		m_offset += size;
		return size;
	}

	/// @brief 残りバイト数を取得する
	/// @return 残りバイト数
	[[nodiscard]] std::size_t remaining() const noexcept
	{
		return m_data.size() - m_offset;
	}

	/// @brief 読み出し位置をリセットする
	void reset() noexcept
	{
		m_offset = 0;
	}

private:
	std::span<const std::byte> m_data;  ///< データ参照
	std::size_t m_offset{0};            ///< 現在の読み出し位置
};

/// @brief メッセージバッファ
///
/// バイト列へのシリアライズ/デシリアライズを行う。
/// trivially_copyable な型の読み書きと生バイト列の操作をサポートする。
class MessageBuffer
{
public:
	/// @brief トリビアルコピー可能な型を書き込む
	/// @tparam T 書き込む型（trivially_copyable必須）
	/// @param value 書き込む値
	template <typename T>
		requires std::is_trivially_copyable_v<T>
	void write(const T& value)
	{
		const auto* bytes = reinterpret_cast<const std::byte*>(&value);
		m_data.insert(m_data.end(), bytes, bytes + sizeof(T));
	}

	/// @brief 生バイト列を書き込む
	/// @param src 読み出し元
	/// @param size 書き込みバイト数
	void write(const void* src, std::size_t size)
	{
		const auto* bytes = static_cast<const std::byte*>(src);
		m_data.insert(m_data.end(), bytes, bytes + size);
	}

	/// @brief バッファ内容をspanとして取得する
	/// @return バッファデータ
	[[nodiscard]] std::span<const std::byte> data() const noexcept
	{
		return {m_data.data(), m_data.size()};
	}

	/// @brief バッファサイズを取得する
	/// @return バイト数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_data.size();
	}

	/// @brief バッファが空か判定する
	/// @return 空ならtrue
	[[nodiscard]] bool empty() const noexcept
	{
		return m_data.empty();
	}

	/// @brief バッファをクリアする
	void clear()
	{
		m_data.clear();
	}

	/// @brief このバッファのリーダーを作成する
	/// @return MessageReader
	[[nodiscard]] MessageReader createReader() const
	{
		return MessageReader{data()};
	}

private:
	std::vector<std::byte> m_data;  ///< バッファ本体
};

/// @brief メッセージハンドラ型
using MessageHandler = std::function<void(const MessageHeader&, MessageReader&)>;

/// @brief メッセージディスパッチャ
///
/// メッセージ種別IDに対応するハンドラを登録し、
/// 受信メッセージを適切なハンドラに振り分ける。
///
/// @code
/// sgc::MessageDispatcher dispatcher;
/// dispatcher.registerHandler(1, [](const sgc::MessageHeader& h, sgc::MessageReader& r) {
///     auto value = r.read<int>();
///     // 処理
/// });
///
/// sgc::MessageHeader header{1, sizeof(int)};
/// sgc::MessageBuffer buf;
/// buf.write<int>(42);
/// dispatcher.dispatch(header, buf);
/// @endcode
class MessageDispatcher
{
public:
	/// @brief メッセージハンドラを登録する
	/// @param messageType メッセージ種別ID
	/// @param handler ハンドラ
	void registerHandler(std::uint32_t messageType, MessageHandler handler)
	{
		m_handlers[messageType] = std::move(handler);
	}

	/// @brief メッセージハンドラを解除する
	/// @param messageType メッセージ種別ID
	void unregisterHandler(std::uint32_t messageType)
	{
		m_handlers.erase(messageType);
	}

	/// @brief メッセージをディスパッチする
	/// @param header メッセージヘッダ
	/// @param buffer メッセージバッファ
	/// @return ハンドラが見つかればtrue
	bool dispatch(const MessageHeader& header, const MessageBuffer& buffer)
	{
		const auto it = m_handlers.find(header.type);
		if (it == m_handlers.end())
		{
			return false;
		}

		auto reader = buffer.createReader();
		it->second(header, reader);
		return true;
	}

	/// @brief 登録済みハンドラ数を取得する
	/// @return ハンドラ数
	[[nodiscard]] std::size_t handlerCount() const noexcept
	{
		return m_handlers.size();
	}

	/// @brief 指定種別のハンドラが登録済みか判定する
	/// @param messageType メッセージ種別ID
	/// @return 登録済みならtrue
	[[nodiscard]] bool hasHandler(std::uint32_t messageType) const
	{
		return m_handlers.contains(messageType);
	}

	/// @brief 全ハンドラを解除する
	void clear()
	{
		m_handlers.clear();
	}

private:
	std::unordered_map<std::uint32_t, MessageHandler> m_handlers;  ///< ハンドラマップ
};

} // namespace sgc
