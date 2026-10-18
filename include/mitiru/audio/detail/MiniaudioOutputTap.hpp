#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace mitiru::audio::detail {

class MiniaudioOutputTap {
public:
	explicit MiniaudioOutputTap(std::size_t capacity)
		: m_samples(capacity) {}

	void write(const float* samples, std::size_t n) noexcept {
		const std::size_t written = m_written.load(std::memory_order_relaxed);
		const std::size_t available = m_samples.size() - written;
		const std::size_t count = std::min(n, available);
		if (count == 0) { return; }
		std::copy_n(samples, count, m_samples.data() + written);
		// 配列への書き込みを読み手へ公開してから、読めるサンプル数を進める。
		m_written.store(written + count, std::memory_order_release);
	}

	[[nodiscard]] std::size_t size() const noexcept {
		return m_written.load(std::memory_order_acquire);
	}

	void copy(std::size_t from, float* out, std::size_t n) const noexcept {
		const std::size_t available = size();
		if (from >= available) {
			std::fill_n(out, n, 0.0f);
			return;
		}
		const std::size_t count = std::min(n, available - from);
		std::copy_n(m_samples.data() + from, count, out);
		std::fill_n(out + count, n - count, 0.0f);
	}

	[[nodiscard]] const std::vector<float>& samples() const noexcept {
		return m_samples;
	}

private:
	std::vector<float> m_samples;
	std::atomic<std::size_t> m_written{0};
};

} // namespace mitiru::audio::detail
