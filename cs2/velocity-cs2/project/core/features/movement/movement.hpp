#pragma once

namespace features::movement {

	class bhop
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );

		// Hysteria-style high-frequency bhop: an independent thread polls
		// the local velocity for the landing signature (vertical speed
		// killed) and pulses the space key at the landing moment. The
		// engine treats the pulse as a real keypress and the server
		// resolves the takeoff on the landing tick at full speed - no
		// subtick/trace dependence, immune to frame drops.
		static void start_thread( );

	private:
		// Consecutive grounded commands: the first one keeps the held
		// jump bit (engine press edge, full-speed takeoff); when the
		// takeoff fails and the player stays grounded, later ticks
		// re-issue a press edge so the chain recovers.
		int m_ground_ticks{};
	};

class airstrafe
{
public:
void on_create_move( systems::input::usercmd* cmd );
void store_angles( );

private:
void check_button( std::uintptr_t current_buttons, std::uintptr_t button );
void rotate_movement( proto::base_usercmd_pb* base, float target_yaw, float view_yaw ) const;
void rotate_to_stop( proto::base_usercmd_pb* base, const math::vector3& velocity ) const;
void unquantized_move( systems::input::usercmd* cmd );

		std::uintptr_t m_last_buttons{};
		std::uintptr_t m_last_pressed{};
		bool m_side_switch{};
		math::vector3 m_angles{};
	};

	class jumpbug
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		[[nodiscard]] bool active_this_tick( ) const { return this->m_active_this_tick; }

	private:
		float m_active_this_tick{ false };
		// Adaptive `when` offset, same calibration scheme as bhop: an
		// attempt that never leaves the ground nudges it later, a takeoff
		// decays it slightly so it converges around the true contact.
		float m_when_bias{};
		bool m_jump_fired_prev{};
		// Same late-takeoff detection as bhop: a takeoff that lost
		// horizontal speed fired after the landing and pulls the bias
		// back earlier, so the calibration converges on the exact
		// contact instead of oscillating on the edge.
		float m_pre_takeoff_speed_2d{};
		// Frame-rate tracker (same scheme as bhop): while create_move
		// commands are being dropped the jump bit stays held so landings
		// whose commands never arrive still auto-takeoff.
		float m_avg_tick_gap{ 1.0f };
		int m_last_tick{};
	};

	class fastladder
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class edgejump
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class edgestop
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class edgebug
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		void on_render( xdraw::draw_list& draw_list );

		[[nodiscard]] bool active_this_tick( ) const { return this->m_active_this_tick; }

	private:
		bool m_active_this_tick{ false };
	};

	class slowwalk
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class mini_jump
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class test_strafer
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		[[nodiscard]] bool is_active( ) const;
		[[nodiscard]] bool handled_this_tick( ) const { return this->m_handled_this_tick; }

	private:
		void quantized_path( systems::input::usercmd* cmd );
		void unquantized_path( systems::input::usercmd* cmd );
		[[nodiscard]] bool apply_yaw_subtick( proto::base_usercmd_pb* base, float when, float yaw_delta ) const;
		void check_button( std::uintptr_t current_buttons, std::uintptr_t button );
		[[nodiscard]] static math::vector2 movement_from_buttons( std::uintptr_t pressed );

		std::uintptr_t m_last_buttons{};
		std::uintptr_t m_last_pressed{};
		int m_substep_counter{};
		bool m_handled_this_tick{};
	};

} // namespace features::movement