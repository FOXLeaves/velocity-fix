#pragma once

namespace features::movement {

	// Owns the only movement coordinate-space conversion in create_move.
	// Movement features write in the unmodified command frame; finish( )
	// converts the base move and every subtick impulse into the final wire
	// frame after anti-aim, aim and bypass code have finished changing yaw.
	class movement_fix
	{
	public:
		void begin( systems::input::usercmd* cmd );
		void finish( systems::input::usercmd* cmd );
		[[nodiscard]] bool claim_quantized_trajectory(
			proto::base_usercmd_pb* base,
			proto::subtick_move_step* const* steps,
			int step_count );

		[[nodiscard]] const math::vector2& source_impulses( ) const { return this->m_source_impulses; }
		[[nodiscard]] float source_yaw( ) const { return this->m_source_yaw; }

	private:
		struct command_frame
		{
			std::intptr_t command_number{ -1 };
			std::uintptr_t pawn{};
			std::uintptr_t movement_buttons{};
			float yaw{};
			bool quantized_payload{};
		};

		static constexpr auto k_command_history_size{ 150 };
		static constexpr auto k_max_quantized_trajectory_steps{ 31 };

		struct quantized_step_claim
		{
			proto::subtick_move_step* step{};
			std::uint32_t presence{};
			float when{};
			float forward_delta{};
			float left_delta{};
			float yaw_delta{};
		};

		math::vector2 m_wire_impulses{};
		math::vector2 m_source_impulses{};
		float m_source_yaw{};

		command_frame m_command_frames[ k_command_history_size ]{};
		command_frame m_legacy_command_frames[ k_command_history_size ]{};
		proto::base_usercmd_pb* m_quantized_trajectory_base{};
		quantized_step_claim m_quantized_trajectory[ k_max_quantized_trajectory_steps ]{};
		int m_quantized_trajectory_count{};
		bool m_quantized_trajectory_claimed{};
		std::uintptr_t m_previous_buttons{};
		bool m_previous_buttons_valid{};
		bool m_active{};
	};

	class bhop
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );

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
