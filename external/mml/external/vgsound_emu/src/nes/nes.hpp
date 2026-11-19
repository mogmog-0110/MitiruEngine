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

#ifndef _VGSOUND_EMU_SRC_NES_HPP
#define _VGSOUND_EMU_SRC_NES_HPP

#pragma once

#include "../core/core.hpp"

namespace vgsound_emu
{
	class nes_apu_intf : public vgsound_emu_core
	{
		public:
			nes_apu_intf()
				: vgsound_emu_core("nes_apu_intf")
			{
			}

			virtual void write_irq(u8 out) {}

			// DMC DMA
			virtual void do_dmc_dma() {}

			virtual void stop_dmc_dma() {}
	};

	class nes_apu_core : public vgsound_emu_core
	{
			friend class nes_apu_intf;	// nes specific interface

		private:
			enum frame_type_t : u8
			{
				FRAME_NONE		= 0,
				FRAME_QUARTER	= 1 << 0,
				FRAME_HALF		= 1 << 1,
				FRAME_IRQ		= 1 << 2,
				FRAME_RESET		= 1 << 3,
				FRAME_HALF_IRQ	= FRAME_HALF | FRAME_IRQ,
				FRAME_IRQ_RESET = FRAME_IRQ | FRAME_RESET
			};

			// Cycle tables (Region, Mode, Step)
			static constexpr u32 m_cycle_table[2][2][6] = {
			  {{7457, 14913, 22371, 29828, 29829, 29830},
			   {7457, 14913, 22371, 29829, 37281, 37282}},
			  {{8313, 16627, 24939, 33252, 33253, 33254},
			   {8313, 16627, 24939, 33253, 41565, 41566}}
			 };

			// Frame tables (Mode, Frame)
			static constexpr frame_type_t m_frame_table[2][6] = {
			  {FRAME_QUARTER,
			   FRAME_HALF, FRAME_QUARTER,
			   FRAME_IRQ, FRAME_HALF_IRQ,
			   FRAME_IRQ_RESET																  },
			  {FRAME_QUARTER, FRAME_HALF, FRAME_QUARTER, FRAME_NONE,	 FRAME_HALF, FRAME_RESET}
			 };

			// Length counter table
			static constexpr u16 m_length_table[32] = {
			  10, 254, 20, 2,  40, 4,  80, 6,  160, 8,	60, 10, 14, 12, 26, 14,
			  12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

			static constexpr u8 m_duty_table[4] = {
			  0b10000000,  // 12.5%
			  0b11000000,  // 25%
			  0b11110000,  // 50%
			  0b00111111   // 25% negated
			};

			// Noise frequency table (Region, Freq)
			static constexpr u16 m_noise_freq_table[2][16] = {
			  {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068},
			  {4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708,	944, 1890, 3778}
			};

			// DMC channel frequency table (Region, Freq)
			static constexpr u16 m_dmc_freq_table[2][16] = {
			  {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54},
			  {398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118,  98, 78, 66, 50}
			  };

			// structs
			class length_counter_t : public vgsound_emu_core
			{
				public:
					length_counter_t(nes_apu_core &host)
						: vgsound_emu_core("nes_apu_length_counter")
						, m_host(host)
						, m_prev(0)
						, m_counter(0)
						, m_enable(false)
						, m_halt(false)
						, m_halt_latch(false)
						, m_reload(0)
					{
					}

					virtual void reset() override
					{
						m_prev		 = 0;
						m_counter	 = 0;
						m_enable	 = false;
						m_halt		 = false;
						m_halt_latch = false;
						m_reload	 = 0;
					}

					void tick()
					{
						if (m_counter > 0 && !m_halt)
						{
							m_counter--;
						}
					}

					void set_enable(const bool enable)
					{
						if (!enable)
						{
							m_counter = 0;
						}
						m_enable = enable;
					}

					void reload()
					{
						if (m_reload)
						{
							if (m_counter == m_prev)
							{
								m_counter = m_reload;
							}
							m_reload = 0;
						}
						m_halt = m_halt_latch;
					}

					void load(const u8 value)
					{
						if (m_enable)
						{
							m_reload = m_host.m_length_table[bitfield(value, 0, 5)];
							m_prev	 = m_counter;
						}
					}

					// setters
					void set_halt(const bool halt) { m_halt_latch = halt; }

					// getters
					bool enable() const { return m_enable; }

					bool halt() const { return m_halt; }

					bool status() const { return m_counter > 0; }

				private:
					nes_apu_core &m_host;
					// internal states
					u16 m_prev	  = 0;	// Previous value
					u16 m_counter = 0;	// Counter
					// registers
					bool m_enable	  = false;	// Enable flag
					bool m_halt		  = false;	// Halt flag
					bool m_halt_latch = false;	// Halt flag latch
					u16 m_reload	  = 0;		// Reload value
			};

			class env_t : public vgsound_emu_core
			{
				public:
					env_t(length_counter_t &length_counter)
						: vgsound_emu_core("nes_apu_envelope_unit")
						, m_length_counter(length_counter)
						, m_start(false)
						, m_counter(0)
						, m_cycle(0)
						, m_const_volume(0)
						, m_loop(0)
						, m_volume(0)
					{
					}

					virtual void reset() override
					{
						m_start		   = false;
						m_cycle		   = 0;
						m_counter	   = 0;
						m_const_volume = false;
						m_loop		   = false;
						m_volume	   = 0;
					}

					void tick();

					// setters
					void set_const_volume(const bool const_volume)
					{
						m_const_volume = const_volume;
					}

					void set_loop(const bool loop) { m_loop = loop; }

					void set_volume(const u8 volume) { m_volume = volume; }

					void restart() { m_start = true; }

					// getters
					bool const_volume() const { return m_const_volume; }

					bool loop() const { return m_loop; }

					bool start() const { return m_start; }

					u8 volume() const { return m_volume; }

					u8 counter() const { return m_cycle; }

					u8 output() const { return m_const_volume ? m_volume : m_cycle; }

				private:
					length_counter_t &m_length_counter;
					// internal states
					bool m_start = false;  // Start flag
					u8 m_cycle	 = 0;	   // Clock cycle
					s8 m_counter = 0;	   // Counter value
					// registers
					bool m_const_volume = false;  // Constant volume flag
					bool m_loop			= false;  // Loop flag
					u8 m_volume			= 0;	  // Volume value
			};

			class frame_counter_t : public vgsound_emu_core
			{
				public:
					frame_counter_t(nes_apu_core &host)
						: vgsound_emu_core("nes_apu_frame_counter")
						, m_host(host)
						, m_odd(false)
						, m_irq_pending(false)
						, m_delay_trig(false)
						, m_write_delay(0)
						, m_clock(0)
						, m_step(0)
						, m_mode(false)
						, m_mode_latch(false)
						, m_irq_disable(false)
					{
					}

					virtual void reset() override
					{
						m_odd		  = false;
						m_irq_pending = false;
						m_delay_trig  = false;
						m_write_delay = 0;
						m_clock		  = 0;
						m_step		  = 0;
						m_mode		  = false;
						m_mode_latch  = false;
						m_irq_disable = false;
					}

					void tick()
					{
						m_odd = !m_odd;
						if ((++m_clock) ==
							m_host.m_cycle_table[m_host.m_region][m_mode ? 1 : 0][m_step])
						{
							const nes_apu_core::frame_type_t type =
							  m_host.m_frame_table[m_mode ? 1 : 0][m_step++];
							clock_frame(type);
						}
						if (m_delay_trig)
						{
							if ((--m_write_delay) == 0)
							{
								m_mode	= m_mode_latch;
								m_clock = m_step = 0;
								m_delay_trig	 = false;
							}

							if (m_mode)
							{
								clock_frame(nes_apu_core::frame_type_t::FRAME_HALF);
							}
						}
					}

					void clock_frame(const nes_apu_core::frame_type_t type)
					{
						if ((type & nes_apu_core::frame_type_t::FRAME_QUARTER) ||
							(type & nes_apu_core::frame_type_t::FRAME_HALF))
						{
							m_host.pulse(0).env().tick();
							m_host.pulse(1).env().tick();
							m_host.triangle().linear_counter().tick();
							m_host.noise().env().tick();
							if (type & nes_apu_core::frame_type_t::FRAME_HALF)
							{
								m_host.pulse(0).length_counter().tick();
								m_host.pulse(0).sweep().tick();
								m_host.pulse(1).length_counter().tick();
								m_host.pulse(1).sweep().tick();
								m_host.triangle().length_counter().tick();
								m_host.noise().length_counter().tick();
							}
						}
						if (type & nes_apu_core::frame_type_t::FRAME_IRQ)
						{
							if (!m_irq_disable)
							{
								m_irq_pending = true;
								m_host.irq_update();
							}
						}
						if (type & nes_apu_core::frame_type_t::FRAME_RESET)
						{
							m_clock = m_step = 0;
						}
					}

					// setters
					void set_mode(const bool mode)
					{
						m_delay_trig  = true;
						m_mode_latch  = mode;
						m_write_delay = m_odd ? 4 : 3;
					}

					void set_irq_disable(const bool irq)
					{
						m_irq_disable = irq;
						if (m_irq_disable)
						{
							m_irq_pending = false;
							m_host.irq_update();
						}
					}

					// getters
					bool irq_pending() const { return m_irq_pending; }

				private:
					nes_apu_core &m_host;
					// internal state
					bool m_odd		   = false;	 // Even/Odd cycle
					bool m_irq_pending = false;	 // IRQ pending?
					bool m_delay_trig  = false;	 // Delay trigger
					s8 m_write_delay   = 0;		 // Write delay
					u32 m_clock		   = 0;		 // clock cycle
					u8 m_step		   = 0;		 // current step
					// registers
					bool m_mode		   = false;	 // step mode (0 = 4 step, 1 = 5 step)
					bool m_mode_latch  = false;	 // Step mode latch
					bool m_irq_disable = false;	 // IRQ disable
			};

			// Common ALU for sound channels
			class divider_t : public vgsound_emu_core
			{
				public:
					divider_t()
						: vgsound_emu_core("nes_apu_frequency_divider")
						, m_divider(0)
					{
					}

					virtual void reset() override { m_divider = 0; }

					void write(const bool msb, const u8 data);

					// setters
					void set_divider(const u16 data, const u16 mask = u16(~0))
					{
						m_divider = (m_divider & ~mask) | (data & mask);
					}

					// getters
					u16 divider() const { return m_divider; }

				private:
					u16 m_divider = 0;	// divider (pitch)
			};

			class alu_t : public vgsound_emu_core
			{
				public:
					alu_t(std::string tag, nes_apu_core &host)
						: vgsound_emu_core(tag)
						, m_host(host)
						, m_divider(divider_t())
						, m_length_counter(length_counter_t(host))
						, m_out(0)
						, m_counter(0)
						, m_cycle(0)
					{
					}

					virtual void reset() override
					{
						m_divider.reset();
						m_length_counter.reset();
						m_out	  = 0;
						m_counter = 0;
						m_cycle	  = 0;
					}

					virtual bool tick();

					virtual s8 get_output()
					{
						m_out = 0;
						return 0;
					}

					// accessors
					void clear_cycle() { m_cycle = 0; }

					// getters
					divider_t &divider() { return m_divider; }

					length_counter_t &length_counter() { return m_length_counter; }

					u16 counter() const { return m_counter; }

					u8 cycle() const { return m_cycle; }

					// for previwe/debug only
					s8 out() const { return m_out; }

				protected:
					nes_apu_core &m_host;
					divider_t m_divider;
					length_counter_t m_length_counter;
					s8 m_out	  = 0;	// output per channel
					u16 m_counter = 0;	// clock counter
					u8 m_cycle	  = 0;	// clock cycle
			};

			class pulse_t : public alu_t
			{
				private:
					class sweep_t : public vgsound_emu_core
					{
						public:
							sweep_t(const u8 channel_index, divider_t &divider)
								: vgsound_emu_core("nes_apu_sweep_unit")
								, m_channel_index(channel_index)
								, m_divider(divider)
								, m_reload(false)
								, m_counter(0)
								, m_curr(0)
								, m_target(0)
								, m_enable(false)
								, m_negate(false)
								, m_period(0)
								, m_shift(0)
							{
							}

							virtual void reset() override
							{
								m_reload  = false;
								m_counter = 0;
								m_curr	  = 0;
								m_target  = 0;
								m_enable  = false;
								m_negate  = false;
								m_period  = 0;
								m_shift	  = 0;
							}

							void tick();

							void period_w(const bool msb, const u8 data);

							void update_target()
							{
								const u16 result = (m_curr >> m_shift);
								if (m_negate)
								{
									m_target = m_curr - result;
									if (m_channel_index == 0)  // is first channel?
									{
										m_target--;
									}
								}
								else
								{
									m_target = m_curr + result;
								}
							}

							// setters
							void set_enable(const bool enable) { m_enable = enable; }

							void set_negate(const bool negate) { m_negate = negate; }

							void set_period(const u8 period) { m_period = bitfield(period, 0, 3); }

							void set_shift(const u8 shift) { m_shift = bitfield(shift, 0, 3); }

							void set_divider(const u16 period, const u16 mask = u16(~0))
							{
								m_curr = (m_curr & ~mask) | (period & mask);
								m_divider.set_divider((m_curr * 2) + 1);
								update_target();
							}

							void reload() { m_reload = true; }

							// getters
							u16 curr() const { return m_curr; }

							u16 target() const { return m_target; }

							bool enable() const { return m_enable; }

							bool negate() const { return m_negate; }

							u8 period() const { return m_period; }

							u8 shift() const { return m_shift; }

						private:
							const u8 m_channel_index;  // Channel index
							divider_t &m_divider;	   // Divider class
							// internal states
							bool m_reload = false;	// Reload sweep flag
							u8 m_counter  = 0;		// Sweep counter
							u16 m_curr	  = 0;		// Current period
							u16 m_target  = 0;		// Sweep target period
							// registers
							bool m_enable = false;	// Enable flag
							bool m_negate = false;	// Negate flag
							u8 m_period	  = 0;		// Sweep period
							u8 m_shift	  = 0;		// Sweep shift
					};

				public:
					pulse_t(nes_apu_core &host, const u8 channel_index)
						: alu_t("nes_apu_pulse_channel", host)
						, m_sweep(sweep_t(channel_index, divider()))
						, m_env(env_t(length_counter()))
						, m_duty(0)
					{
					}

					virtual void reset() override
					{
						alu_t::reset();
						m_sweep.reset();
						m_env.reset();
						m_duty = 0;
					}

					virtual bool tick() override;

					virtual s8 get_output()
					{
						m_out = tick() * m_env.output();
						return m_out;
					}

					// setters
					void set_duty(const u8 duty)
					{
						if (m_host.m_swapped_duty)
						{
							m_duty = (bitfield(duty, 0) << 1) | bitfield(duty, 1);
						}
						else
						{
							m_duty = bitfield(duty, 0, 2);
						}
					}

					// getters
					sweep_t &sweep() { return m_sweep; }

					env_t &env() { return m_env; }

					u8 duty() const { return m_duty; }

					bool muted()
					{
						return (m_sweep.curr() < 8) ||
							   (!m_sweep.negate() && m_sweep.target() > 0x7ff);
					}

				private:
					sweep_t m_sweep;  // Sweep unit
					env_t m_env;	  // Envelope unit
					u8 m_duty = 0;	  // Duty select
			};

			class triangle_t : public alu_t
			{
				private:
					class linear_counter_t : public vgsound_emu_core
					{
						public:
							linear_counter_t()
								: vgsound_emu_core("nes_apu_triangle_linear_counter")
								, m_counter(0)
								, m_reload_flag(false)
								, m_reload_value(0)
								, m_control(false)
							{
							}

							virtual void reset() override
							{
								m_counter	   = 0;
								m_reload_flag  = false;
								m_reload_value = 0;
								m_control	   = false;
							}

							void tick();

							// setters
							void set_control(const bool control) { m_control = control; }

							void set_reload_value(const u8 reload_value)
							{
								m_reload_value = bitfield(reload_value, 0, 7);
							}

							void reload() { m_reload_flag = true; }

							// getters
							u8 counter() const { return m_counter; }

						private:
							// internal states
							u8 m_counter	   = 0;
							bool m_reload_flag = false;
							// registers
							u8 m_reload_value = 0;
							bool m_control	  = false;
					};

				public:
					triangle_t(nes_apu_core &host)
						: alu_t("nes_apu_triangle_channel", host)
						, m_linear_counter(linear_counter_t())
						, m_cycle(0)
					{
					}

					virtual void reset() override
					{
						alu_t::reset();
						m_linear_counter.reset();
						m_cycle = 0;
					}

					virtual bool tick() override;

					virtual s8 get_output()
					{
						tick();
						m_out = m_level;
						return m_out;
					}

					// setters
					void set_divider(const u16 divider, const u16 mask = u16(~0))
					{
						m_divider.set_divider(divider, mask);
					}

					// getters
					linear_counter_t &linear_counter() { return m_linear_counter; }

				private:
					linear_counter_t m_linear_counter;
					u8 m_cycle = 0;
					u8 m_level = 0;
			};

			class noise_t : public alu_t
			{
				public:
					noise_t(nes_apu_core &host)
						: alu_t("nes_apu_noise_channel", host)
						, m_env(env_t(length_counter()))
						, m_lfsr(1)
						, m_mode(false)
					{
					}

					virtual void reset() override
					{
						alu_t::reset();
						m_env.reset();
						m_lfsr = 1;
						m_mode = false;
						set_divider(0);
					}

					virtual bool tick() override;

					virtual s8 get_output()
					{
						m_out = tick() * m_env.volume();
						return m_out;
					}

					// setters
					void set_divider(const u8 divider)
					{
						m_divider.set_divider(
						  m_host.m_noise_freq_table[m_host.m_region][bitfield(divider, 0, 4)] - 1);
					}

					void set_mode(bool mode) { m_mode = mode; }

					// getters
					env_t &env() { return m_env; }

					bool mode() const { return m_mode; }

					bool muted() { return false; }

				private:
					env_t m_env;  // Envelope unit
					// internal states
					u32 m_lfsr = 1;	 // LFSR
					// registers
					bool m_mode = false;  // Noise mode
			};

			class dmc_t : public vgsound_emu_core
			{
				public:
					dmc_t(nes_apu_core &host)
						: vgsound_emu_core("nes_apu_dmc_channel")
						, m_host(host)
						, m_irq_pending(false)
						, m_silence(false)
						, m_empty(false)
						, m_transfer_delay(0)
						, m_disable_delay(0)
						, m_curr(0)
						, m_remain(0)
						, m_bits(0)
						, m_counter(0)
						, m_readbuff(0)
						, m_shift(0)
						, m_output(0)
						, m_enable(false)
						, m_irq_en(false)
						, m_loop(false)
						, m_addr(0)
						, m_len(0)
						, m_divider(0)
						, m_output_latch(0)
					{
					}

					virtual void reset() override
					{
						m_irq_pending	 = false;
						m_silence		 = false;
						m_empty			 = false;
						m_transfer_delay = 0;
						m_disable_delay	 = 0;
						m_curr			 = 0;
						m_remain		 = 0;
						m_bits			 = 0;
						m_counter		 = 0;
						m_readbuff		 = 0;
						m_shift			 = 0;
						m_output		 = 0;
						m_enable		 = false;
						m_irq_en		 = false;
						m_loop			 = false;
						m_addr			 = 0xc000;
						m_len			 = 1;
						m_divider		 = 0;
						m_output_latch	 = 0;
						set_divider(0);
						m_host.irq_update();
					}

					void tick();

					void transfer(const u8 data);

					void init_sample()
					{
						m_curr	 = m_addr;
						m_remain = m_len;
					}

					// setters
					void set_irq_en(const bool irq_en)
					{
						m_irq_en = irq_en;
						if (!m_irq_en)
						{
							m_irq_pending = false;
							m_host.irq_update();
						}
					}

					void set_loop(const bool loop) { m_loop = loop; }

					void set_divider(const u8 divider)
					{
						m_divider =
						  m_host.m_dmc_freq_table[m_host.m_region][bitfield(divider, 0, 4)] - 1;
					}

					void set_output(const u8 output) { m_output = output; }

					void set_enable(const bool enable)
					{
						m_enable = true;
						if (m_enable && (m_remain == 0))
						{
							init_sample();
							m_transfer_delay = 2 + bitfield(~m_counter, 0);
						}
						else if (!m_enable)
						{
							if (m_disable_delay == 0)
							{
								m_disable_delay = 2 + bitfield(~m_counter, 0);
							}
						}
					}

					void clear_irq()
					{
						m_irq_pending = false;
						m_host.irq_update();
					}

					void set_addr(const u8 addr) { m_addr = 0xc000 | (u16(addr) << 6); }

					void set_len(const u8 len) { m_len = (u16(len) << 4) | 1; }

					// getters
					bool irq_pending() { return m_irq_pending; }

					bool status() { return m_remain > 0; }

					u8 output() const { return m_output; }

				private:
					nes_apu_core &m_host;
					// internal states
					bool m_irq_pending	= false;  // IRQ pending
					bool m_silence		= false;  // Silence flag
					bool m_empty		= false;  // Buffer empty flag
					s8 m_transfer_delay = 0;	  // Transfer delay
					s8 m_disable_delay	= 0;	  // Disable delay
					u16 m_curr			= 0;	  // Current address
					u16 m_remain		= 0;	  // Remain bytes
					u8 m_bits			= 0;	  // Bit cycle
					u16 m_counter		= 0;	  // Counter
					u8 m_readbuff		= 0;	  // Read Buffer
					u8 m_shift			= 0;	  // Shift reigster
					u8 m_output			= 0;	  // Current output
					// registers
					bool m_enable	  = false;	// Channel enable
					bool m_irq_en	  = false;	// IRQ enable
					bool m_loop		  = false;	// Loop flag
					u16 m_addr		  = 0;		// Sample address
					u16 m_len		  = 0;		// Sample length
					u16 m_divider	  = 0;		// DMC Period divider
					u8 m_output_latch = 0;		// Output latch
			};

		public:
			nes_apu_core(nes_apu_intf &intf,
						 const bool region,
						 const bool no_noise_mode,
						 const bool swapped_duty)
				: vgsound_emu_core("nes_apu_core")
				, m_intf(intf)
				, m_pulse_out_table{0.0f}
				, m_tnd_out_table{0.0f}
				, m_region(region)
				, m_no_noise_mode(no_noise_mode)
				, m_swapped_duty(swapped_duty)
				, m_pulse{pulse_t(*this, 0), pulse_t(*this, 1)}
				, m_triangle(triangle_t(*this))
				, m_noise(noise_t(*this))
				, m_dmc(dmc_t(*this))
				, m_frame_counter(frame_counter_t(*this))
				, m_pulse_out(0)
				, m_tnd_out(0)
				, m_out(0)
			{

				// calculate mixer output
				/*
				pulse channel output:

						 95.88
				-----------------------
					  8128
				----------------- + 100
				pulse 1 + pulse 2

				*/
				for (u8 i = 0; i < 31; i++)
				{
					const f32 out		 = (i == 0) ? 0.0 : 95.88 / ((8128.0 / i) + 100.0);
					m_pulse_out_table[i] = out;
				}

				/*
				triangle, noise, DMC channel output:

							 159.79
				-------------------------------
							1
				------------------------- + 100
				triangle   noise    dmc
				-------- + ----- + -----
				  8227     12241   22638

				*/
				for (u8 t = 0; t < 16; t++)
				{
					for (u8 n = 0; n < 16; n++)
					{
						for (u8 d = 0; d < 128; d++)
						{
							f32 out = (t / 8227.0) + (n / 12241.0) + (d / 22638.0);
							out		= (out == 0.0) ? 0.0 : 159.79 / ((1.0 / out) + 100.0);
							m_tnd_out_table[t][n][d] = out;
						}
					}
				}
			}

			virtual void reset() override;
			void tick();

			// host accesses
			u8 read(const u8 address);
			void write(const u8 address, const u8 data);

			void dmc_transfer(const u8 data) { m_dmc.transfer(data); }

			// configurations
			void set_no_noise_mode(const bool no_noise_mode) { m_no_noise_mode = no_noise_mode; }

			void set_swapped_duty(const bool swapped_duty) { m_swapped_duty = swapped_duty; }

			// getters
			pulse_t &pulse(const u8 ch) { return m_pulse[bitfield(ch, 0)]; }

			triangle_t &triangle() { return m_triangle; }

			noise_t &noise() { return m_noise; }

			dmc_t &dmc() { return m_dmc; }

			frame_counter_t &frame_counter() { return m_frame_counter; }

			// IRQ update
			void irq_update()
			{
				m_intf.write_irq(m_frame_counter.irq_pending() || m_dmc.irq_pending());
			}

			// For preview/Debug only
			s8 pulse_output(const u8 ch) { return m_pulse[bitfield(ch, 0)].out(); }

			s8 triangle_output() { return m_triangle.out(); }

			s8 noise_output() { return m_noise.out(); }

			s8 dmc_output() { return m_dmc.output(); }

			f32 pulse_out() const { return m_pulse_out; }

			f32 tnd_out() const { return m_tnd_out; }

			s32 out() const { return m_out; }

		private:
			nes_apu_intf &m_intf;  // memory interface
			// Nonlinear mixer tables
			f32 m_pulse_out_table[31]		 = {0.0f};
			f32 m_tnd_out_table[16][16][128] = {0.0f};
			const bool m_region;  // console region (0 = NTSC, 1 = PAL)

			// configurations
			bool m_no_noise_mode = false;  // No noise mode flag
			bool m_swapped_duty	 = false;  // Swapped duty cycle

			// internal states
			std::array<pulse_t, 2> m_pulse;
			triangle_t m_triangle;
			noise_t m_noise;
			dmc_t m_dmc;
			frame_counter_t m_frame_counter;
			f32 m_pulse_out = 0.0f;	 // Pulse channel output
			f32 m_tnd_out	= 0.0f;	 // Triangle/Noise/DMC channel output
			s32 m_out		= 0;	 // Overall output
	};
};	// namespace vgsound_emu

#endif	// _VGSOUND_EMU_SRC_NES_HPP