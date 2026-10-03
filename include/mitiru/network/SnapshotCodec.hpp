#pragma once

/// @file SnapshotCodec.hpp
/// @brief host 権威の回線に流す状態の image (GameMemory + 窓口) を詰める・戻す
///
/// image は参加者が持っている前の image (base) との差分にする。差分は巻き戻しのリングと同じ XOR の
/// zero-run RLE (observe/detail/GameMemoryDelta.hpp) で、動かなかった区間は数 byte になる。base の無い参加者には
/// image をそのまま送る。packet の本体は最後に zstd で詰める (縮まなければ素のまま)。
/// 戻す側は相手から来た byte 列を信じず、長さと範囲を全部確かめる。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <mitiru/network/WireBytes.hpp>
#include <mitiru/observe/detail/GameMemoryDelta.hpp>
#include <mitiru/util/ZstdContext.hpp>

namespace mitiru::network::authority
{

class SnapshotCodec
{
public:
	SnapshotCodec() { m_zstd.open(); }

	/// @brief image を base との差分 (base が空なら image そのもの) にして out の末尾へ足す
	/// @details 書式は [u8 差分か][u32 image の長さ][u32 中身の長さ][中身]。base が image より短い所は 0 と比べる
	void encodeImage(std::span<const std::uint8_t> image, std::span<const std::uint8_t> base, std::vector<std::uint8_t>& out)
	{
		const std::size_t n = image.size();
		std::size_t len = 0;
		if (!base.empty())
		{
			m_base.assign(n, std::uint8_t{0});
			std::memcpy(m_base.data(), base.data(), (std::min)(n, base.size()));
			m_delta.resize(n);
			len = observe::detail::encodeXorRle(m_base.data(), image.data(), n, m_delta.data(), m_delta.size());
		}
		const bool delta = len != 0 && len < n;
		putLe(out, delta ? 1 : 0, 1);
		putLe(out, n, 4);
		putLe(out, delta ? len : n, 4);
		const std::uint8_t* body = delta ? m_delta.data() : image.data();
		out.insert(out.end(), body, body + (delta ? len : n));
	}

	/// @brief encodeImage の書式を読み、image を作る。base は送り手が差分に使った image
	/// @return 壊れている・maxImage を超える・差分なのに base が無い時は false
	bool decodeImage(WireReader& r, std::span<const std::uint8_t> base, std::size_t maxImage, std::vector<std::uint8_t>& image)
	{
		const auto delta = r.get(1);
		const auto n = static_cast<std::size_t>(r.get(4));
		const auto len = static_cast<std::size_t>(r.get(4));
		const std::uint8_t* body = r.take(len);
		if (body == nullptr || delta > 1 || n > maxImage) return false;
		if (delta == 0)
		{
			if (len != n) return false;
			image.assign(body, body + n);
			return true;
		}
		if (base.empty()) return false;
		image.assign(n, std::uint8_t{0});
		std::memcpy(image.data(), base.data(), (std::min)(n, base.size()));
		return observe::detail::decodeXorRleApplyChecked(body, len, image.data(), n);
	}

	/// @brief 本体を zstd で詰めて out に置く。書式は [u8 zstd か][u32 本体の長さ][中身]
	void pack(std::span<const std::uint8_t> body, std::vector<std::uint8_t>& out)
	{
		out.clear();
		m_packed.resize(body.size());
		const std::size_t z = body.empty() ? 0 : m_zstd.compress(body.data(), body.size(), m_packed.data(), m_packed.size());
		putLe(out, z != 0 ? 1 : 0, 1);
		putLe(out, body.size(), 4);
		const std::uint8_t* src = z != 0 ? m_packed.data() : body.data();
		out.insert(out.end(), src, src + (z != 0 ? z : body.size()));
	}

	/// @return 壊れているか maxBody を超える時は false
	bool unpack(std::span<const std::uint8_t> in, std::size_t maxBody, std::vector<std::uint8_t>& body)
	{
		WireReader r(in.data(), in.size());
		const auto zstd = r.get(1);
		const auto n = static_cast<std::size_t>(r.get(4));
		if (!r.ok || zstd > 1 || n > maxBody) return false;
		const std::size_t rest = r.remaining();
		const std::uint8_t* src = r.take(rest);
		if (src == nullptr && rest != 0) return false;
		if (zstd == 0)
		{
			if (rest != n) return false;
			body.assign(src, src + n);
			return true;
		}
		body.resize(n);
		return n > 0 && m_zstd.decompress(src, rest, body.data(), n) == n;
	}

private:
	util::ZstdContext m_zstd;
	std::vector<std::uint8_t> m_base;
	std::vector<std::uint8_t> m_delta;
	std::vector<std::uint8_t> m_packed;
};

} // namespace mitiru::network::authority
