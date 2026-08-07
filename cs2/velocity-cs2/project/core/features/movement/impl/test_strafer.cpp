#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::movement {

	namespace {

		constexpr auto k_max_subticks{ 16 };
		constexpr auto k_min_strafe_speed{ 5.0f };

		[[nodiscard]] float get_max_subtick_when( proto::base_usercmd_pb* base )
		{
			auto max_when{ 0.0f };

			for ( auto i = 0; i < base->subtick_moves_size( ); ++i )
			{
				if ( const auto step = base->mutable_subtick_moves( i ) )
				{
					max_when = std::fmaxf( max_when, step->when( ) );
				}
			}

			return max_when;
		}

		[[nodiscard]] float ref_ideal_angle( float speed, float dt, float wishspeed, float air_accel, float air_max_wishspeed )
		{
			if ( speed < 1.0f )
			{
				return 15.0f;
			}

			const auto accel_speed = wishspeed * air_accel * dt;
			float cos_theta{};

			if ( accel_speed >= air_max_wishspeed )
			{
				cos_theta = air_max_wishspeed / ( 2.0f * speed );
			}
			else
			{
				cos_theta = ( air_max_wishspeed - accel_speed ) / speed;
			}

			cos_theta = std::clamp( cos_theta, -1.0f, 1.0f );
			return std::fmaxf( std::acosf( cos_theta ) * ( 180.0f / std::numbers::pi_v<float> ), 1.0f );
		}

		[[nodiscard]] float ref_air_strafer( float vel_x, float vel_y, float target_yaw, float dt, bool side_switch, float wishspeed, float air_accel, float air_max_wishspeed )
		{
			const auto speed = std::sqrtf( vel_x * vel_x + vel_y * vel_y );
			const auto theta = ref_ideal_angle( speed, dt, wishspeed, air_accel, air_max_wishspeed );

			if ( speed < 15.0f )
			{
				return target_yaw;
			}

			const auto vel_angle = std::atan2f( vel_y, vel_x ) * ( 180.0f / std::numbers::pi_v<float> );
			auto vel_delta = target_yaw - vel_angle;
			math::helpers::normalize_angle( vel_delta );

			if ( std::fabsf( vel_delta ) > 2.0f )
			{
				if ( vel_delta > 0.0f )
				{
					auto yaw = vel_angle + theta;
					math::helpers::normalize_angle( yaw );
					return yaw;
				}

				auto yaw = vel_angle - theta;
				math::helpers::normalize_angle( yaw );
				return yaw;
			}

			if ( side_switch )
			{
				auto yaw = vel_angle + theta;
				math::helpers::normalize_angle( yaw );
				return yaw;
			}

			auto yaw = vel_angle - theta;
			math::helpers::normalize_angle( yaw );
			return yaw;
		}

		// Corrected reference for unquantized servers: the ideal angle is
		// derived from the capped wishspeed projection, and friction is
		// carried through so the reference matches the simulation.
		[[nodiscard]] float ref_ideal_angle_unq( float speed, float dt, float friction, float wishspeed, float air_accel, float air_max_wishspeed )
		{
			if ( speed < 1.0f )
			{
				return 90.0f;
			}

			const auto capped_wishspeed = std::fminf( wishspeed, air_max_wishspeed );
			const auto accel_speed = wishspeed * air_accel * friction * dt;

			const auto ideal_projection = std::fmaxf( capped_wishspeed - accel_speed, 0.0f );
			const auto cos_theta = std::clamp( ideal_projection / speed, -1.0f, 1.0f );

			return std::acosf( cos_theta ) * ( 180.0f / std::numbers::pi_v<float> );
		}

		[[nodiscard]] float ref_air_strafer_unq( float vel_x, float vel_y, float target_yaw, float dt, bool side_switch, float wishspeed, float air_accel, float air_max_wishspeed, float friction )
		{
			const auto speed = std::sqrtf( vel_x * vel_x + vel_y * vel_y );
			const auto theta = ref_ideal_angle_unq( speed, dt, friction, wishspeed, air_accel, air_max_wishspeed );

			if ( speed < 15.0f )
			{
				return target_yaw;
			}

			const auto vel_angle = std::atan2f( vel_y, vel_x ) * ( 180.0f / std::numbers::pi_v<float> );
			auto vel_delta = target_yaw - vel_angle;
			math::helpers::normalize_angle( vel_delta );

			float result{};

			if ( std::fabsf( vel_delta ) > 2.0f )
			{
				result = vel_angle + ( vel_delta > 0.0f ? theta : -theta );
			}
			else
			{
				result = vel_angle + ( side_switch ? theta : -theta );
			}

			math::helpers::normalize_angle( result );
			return result;
		}

		void ref_air_accel_sim( float& vel_x, float& vel_y, float wishdir_yaw, float frame_time, float friction, float wishspeed, float air_accel, float air_max_wishspeed )
		{
			const auto yaw_rad = wishdir_yaw * ( std::numbers::pi_v<float> / 180.0f );
			const auto wish_dir_x = std::cosf( yaw_rad );
			const auto wish_dir_y = std::sinf( yaw_rad );

			const auto capped = std::fminf( wishspeed, air_max_wishspeed );
			const auto dot = vel_x * wish_dir_x + vel_y * wish_dir_y;
			const auto add_speed = capped - dot;

			if ( add_speed <= 0.0f )
			{
				return;
			}

			const auto accel_speed = wishspeed * air_accel * friction * frame_time;
			const auto step = std::fminf( accel_speed, add_speed );

			vel_x += wish_dir_x * step;
			vel_y += wish_dir_y * step;
		}

	} // namespace

	[[nodiscard]] bool test_strafer::is_active( ) const
	{
		// Works with both quantization modes: subtick yaw deltas when the
		// server quantizes movement input, direct view angles otherwise.
		return settings::g_movement.m_test_strafer.enabled.value;
	}

	math::vector2 test_strafer::movement_from_buttons( std::uintptr_t pressed )
	{
		auto forward_move{ 0.0f };
		auto left_move{ 0.0f };

		if ( pressed & cstypes::command_buttons::in_forward )
		{
			forward_move = 1.0f;
		}
		else if ( pressed & cstypes::command_buttons::in_back )
		{
			forward_move = -1.0f;
		}

		if ( pressed & cstypes::command_buttons::in_moveleft )
		{
			left_move = -1.0f;
		}
		else if ( pressed & cstypes::command_buttons::in_moveright )
		{
			left_move = 1.0f;
		}

		return { forward_move, left_move };
	}

	void test_strafer::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_handled_this_tick = false;

		if ( !this->is_active( ) )
		{
			return;
		}

		if ( features::movement::g_jumpbug.active_this_tick( ) )
		{
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		const auto move_type = memory::read<std::uint8_t>( local.pawn + SCHEMA( "CBaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			return;
		}

		const auto& prestate = systems::g_prediction.pre( );
		if ( prestate.flags & cstypes::entity_flags::on_ground )
		{
			return;
		}

		if ( features::combat::g_rage.is_firing_this_tick( ) )
		{
			return;
		}

		// TEMP TEST (gpt5.6): always run the subtick yaw path regardless of
		// sv_quantize_movement_input to verify whether quantize 0 actually
		// ignores subtick yaw deltas at all.
		this->quantized_path( cmd );
	}

	// Unquantized servers (most community servers, sv_quantize_movement_input 0)
	// ignore subtick yaw deltas. Steer by rotating the desired world direction
	// into the command frame and writing the base move fields - the view (and
	// anti-aim yaw) stays untouched.
	void test_strafer::unquantized_path( systems::input::usercmd* cmd )
	{
		const auto current_buttons = cmd->buttons.value;
		if ( current_buttons & static_cast< std::uintptr_t >( cstypes::command_buttons::in_sprint ) )
		{
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base || !base->viewangles( ) )
		{
			return;
		}

		this->check_button( current_buttons, cstypes::command_buttons::in_moveleft );
		this->check_button( current_buttons, cstypes::command_buttons::in_moveright );
		this->check_button( current_buttons, cstypes::command_buttons::in_forward );
		this->check_button( current_buttons, cstypes::command_buttons::in_back );
		this->m_last_buttons = current_buttons;

		const auto player_move = movement_from_buttons( this->m_last_pressed );
		if ( player_move.x == 0.0f && player_move.y == 0.0f )
		{
			return;
		}

		const auto& prestate = systems::g_prediction.pre( );
		const auto velocity = prestate.networked_velocity;
		const auto speed_2d = velocity.length_2d( );

		if ( speed_2d < k_min_strafe_speed )
		{
			return;
		}

		const auto sv_airaccelerate = CONVAR ("sv_airaccelerate")->get<float>( );
		const auto sv_maxspeed = CONVAR ("sv_maxspeed")->get<float>( );
		const auto sv_air_max_wishspeed = CONVAR ("sv_air_max_wishspeed")->get<float>( );
		auto surface_friction = prestate.surface_friction;
		if ( !std::isfinite( surface_friction ) || surface_friction <= 0.0f )
		{
			surface_friction = 1.0f;
		}

		// Interpret WASD against the real view yaw.
		const auto input_yaw = systems::g_input.get_view_angles( ).y;
		const auto input_offset = std::atan2f( -player_move.y, player_move.x ) * ( 180.0f / std::numbers::pi_v<float> );

		auto target_world_yaw = input_yaw + input_offset;
		math::helpers::normalize_angle( target_world_yaw );

		const auto entry_side = ( this->m_substep_counter % 2 ) == 0;
		const auto wishdir_yaw = ref_air_strafer_unq( velocity.x, velocity.y, target_world_yaw, cstypes::tick_interval, entry_side, sv_maxspeed, sv_airaccelerate, sv_air_max_wishspeed, surface_friction );

		// Use the final command yaw (keeps whatever anti-aim wrote).
		const auto command_yaw = base->viewangles( )->y( );

		auto delta = wishdir_yaw - command_yaw;
		math::helpers::normalize_angle( delta );

		// Unquantized servers clamp the move fields to [-1, 1]; keep the
		// input fractional or the direction collapses toward 45 degrees.
		const auto rad = delta * ( std::numbers::pi_v<float> / 180.0f );
		auto forward_move = std::cosf( rad );
		auto left_move = -std::sinf( rad );

		const auto move_length = std::hypotf( forward_move, left_move );
		if ( move_length > 1.0f )
		{
			forward_move /= move_length;
			left_move /= move_length;
		}

		base->set_forwardmove( std::clamp( forward_move, -1.0f, 1.0f ) );
		base->set_leftmove( std::clamp( left_move, -1.0f, 1.0f ) );

		this->m_handled_this_tick = true;
		++this->m_substep_counter;
	}

	bool test_strafer::apply_yaw_subtick( proto::base_usercmd_pb* base, float when, float yaw_delta ) const
	{
		math::helpers::normalize_angle( yaw_delta );

		// Only reject a truly zero spin; the side-step lock injects 0.005
		// deg per step to keep the strafer active.
		if ( std::fabsf( yaw_delta ) <= 0.001f )
		{
			return false;
		}

		const auto subtick_moves = base->mutable_subtick_moves( );
		if ( !subtick_moves )
		{
			return false;
		}

		const auto step = systems::g_input.acquire_subtick_step( subtick_moves );
		if ( !step )
		{
			return false;
		}

		step->set_when( when );
		step->set_button( 0 );
		step->set_pressed( false );
		step->set_analog_forward_delta( 0.0f );
		step->set_analog_left_delta( 0.0f );
		step->set_yaw_delta( yaw_delta );
		step->set_pitch_delta( 0.0f );
		return true;
	}

	void test_strafer::quantized_path( systems::input::usercmd* cmd )
	{
		const auto current_buttons = cmd->buttons.value;
		if ( current_buttons & static_cast< std::uintptr_t >( cstypes::command_buttons::in_sprint ) )
		{
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		this->check_button( current_buttons, cstypes::command_buttons::in_moveleft );
		this->check_button( current_buttons, cstypes::command_buttons::in_moveright );
		this->check_button( current_buttons, cstypes::command_buttons::in_forward );
		this->check_button( current_buttons, cstypes::command_buttons::in_back );
		this->m_last_buttons = current_buttons;

		const auto& prestate = systems::g_prediction.pre( );
		const auto velocity = prestate.networked_velocity;
		const auto speed_2d = velocity.length_2d( );
		const auto command_yaw = systems::g_input.get_view_angles( ).y;

		const auto player_move = movement_from_buttons( this->m_last_pressed );
		if ( player_move.x == 0.0f && player_move.y == 0.0f )
		{
			return;
		}

		if ( speed_2d < k_min_strafe_speed )
		{
			return;
		}

		const auto start_when = get_max_subtick_when( base );
		if ( start_when >= 0.99f )
		{
			return;
		}

		const auto sv_airaccelerate = CONVAR ("sv_airaccelerate")->get<float>( );
		const auto sv_maxspeed = CONVAR ("sv_maxspeed")->get<float>( );
		const auto sv_air_max_wishspeed = CONVAR ("sv_air_max_wishspeed")->get<float>( );
		const auto surface_friction = prestate.surface_friction;

		const auto base_yaw_offset = std::atan2f( -player_move.y, player_move.x ) * ( 180.0f / std::numbers::pi_v<float> );
		auto target_yaw = command_yaw + base_yaw_offset;
		math::helpers::normalize_angle( target_yaw );

		// While a side-step is active the engine view must stay locked at
		// the AA yaw: the AA-rotated input decomposes along it back to the
		// user's world direction the whole way, whereas unwinding the view
		// toward the strafe target only aligns part of the tick and the
		// accel stalls. Back mode keeps the normal steering (works fine).
		auto side_offset{ 0.0f };
		if ( features::combat::g_misc.antiaim( ).antiaim_active( ) )
		{
			const auto aa_yaw = features::combat::g_misc.antiaim( ).get_modified_angles( ).y;
			side_offset = aa_yaw - command_yaw;
			math::helpers::normalize_angle( side_offset );
			if ( side_offset > 90.0f )
			{
				side_offset -= 180.0f;
			}
			else if ( side_offset < -90.0f )
			{
				side_offset += 180.0f;
			}
		}
		const auto side_locked = std::fabsf( side_offset ) > 30.0f;

		const auto sub_frame = cstypes::tick_interval / static_cast< float >( k_max_subticks );
		const auto when_step = ( 1.0f - start_when ) / static_cast< float >( k_max_subticks + 1 );

		auto acc_yaw = command_yaw;
		auto sim_vx = velocity.x;
		auto sim_vy = velocity.y;
		auto injected = 0;

		for ( auto i = 1; i <= k_max_subticks; ++i )
		{
			const auto entry_side = ( ( this->m_substep_counter + i ) % 2 ) == 0;
			const auto wishdir_yaw = ref_air_strafer( sim_vx, sim_vy, target_yaw, sub_frame, entry_side, sv_maxspeed, sv_airaccelerate, sv_air_max_wishspeed );

			auto target_view_yaw = wishdir_yaw - base_yaw_offset;
			math::helpers::normalize_angle( target_view_yaw );

			// Side-step: keep the view on the AA yaw (0.005 deg/step keeps
			// the subtick step registering so the strafer stays active).
			auto yaw_delta{ 0.005f };
			if ( !side_locked )
			{
				yaw_delta = target_view_yaw - acc_yaw;
				math::helpers::normalize_angle( yaw_delta );
			}
			acc_yaw += yaw_delta;
			math::helpers::normalize_angle( acc_yaw );

			const auto when_frac = start_when + static_cast< float >( i ) * when_step;

			if ( !this->apply_yaw_subtick( base, when_frac, yaw_delta ) )
			{
				break;
			}

			ref_air_accel_sim( sim_vx, sim_vy, wishdir_yaw, sub_frame, surface_friction, sv_maxspeed, sv_airaccelerate, sv_air_max_wishspeed );
			++injected;
		}

		if ( injected > 0 )
		{
			this->m_handled_this_tick = true;
			++this->m_substep_counter;
		}
	}

	void test_strafer::check_button( std::uintptr_t current_buttons, std::uintptr_t button )
	{
		constexpr auto moveleft = static_cast< std::uintptr_t >( cstypes::command_buttons::in_moveleft );
		constexpr auto moveright = static_cast< std::uintptr_t >( cstypes::command_buttons::in_moveright );
		constexpr auto forward = static_cast< std::uintptr_t >( cstypes::command_buttons::in_forward );
		constexpr auto back = static_cast< std::uintptr_t >( cstypes::command_buttons::in_back );

		if ( current_buttons & button && ( !( this->m_last_buttons & button ) || ( button & moveleft && !( this->m_last_pressed & moveright ) ) || ( button & moveright && !( this->m_last_pressed & moveleft ) ) || ( button & forward && !( this->m_last_pressed & back ) ) || ( button & back && !( this->m_last_pressed & forward ) ) ) )
		{
			if ( button & moveleft )
			{
				this->m_last_pressed &= ~moveright;
			}
			else if ( button & moveright )
			{
				this->m_last_pressed &= ~moveleft;
			}
			else if ( button & forward )
			{
				this->m_last_pressed &= ~back;
			}
			else if ( button & back )
			{
				this->m_last_pressed &= ~forward;
			}

			this->m_last_pressed |= button;
		}
		else if ( !( current_buttons & button ) )
		{
			this->m_last_pressed &= ~button;
		}
	}

} // namespace features::movement
