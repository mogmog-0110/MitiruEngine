#pragma once
/// @file StreamingDynamics.hpp
/// @brief Dynamics.hpp の仕上げ処理を、フレーム単位で流せるようにした版
/// @details 一括版と同じ式を同じ順序で計算し、塊の大きさにかかわらずビット単位で同じ値を返す。
/// リミッターの出力は、lookahead フレーム先まで入力するか finish() を呼ぶまで遅れる。

#include <mitiru_mml/Dynamics.hpp>
#include <mitiru_mml/ModernDsp.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace mitiru_mml
{

/// @brief applyLimiter のストリーミング版
class StreamingLimiter
{
public:
	StreamingLimiter(const LimiterSettings& settings, std::uint32_t sampleRate)
		: m_ceiling(static_cast<double>(dbToLinear(settings.ceilingDb)))
		, m_lookahead(static_cast<std::size_t>(std::max(1.0,
			std::round(static_cast<double>(settings.lookaheadMs) * 0.001 * static_cast<double>(sampleRate)))))
		, m_relCoef(std::exp(-2.302585092994046 / (std::max(static_cast<double>(settings.releaseMs), 0.1) * 0.001 * static_cast<double>(sampleRate))))
		, m_window(m_lookahead + 1)
		, m_ring(m_lookahead + 2, 1.0)
	{
	}

	void push(const float* data, std::size_t frames)
	{
		for (std::size_t k = 0; k < frames; ++k)
		{
			const float l = data[k * 2];
			const float r = data[k * 2 + 1];
			const double peak = std::max(std::fabs(static_cast<double>(l)), std::fabs(static_cast<double>(r)));
			const double need = (peak > m_ceiling) ? m_ceiling / peak : 1.0;
			while (!m_dq.empty() && m_dq.back().second >= need)
			{
				m_dq.pop_back();
			}
			m_dq.emplace_back(m_pushed, need);
			m_pending.push_back(l);
			m_pending.push_back(r);
			++m_pushed;
			if (m_pushed > m_lookahead)
			{
				finalizeNext();
			}
		}
	}

	/// @brief 入力を終了し、先読みウィンドウの不足分を切り詰めて出力する
	void finish()
	{
		while (m_finalized < m_pushed)
		{
			finalizeNext();
		}
	}

	[[nodiscard]] std::size_t available() const noexcept
	{
		return (m_out.size() - m_head) / 2;
	}

	[[nodiscard]] const float* peek() const noexcept
	{
		return m_out.data() + m_head;
	}

	void consume(std::size_t frames)
	{
		m_head += frames * 2;
		if (m_head >= 32768 && m_head * 2 >= m_out.size())
		{
			m_out.erase(m_out.begin(), m_out.begin() + static_cast<std::ptrdiff_t>(m_head));
			m_head = 0;
		}
	}

	/// @brief ここまでの最大ゲインリダクション (dB、0 以下)
	[[nodiscard]] float reductionDb() const noexcept
	{
		return linearToDb(static_cast<float>(m_worst));
	}

private:
	void finalizeNext()
	{
		const std::size_t i = m_finalized;
		while (!m_dq.empty() && m_dq.front().first < i)
		{
			m_dq.pop_front();
		}
		const double windowMin = m_dq.empty() ? 1.0 : m_dq.front().second;

		const double recovered = 1.0 - (1.0 - m_previous) * m_relCoef;
		m_previous = std::min(windowMin, recovered);
		const double held = m_previous;
		const std::size_t slots = m_ring.size();
		m_ring[i % slots] = held;
		if (i == 0)
		{
			m_held0 = held;
			for (std::size_t j = 0; j < m_window; ++j)
			{
				m_sum += m_held0;
			}
		}
		else
		{
			const std::size_t leaving = (i > m_window) ? i - m_window : 0;
			const double leavingValue = (leaving == 0) ? m_held0 : m_ring[leaving % slots];
			m_sum += held - leavingValue;
		}
		const double gain = m_sum / static_cast<double>(m_window);
		m_worst = std::min(m_worst, gain);

		m_out.push_back(static_cast<float>(static_cast<double>(m_pending[m_pendingHead]) * gain));
		m_out.push_back(static_cast<float>(static_cast<double>(m_pending[m_pendingHead + 1]) * gain));
		m_pendingHead += 2;
		if (m_pendingHead >= 32768 && m_pendingHead * 2 >= m_pending.size())
		{
			m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(m_pendingHead));
			m_pendingHead = 0;
		}
		++m_finalized;
	}

	double m_ceiling;
	std::size_t m_lookahead;
	double m_relCoef;
	std::size_t m_window;
	std::vector<double> m_ring;  ///< 直近の held (window + 1 個以上)。添字は通し番号の剰余
	std::deque<std::pair<std::size_t, double>> m_dq;  ///< ウィンドウの最小を取るための、単調な (通し番号, need)
	std::vector<float> m_pending;  ///< まだ出していない入力のフレーム
	std::size_t m_pendingHead = 0;
	std::vector<float> m_out;
	std::size_t m_head = 0;
	std::size_t m_pushed = 0;
	std::size_t m_finalized = 0;
	double m_previous = 1.0;
	double m_held0 = 1.0;
	double m_sum = 0.0;
	double m_worst = 1.0;
};

/// @brief buildDuckCurve のストリーミング版
class StreamingDuckCurve
{
public:
	StreamingDuckCurve(std::vector<std::uint64_t> triggers, std::uint32_t sampleRate, const DuckSettings& s)
		: m_triggers(std::move(triggers))
		, m_settings(s)
	{
		const double rate = static_cast<double>(sampleRate);
		m_floorGain = static_cast<double>(dbToLinear(s.depthDb));
		m_attack = static_cast<std::size_t>(std::max(1.0, std::round(static_cast<double>(s.attackMs) * 0.001 * rate)));
		m_hold = static_cast<std::size_t>(std::max(0.0, std::round(static_cast<double>(s.holdMs) * 0.001 * rate)));
		m_tau = std::max(static_cast<double>(s.releaseMs), 0.1) * 0.001 * rate / 2.302585092994046;
	}

	[[nodiscard]] float next()
	{
		const std::size_t i = m_frame++;
		while (m_next < m_triggers.size() && m_triggers[m_next] <= i)
		{
			if (m_triggers[m_next] == i)
			{
				m_stage = Stage::Attack;
				m_attackStep = (m_floorGain - m_gain) / static_cast<double>(m_attack);
				m_holdLeft = m_hold;
			}
			++m_next;
		}
		switch (m_stage)
		{
		case Stage::Attack:
			m_gain += m_attackStep;
			if (m_attackStep >= 0.0 || m_gain <= m_floorGain + 1.0e-9)
			{
				m_gain = m_floorGain;
				m_stage = (m_holdLeft > 0) ? Stage::Hold : Stage::Release;
				m_releaseDb = static_cast<double>(m_settings.depthDb);
			}
			break;
		case Stage::Hold:
			if (m_holdLeft-- <= 1)
			{
				m_stage = Stage::Release;
				m_releaseDb = static_cast<double>(m_settings.depthDb);
			}
			break;
		case Stage::Release:
			m_releaseDb *= std::exp(-1.0 / m_tau);
			m_gain = std::pow(10.0, m_releaseDb / 20.0);
			if (m_releaseDb > -0.01)
			{
				m_gain = 1.0;
				m_stage = Stage::Idle;
			}
			break;
		case Stage::Idle:
		default:
			m_gain = 1.0;
			break;
		}
		return static_cast<float>(m_gain);
	}

private:
	enum class Stage { Idle, Attack, Hold, Release };

	std::vector<std::uint64_t> m_triggers;
	DuckSettings m_settings;
	double m_floorGain = 1.0;
	std::size_t m_attack = 1;
	std::size_t m_hold = 0;
	double m_tau = 1.0;
	Stage m_stage = Stage::Idle;
	double m_gain = 1.0;
	double m_attackStep = 0.0;
	std::size_t m_holdLeft = 0;
	double m_releaseDb = 0.0;
	std::size_t m_next = 0;
	std::size_t m_frame = 0;
};

/// @brief applySidechainCompressor のストリーミング版
class StreamingSidechain
{
public:
	StreamingSidechain(std::uint32_t sampleRate, const SidechainSettings& s)
		: m_settings(s)
	{
		const double rate = static_cast<double>(sampleRate);
		m_aCoef = std::exp(-1.0 / (std::max(static_cast<double>(s.attackMs), 0.01) * 0.001 * rate));
		m_rCoef = std::exp(-1.0 / (std::max(static_cast<double>(s.releaseMs), 0.01) * 0.001 * rate));
		m_slope = 1.0 - 1.0 / static_cast<double>(std::max(s.ratio, 1.0f));
		m_makeup = dbToLinear(s.makeupDb);
		if (s.keyLowPassHz > 0.0f)
		{
			m_lowL.setup(static_cast<double>(s.keyLowPassHz), 0.0f, rate);
			m_lowR.setup(static_cast<double>(s.keyLowPassHz), 0.0f, rate);
		}
	}

	/// @brief 検出用の 1 フレームを受け取り、同じフレームに掛けるゲインを返す
	[[nodiscard]] float next(float keyL, float keyR)
	{
		float kl = keyL;
		float kr = keyR;
		if (m_settings.keyLowPassHz > 0.0f)
		{
			kl = m_lowL.process(kl, modern_dsp::StateVariableFilter::Type::LowPass);
			kr = m_lowR.process(kr, modern_dsp::StateVariableFilter::Type::LowPass);
		}
		const double level = static_cast<double>(std::max(std::fabs(kl), std::fabs(kr)));
		const double levelDb = (level > 1.0e-9) ? 20.0 * std::log10(level) : -180.0;
		const double c = (levelDb > m_envDb) ? m_aCoef : m_rCoef;
		m_envDb = c * m_envDb + (1.0 - c) * levelDb;
		const double want = std::max(0.0, (m_envDb - static_cast<double>(m_settings.thresholdDb)) * m_slope);
		const double g = (want > m_reduction) ? m_aCoef : m_rCoef;
		m_reduction = g * m_reduction + (1.0 - g) * want;
		return static_cast<float>(std::pow(10.0, -m_reduction / 20.0)) * m_makeup;
	}

private:
	SidechainSettings m_settings;
	double m_aCoef = 0.0;
	double m_rCoef = 0.0;
	double m_slope = 0.0;
	float m_makeup = 1.0f;
	modern_dsp::StateVariableFilter m_lowL;
	modern_dsp::StateVariableFilter m_lowR;
	double m_envDb = -180.0;
	double m_reduction = 0.0;
};

} // namespace mitiru_mml
