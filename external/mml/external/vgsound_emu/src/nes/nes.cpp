/*
	MODIFIED BY MitiruMML (zlib License, clause 2: altered source versions must be plainly marked).
	This file differs from the original vgsound_emu (gitlab.com/cam900/vgsound_emu, commit c75c046) in two places:
	- noise_t::tick: parentheses added so that the LFSR feedback bit is XORed before the shift by 14.
	- triangle_t: the 4-bit level is kept in m_level; tick() returns bool so the old code reduced it to 0 or 1.
*/
/*
	License: Zlib
	see https://gitlab.com/cam900/vgsound_emu/-/blob/main/LICENSE for more details

	Copyright holder(s): cam900
	NES APU emulation core

	TODO:
	- needs to further verifications from real hardware

	Tech info: https://www.nesdev.org/wiki/APU
*/

#include "nes.hpp"

namespace vgsound_emu
{
	void nes_apu_core::reset()
	{
		for (pulse_t &pulse : m_pulse)
		{
			pulse.reset();
		}
		m_triangle.reset();
		m_noise.reset();
		m_dmc.reset();
		m_frame_counter.reset();
	}

	void nes_apu_core::tick()
	{
		m_out		= 0;
		m_pulse_out = m_tnd_out = 0;
		s32 pulse_out[2] = {0}, tnd_out[3] = {0};
		for (u8 p = 0; p < 2; p++)
		{
			pulse_out[p] += m_pulse[p].get_output();
		}
		tnd_out[0] += m_triangle.get_output();
		tnd_out[1] += m_noise.get_output();
		m_dmc.tick();
		tnd_out[2] += m_dmc.output();

		m_frame_counter.tick();

		m_pulse_out = m_pulse_out_table[pulse_out[0] + pulse_out[1]];
		m_tnd_out	= m_tnd_out_table[tnd_out[0]][tnd_out[1]][tnd_out[2]];

		m_out		= s32((m_pulse_out + m_tnd_out) * 32767.0f);
	}

	u8 nes_apu_core::read(const u8 address)
	{
		u8 ret = 0xff;	// Open bus
		switch (address)
		{
			case 0x15:	// Get channel status
				ret = 0;
				if (m_pulse[0].length_counter().status())
				{
					ret |= 0x01;
				}
				if (m_pulse[1].length_counter().status())
				{
					ret |= 0x02;
				}
				if (m_triangle.length_counter().status())
				{
					ret |= 0x04;
				}
				if (m_noise.length_counter().status())
				{
					ret |= 0x08;
				}
				if (m_dmc.status())
				{
					ret |= 0x10;
				}
				if (m_frame_counter.irq_pending())
				{
					ret |= 0x40;
				}
				if (m_dmc.irq_pending())
				{
					ret |= 0x80;
				}
				break;
		}
		return ret;
	}

	void nes_apu_core::write(const u8 address, const u8 data)
	{
		const u8 pulse_ch = bitfield(address, 2);
		switch (address)
		{
			// Pulse
			case 0x00:
			case 0x04:	// Pulse channel duty/envelope
				m_pulse[pulse_ch].set_duty(bitfield(data, 6, 2));
				m_pulse[pulse_ch].length_counter().set_halt(bitfield(data, 5));
				m_pulse[pulse_ch].env().set_loop(bitfield(data, 5));
				m_pulse[pulse_ch].env().set_const_volume(bitfield(data, 4));
				m_pulse[pulse_ch].env().set_volume(bitfield(data, 0, 4));
				break;
			case 0x01:
			case 0x05:	// Pulse channel sweep
				m_pulse[pulse_ch].sweep().set_enable(bitfield(data, 7));
				m_pulse[pulse_ch].sweep().set_period(bitfield(data, 4, 3));
				m_pulse[pulse_ch].sweep().set_negate(bitfield(data, 3));
				m_pulse[pulse_ch].sweep().set_shift(bitfield(data, 0, 3));
				m_pulse[pulse_ch].sweep().update_target();
				m_pulse[pulse_ch].sweep().reload();
				break;
			case 0x02:	// Pulse channel timer LSB
			case 0x06: m_pulse[pulse_ch].sweep().set_divider(data, 0x00ff); break;
			case 0x03:
			case 0x07:	// Pulse channel timer MSB, length counter
				m_pulse[pulse_ch].length_counter().load(bitfield(data, 3, 5));
				m_pulse[pulse_ch].sweep().set_divider(bitfield<u16>(data, 0, 3) << 8, 0x0700);
				break;
				// Triangle
			case 0x08:	// Triangle channel linear counter
				m_triangle.linear_counter().set_control(bitfield(data, 7));
				m_triangle.length_counter().set_halt(bitfield(data, 7));
				m_triangle.linear_counter().set_reload_value(bitfield(data, 0, 7));
				break;
			case 0x0a:	// Triangle channel timer LSB
				m_triangle.divider().set_divider(data, 0x00ff);
				break;
			case 0x0b:	// Triangle channel timer MSB, length counter
				m_triangle.length_counter().load(bitfield(data, 3, 5));
				m_triangle.divider().set_divider(bitfield<u16>(data, 0, 3) << 8, 0x0700);
				m_triangle.linear_counter().reload();
				break;
				// Noise
			case 0x0c:	// Noise channel envelope
				m_noise.length_counter().set_halt(bitfield(data, 5));
				m_noise.env().set_loop(bitfield(data, 5));
				m_noise.env().set_const_volume(bitfield(data, 4));
				m_noise.env().set_volume(bitfield(data, 0, 4));
				break;
			case 0x0e:	// Noise channel timer/mode
				m_noise.set_divider(bitfield(data, 0, 4));
				m_noise.set_mode(bitfield(data, 7));
				break;
			case 0x0f:	// Noise channel length counter
				m_noise.length_counter().load(bitfield(data, 3, 5));
				m_noise.env().restart();
				break;
			case 0x10:	// DMC irq, loop, timer
				m_dmc.set_irq_en(bitfield(data, 7));
				m_dmc.set_loop(bitfield(data, 6));
				m_dmc.set_divider(bitfield(data, 0, 4));
				break;
			case 0x11:	// DMC output
				m_dmc.set_output(bitfield(data, 0, 7));
				break;
			case 0x12:	// DMC sample address
				m_dmc.set_addr(data);
				break;
			case 0x13:	// DMC sample length
				m_dmc.set_len(data);
				break;
			case 0x15:	// Set enable
				m_pulse[0].length_counter().set_enable(bitfield(data, 0));
				m_pulse[1].length_counter().set_enable(bitfield(data, 1));
				m_triangle.length_counter().set_enable(bitfield(data, 2));
				m_noise.length_counter().set_enable(bitfield(data, 3));
				m_dmc.set_enable(bitfield(data, 4));
				m_dmc.clear_irq();
				break;
			case 0x17:	// Set frame counter
				m_frame_counter.set_mode(bitfield(data, 7));
				m_frame_counter.set_irq_disable(bitfield(data, 6));
				break;
		}
	}

	void nes_apu_core::env_t::tick()
	{
		if (!m_start)
		{
			if ((m_counter--) == 0)
			{
				m_counter = m_volume;
				if (m_cycle > 0)
				{
					m_cycle--;
				}
				else if (m_length_counter.halt())
				{
					m_cycle = 15;
				}
			}
		}
		else
		{
			m_start	  = false;
			m_cycle	  = 15;
			m_counter = m_volume;
		}
	}

	bool nes_apu_core::alu_t::tick()
	{
		// carry handling
		const bool carry = (((m_counter--) & 0xffff) == 0);
		if (carry)
		{
			m_counter = m_divider.divider();
		}

		return carry;
	}

	bool nes_apu_core::pulse_t::tick()
	{
		m_length_counter.reload();
		if (nes_apu_core::alu_t::tick())
		{
			m_cycle = bitfield(m_cycle - 1, 0, 3);
		}

		return muted() ? 0 : bitfield(m_host.m_duty_table[duty()], m_cycle);
	}

	void nes_apu_core::pulse_t::sweep_t::tick()
	{
		const bool carry = (((m_counter--) & 0x7) == 0);
		if (carry)
		{
			if (m_shift && m_enable && (m_curr >= 8) && (m_target <= 0x7ff))
			{
				set_period(m_target);
			}
			m_counter = m_period;
		}
		if (m_reload)
		{
			m_counter = m_period;
			m_reload  = false;
		}
	}

	bool nes_apu_core::triangle_t::tick()
	{
		m_length_counter.reload();
		if (nes_apu_core::alu_t::tick())
		{
			if (m_length_counter.status() && m_linear_counter.counter() > 0)
			{
				m_cycle = bitfield(m_cycle + 1, 0, 5);
			}
		}

		m_level = bitfield(m_cycle, 4) ? bitfield(m_cycle, 0, 4) : bitfield(~m_cycle, 0, 4);
		return m_level != 0;
	}

	void nes_apu_core::triangle_t::linear_counter_t::tick()
	{
		if (m_reload_flag)
		{
			m_counter = m_reload_value;
		}
		else if (m_counter > 0)
		{
			m_counter--;
		}

		if (!m_control)
		{
			m_reload_flag = false;
		}
	}

	bool nes_apu_core::noise_t::tick()
	{
		m_length_counter.reload();
		if (nes_apu_core::alu_t::tick())
		{
			m_lfsr =
			  (m_lfsr >> 1) | ((bitfield(m_lfsr, 0) ^
							   bitfield(m_lfsr, (!m_host.m_no_noise_mode && m_mode) ? 6 : 1)) << 14);
		}

		return bitfield(~m_lfsr, 0);
	}

	void nes_apu_core::dmc_t::tick()
	{
		// carry handling
		const bool carry = (((--m_counter) & 0xffff) == 0);
		if (carry)
		{
			m_counter = m_divider;
			if (!m_silence)
			{
				if (bitfield(m_shift, 0))
				{
					if (m_output <= 125)
					{
						m_output += 2;
					}
				}
				else
				{
					if (m_output >= 2)
					{
						m_output -= 2;
					}
				}
				m_shift >>= 1;
			}
			if ((--m_bits) == 0)
			{
				m_bits = 8;
				if (m_empty)
				{
					m_silence = true;
				}
				else
				{
					m_silence = false;
					m_shift	  = m_readbuff;
					m_empty	  = true;
					m_host.m_intf.do_dmc_dma();
				}
			}
		}

		if (m_disable_delay && ((--m_disable_delay) == 0))
		{
			m_disable_delay = 0;
			m_remain		= 0;
			m_host.m_intf.stop_dmc_dma();
		}

		if (m_transfer_delay && ((--m_transfer_delay) == 0))
		{
			m_host.m_intf.do_dmc_dma();
		}
	}

	void nes_apu_core::dmc_t::transfer(const u8 data)
	{
		if (m_remain > 0)
		{
			m_readbuff = data;
			m_empty	   = false;

			// wraparound
			if ((++m_curr) == 0)
			{
				m_curr = 0x8000;
			}

			if ((--m_remain) == 0)
			{
				if (m_loop)
				{
					init_sample();
				}
				else if (m_irq_en)
				{
					m_irq_pending = true;
					m_host.irq_update();
				}
			}
		}
	}

};	// namespace vgsound_emu