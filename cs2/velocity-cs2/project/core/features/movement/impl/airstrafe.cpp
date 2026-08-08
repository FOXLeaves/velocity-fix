#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::movement {

	void airstrafe::on_create_move( systems::input::usercmd* cmd )
	{
		if ( features::movement::g_jumpbug.active_this_tick( ) )
		{
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		// The discrete/yaw path needs both movement quantization and subtick
		// movement view angles. Without either capability, use the analog path.
		const auto quantize_cvar = CONVAR( "sv_quantize_movement_input" );
		const auto subtick_view_cvar = CONVAR( "sv_subtick_movement_view_angles" );
		if ( !quantize_cvar || !quantize_cvar->get<bool>( )
			|| !subtick_view_cvar || !subtick_view_cvar->get<bool>( ) )
		{
			this->unquantized_move( cmd );
			return;
		}

		const auto current_buttons = cmd->buttons.value;
		const bool shift_held = current_buttons & static_cast< std::uintptr_t >( cstypes::command_buttons::in_sprint );

		const auto& prestate = systems::g_prediction.pre( );
		const bool in_air = !( prestate.flags & cstypes::entity_flags::on_ground );


		const auto wants_stop = shift_held
			|| features::combat::g_rage.should_stop( )
			|| features::misc::g_projectile_trajectory.should_stop( );

		if ( ( !settings::g_movement.airstrafe.value && !wants_stop )
			|| features::combat::g_rage.is_firing_this_tick( ) )
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

		if ( settings::g_movement.m_test_strafer.enabled.value && features::movement::g_test_strafer.handled_this_tick( ) )
		{
			return;
		}

		if ( prestate.flags & cstypes::entity_flags::on_ground )
		{
			return;
		}

		const auto subtick_moves = base->mutable_subtick_moves( );
		if ( !subtick_moves )
		{
			return;
		}

		if ( !wants_stop )
		{
			// The global movement transaction runs after the airstrafer, so
			// buttons are still the player's unmodified input here.
			this->check_button( current_buttons, cstypes::command_buttons::in_moveleft );
			this->check_button( current_buttons, cstypes::command_buttons::in_moveright );
			this->check_button( current_buttons, cstypes::command_buttons::in_forward );
			this->check_button( current_buttons, cstypes::command_buttons::in_back );
			this->m_last_buttons = current_buttons;
		}

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return;
		}

		const auto sv_airaccelerate = CONVAR ("sv_airaccelerate")->get<float>( );
		const auto sv_air_max_wishspeed = CONVAR ("sv_air_max_wishspeed")->get<float>( );
		const auto sv_gravity = CONVAR ("sv_gravity")->get<float>( );
		const auto sv_staminarecoveryrate = CONVAR ("sv_staminarecoveryrate")->get<float>( );

		// Steering is derived from the REAL view yaw (the airstrafe frame
		// that keeps bunnyhop accel self-consistent); anti-aim only rewrites
		// the command view angles, and the movement fix rotates the user's
		// input into that modified frame separately.
		const auto view_angles = this->m_angles;
		const auto cmd_move_backup = math::vector3{ base->forwardmove( ), base->leftmove( ), 0.0f };
		const auto effective_maxspeed = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) );

		constexpr auto subtick_count{ 32 };
		constexpr auto frame_time{ cstypes::tick_interval / static_cast< float >( subtick_count ) };

		auto yaw_offset{ 0.0f };

		if ( !wants_stop )
		{
			if ( this->m_last_pressed & cstypes::command_buttons::in_moveleft )
			{
				yaw_offset += 90.0f;
			}

			if ( this->m_last_pressed & cstypes::command_buttons::in_moveright )
			{
				yaw_offset -= 90.0f;
			}

			if ( this->m_last_pressed & cstypes::command_buttons::in_forward )
			{
				yaw_offset *= 0.5f;
			}
			else if ( this->m_last_pressed & cstypes::command_buttons::in_back )
			{
				yaw_offset = -yaw_offset * 0.5f + 180.0f;
			}
		}

		const auto has_direction_input = ( this->m_last_pressed & cstypes::command_buttons::in_moveleft ) || ( this->m_last_pressed & cstypes::command_buttons::in_moveright ) || ( this->m_last_pressed & cstypes::command_buttons::in_forward ) || ( this->m_last_pressed & cstypes::command_buttons::in_back );
		const auto effective_wants_stop = wants_stop || ( settings::g_movement.airstrafe_fully_directional.value && !has_direction_input );

		auto velocity = prestate.networked_velocity;
		const auto& initial_impulses = features::movement::g_movement_fix.source_impulses( );
		auto last_impulses = math::vector3{ initial_impulses.x, initial_impulses.y, prestate.last_movement_impulses.z };
		auto stamina = prestate.stamina;
		const auto surface_friction = prestate.surface_friction;

		if ( effective_wants_stop && velocity.length_2d( ) <= 10.0f )
		{
			this->rotate_to_stop( base, velocity );
			return;
		}

		if ( last_impulses.y < 0.0f )
		{
			this->m_side_switch = false;
		}
		else if ( last_impulses.y > 0.0f )
		{
			this->m_side_switch = true;
		}

		for ( auto i = 0; i < subtick_count; ++i )
		{
			base->set_forwardmove( cmd_move_backup.x );
			base->set_leftmove( cmd_move_backup.y );

			const auto speed_2d = std::sqrtf( velocity.x * velocity.x + velocity.y * velocity.y );

			if ( speed_2d >= 10.0f )
			{
				base->set_forwardmove( 0.0f );
				base->set_leftmove( 0.0f );
				if ( effective_wants_stop )
				{
					this->rotate_to_stop( base, velocity );
				}
				else
				{
					const auto velocity_angle = std::atan2f( velocity.y, velocity.x ) * ( 180.0f / std::numbers::pi_v<float> );
					const auto accel_speed = sv_airaccelerate * effective_maxspeed * frame_time * surface_friction;
					const auto half_accel = accel_speed * 0.5f;
					const auto optimal_floor = std::fmaxf( half_accel, sv_air_max_wishspeed - half_accel );
					const auto ideal_angle = std::clamp( std::atanf( optimal_floor / speed_2d ) * ( 180.0f / std::numbers::pi_v<float> ), 0.0f, 45.0f );

					auto target_yaw = view_angles.y + yaw_offset;
					math::helpers::normalize_angle( target_yaw );

					auto velocity_delta = target_yaw - velocity_angle;
					math::helpers::normalize_angle( velocity_delta );

					if ( ( std::fabsf( velocity_delta ) > 170.0f && speed_2d > 80.0f ) || ( velocity_delta > ideal_angle && speed_2d > 80.0f ) )
					{
						target_yaw = velocity_angle + ideal_angle;
						base->set_leftmove( -1.0f );
					}
					else if ( -ideal_angle <= velocity_delta || speed_2d <= 80.0f )
					{
						if ( this->m_side_switch )
						{
							target_yaw = target_yaw - ideal_angle;
							base->set_leftmove( -1.0f );
						}
						else
						{
							target_yaw = target_yaw + ideal_angle;
							base->set_leftmove( 1.0f );
						}
					}
					else
					{
						target_yaw = velocity_angle - ideal_angle;
						base->set_leftmove( 1.0f );
					}

					math::helpers::normalize_angle( target_yaw );

					this->rotate_movement( base, target_yaw, this->m_angles.y );
				}
			}

			const auto step = systems::g_input.acquire_subtick_step( subtick_moves );
			if ( !step )
			{
				break;
			}

			step->set_button( 0 );
			step->set_pressed( false );
			step->set_when( static_cast< float >( i ) / static_cast< float >( subtick_count ) );
			step->set_analog_forward_delta( base->forwardmove( ) - last_impulses.x );
			step->set_analog_left_delta( base->leftmove( ) - last_impulses.y );

			last_impulses.x = base->forwardmove( );
			last_impulses.y = base->leftmove( );

			// A subtick movement event applies at the start of its interval.
			// Simulate that interval with the just-emitted absolute impulse so
			// the planner and the server do not differ by one full substep.
			auto simulated_speed = speed_2d;
			if ( stamina > 0.0f )
			{
				const auto speed_scale = std::clamp( 1.0f - ( stamina / 100.0f ), 0.0f, 1.0f );
				simulated_speed *= speed_scale * speed_scale;
				stamina = std::fmaxf( stamina - ( frame_time * sv_staminarecoveryrate ), 0.0f );
			}

			velocity.z -= ( sv_gravity * frame_time );

			if ( simulated_speed > 0.0001f )
			{
				math::vector3 forward_dir{}, left_dir{};
				math::helpers::angle_vectors_2d( view_angles.y, forward_dir, left_dir );

				math::vector3 wish_dir
				{
					( forward_dir.x * last_impulses.x * effective_maxspeed ) + ( left_dir.x * last_impulses.y * effective_maxspeed ),
					( forward_dir.y * last_impulses.x * effective_maxspeed ) + ( left_dir.y * last_impulses.y * effective_maxspeed ),
					0.0f
				};

				auto wish_speed = std::sqrtf( wish_dir.x * wish_dir.x + wish_dir.y * wish_dir.y );
				if ( wish_speed > 0.0001f )
				{
					wish_dir.x /= wish_speed;
					wish_dir.y /= wish_speed;
				}

				wish_speed = std::fminf( wish_speed, effective_maxspeed );
				const auto capped_wish = std::fminf( wish_speed, sv_air_max_wishspeed );
				const auto current_speed = velocity.x * wish_dir.x + velocity.y * wish_dir.y;
				const auto add_speed = capped_wish - current_speed;

				if ( add_speed > 0.0f )
				{
					const auto accel_speed = sv_airaccelerate * effective_maxspeed * frame_time * surface_friction;
					const auto gain = std::fminf( accel_speed * 0.5f, add_speed );
					velocity.x += wish_dir.x * gain;
					velocity.y += wish_dir.y * gain;
				}
			}

			velocity.z += ( sv_gravity * frame_time ) * 0.5f;

			if ( !effective_wants_stop )
			{
				this->m_side_switch = !this->m_side_switch;
			}
		}
	}

	void airstrafe::unquantized_move( systems::input::usercmd* cmd )
	{
		if ( features::movement::g_jumpbug.active_this_tick( ) )
		{
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		const auto current_buttons = cmd->buttons.value;
		const bool shift_held = current_buttons & static_cast< std::uintptr_t >( cstypes::command_buttons::in_sprint );

		const auto& prestate = systems::g_prediction.pre( );
		const bool in_air = !( prestate.flags & cstypes::entity_flags::on_ground );
		if ( !in_air )
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

		const auto& velocity = prestate.networked_velocity;

		// Shift air-stop: push against the velocity (base fields only).
		if ( shift_held )
		{
			float forward_move = 0.0f;
			float left_move = 0.0f;

			if ( velocity.length_2d( ) > 10.0f )
			{
				const auto vel_yaw = std::atan2f( velocity.y, velocity.x ) * ( 180.0f / std::numbers::pi_v<float> );
				const auto relative_yaw = ( vel_yaw - this->m_angles.y ) * ( std::numbers::pi_v<float> / 180.0f );

				forward_move = std::clamp( -std::cosf( relative_yaw ), -1.0f, 1.0f );
				left_move = std::clamp( -std::sinf( relative_yaw ), -1.0f, 1.0f );
			}

			base->set_forwardmove( forward_move );
			base->set_leftmove( left_move );
			return;
		}

		const auto wants_stop = features::combat::g_rage.should_stop( ) || features::misc::g_projectile_trajectory.should_stop( );

		if ( ( !settings::g_movement.airstrafe.value && !wants_stop )
			|| features::combat::g_rage.is_firing_this_tick( ) )
		{
			return;
		}

		if ( settings::g_movement.m_test_strafer.enabled.value && features::movement::g_test_strafer.handled_this_tick( ) )
		{
			return;
		}

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return;
		}

		this->check_button( current_buttons, cstypes::command_buttons::in_moveleft );
		this->check_button( current_buttons, cstypes::command_buttons::in_moveright );
		this->check_button( current_buttons, cstypes::command_buttons::in_forward );
		this->check_button( current_buttons, cstypes::command_buttons::in_back );
		this->m_last_buttons = current_buttons;

		const auto speed_2d = velocity.length_2d( );
		const auto effective_maxspeed = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) );

		if ( wants_stop && speed_2d <= 10.0f )
		{
			this->rotate_to_stop( base, velocity );
			return;
		}

		const auto sv_airaccelerate = CONVAR ("sv_airaccelerate")->get<float>( );
		const auto sv_air_max_wishspeed = CONVAR ("sv_air_max_wishspeed")->get<float>( );
		const auto surface_friction = prestate.surface_friction;

		auto yaw_offset{ 0.0f };
		if ( this->m_last_pressed & cstypes::command_buttons::in_moveleft )
		{
			yaw_offset += 90.0f;
		}
		if ( this->m_last_pressed & cstypes::command_buttons::in_moveright )
		{
			yaw_offset -= 90.0f;
		}
		if ( this->m_last_pressed & cstypes::command_buttons::in_forward )
		{
			yaw_offset *= 0.5f;
		}
		else if ( this->m_last_pressed & cstypes::command_buttons::in_back )
		{
			yaw_offset = -yaw_offset * 0.5f + 180.0f;
		}

		auto target_yaw = this->m_angles.y + yaw_offset;
		math::helpers::normalize_angle( target_yaw );

		const auto velocity_angle = std::atan2f( velocity.y, velocity.x ) * ( 180.0f / std::numbers::pi_v<float> );
		auto velocity_delta = target_yaw - velocity_angle;
		math::helpers::normalize_angle( velocity_delta );

		const auto accel_speed = sv_airaccelerate * effective_maxspeed * cstypes::tick_interval * surface_friction;
		const auto half_accel = accel_speed * 0.5f;
		const auto optimal_floor = std::fmaxf( half_accel, sv_air_max_wishspeed - half_accel );
		const auto ideal_angle = std::clamp( std::atanf( optimal_floor / std::fmaxf( speed_2d, 1.0f ) ) * ( 180.0f / std::numbers::pi_v<float> ), 0.0f, 45.0f );

		if ( ( std::fabsf( velocity_delta ) > 170.0f && speed_2d > 80.0f ) || ( velocity_delta > ideal_angle && speed_2d > 80.0f ) )
		{
			target_yaw = velocity_angle + ideal_angle;
			base->set_leftmove( -1.0f );
		}
		else if ( -ideal_angle <= velocity_delta || speed_2d <= 80.0f )
		{
			if ( this->m_side_switch )
			{
				target_yaw = target_yaw - ideal_angle;
				base->set_leftmove( -1.0f );
			}
			else
			{
				target_yaw = target_yaw + ideal_angle;
				base->set_leftmove( 1.0f );
			}
		}
		else
		{
			target_yaw = velocity_angle - ideal_angle;
			base->set_leftmove( 1.0f );
		}

		math::helpers::normalize_angle( target_yaw );

		this->rotate_movement( base, target_yaw, this->m_angles.y );
		this->m_side_switch = !this->m_side_switch;
	}

	void airstrafe::store_angles( )
	{
		this->m_angles = systems::g_input.get_view_angles( );
		this->m_angles.y = features::movement::g_movement_fix.source_yaw( );
	}

	void airstrafe::check_button( std::uintptr_t current_buttons, std::uintptr_t button )
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

	void airstrafe::rotate_movement( proto::base_usercmd_pb* base, float target_yaw, float view_yaw ) const
	{
		const auto forward_move = base->forwardmove( );
		const auto side_move = base->leftmove( );

		math::vector3 target_forward{}, target_left{};
		math::helpers::angle_vectors_2d( target_yaw, target_forward, target_left );

		math::vector3 view_forward{}, view_left{};
		math::helpers::angle_vectors_2d( view_yaw, view_forward, view_left );

		const auto tf = target_forward * forward_move;
		const auto tl = target_left * side_move;

		const auto corrected_forward = view_forward.dot( tf ) + view_forward.dot( tl );
		const auto corrected_side = view_left.dot( tf ) + view_left.dot( tl );

		base->set_forwardmove( std::clamp( corrected_forward, -1.0f, 1.0f ) );
		base->set_leftmove( std::clamp( corrected_side, -1.0f, 1.0f ) );
	}

	void airstrafe::rotate_to_stop( proto::base_usercmd_pb* base, const math::vector3& velocity ) const
	{
		const auto speed = velocity.length_2d( );
		const auto wish_yaw = std::atan2f( velocity.y, velocity.x ) * ( 180.0f / std::numbers::pi_v<float> ) + 180.0f;

		{
			const auto& ctx = features::combat::g_shared.ctx( );
			const auto max_speed = ( ctx.valid && ctx.weapon_vdata ) ? memory::read<float>( ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_hash ) ) : 250.0f;

			base->set_forwardmove( std::clamp( speed / max_speed, 0.0f, 1.0f ) );
			base->set_leftmove( 0.0f );
		}

		const auto rotation = ( this->m_angles.y - wish_yaw ) * ( std::numbers::pi_v<float> / 180.0f );
		const auto fwd = base->forwardmove( );
		const auto side = base->leftmove( );

		base->set_forwardmove( std::clamp( std::cosf( rotation ) * fwd - std::sinf( rotation ) * side, -1.0f, 1.0f ) );
		base->set_leftmove( std::clamp( ( std::sinf( rotation ) * fwd + std::cosf( rotation ) * side ) * -1.0f, -1.0f, 1.0f ) );
	}

} // namespace features::movement
