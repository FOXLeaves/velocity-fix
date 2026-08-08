#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>

#include "../movement.hpp"
#include "utils.hpp"
#include <protection/game_addresses.hpp>

namespace features::movement {

	namespace {

		constexpr std::uint32_t k_flag_on_ground{ 1u };

	} // namespace

	// Pure command-level predictive bunnyhop (no SendInput / no helper
	// thread): while the player holds jump, airborne commands clear the
	// jump bit (releasing the key) so the landing command can re-arm a
	// fresh 0->1 press edge. When the shared landing predictor finds the
	// contact INSIDE the current tick, the exact subtick release/press
	// edges are emitted at the contact fraction - the server takes off on
	// the landing frame at full speed instead of one tick later.
	void bhop::on_create_move( systems::input::usercmd* cmd )
	{
		if ( !settings::g_movement.bhop.value )
		{
			return;
		}

		// Jumpbug owns the landing tick; let it drive the jump input.
		if ( features::movement::g_jumpbug.active_this_tick( ) )
		{
			return;
		}

		// The player must be holding jump (command buttons only - the
		// engine button is the single source of truth now).
		if ( !( cmd->buttons.value & cstypes::command_buttons::in_jump ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn || !local.is_alive )
		{
			return;
		}

		if ( utils::is_restricted_move_type( local.pawn ) )
		{
			return;
		}

		const auto& prestate = systems::g_prediction.pre( );
		const auto on_ground = ( prestate.flags & k_flag_on_ground ) != 0;

		// Dual ground detection: the predicted flags (what the server is
		// about to simulate) plus the raw networked flags (the server's
		// current truth) - prediction desync used to stall the recovery
		// logic forever.
		const auto actual_ground = ( memory::read<std::uint32_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) ) & k_flag_on_ground ) != 0;
		const auto grounded = on_ground || actual_ground;

		if ( grounded )
		{
			++this->m_ground_ticks;

			// Landing tick: the airborne pass cleared the jump bit, so the
			// engine (whose internal key state still says "held") would
			// never generate a 0->1 edge by itself. Re-issue the press
			// edge here - on the FIRST grounded command (faster than the
			// old 2-tick gate); later commands keep re-issuing until the
			// takeoff actually happens.
			cmd->buttons.value |= cstypes::command_buttons::in_jump;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;

			if ( this->m_ground_ticks == 1 )
			{
				// Subtick press fallback: a clean 0->1 edge at the start
				// of the tick survives value clears and makes the server
				// take off even when the engine edge was lost.
				if ( const auto base = cmd->csgo_user_cmd.mutable_base( ) )
				{
					if ( const auto subtick_moves = base->mutable_subtick_moves( ) )
					{
						if ( const auto press_step = systems::g_input.acquire_subtick_step( subtick_moves ) )
						{
							press_step->set_button( cstypes::command_buttons::in_jump );
							press_step->set_pressed( true );
							press_step->set_when( 0.0f );
							press_step->set_analog_forward_delta( 0.0f );
							press_step->set_analog_left_delta( 0.0f );
						}
					}
				}
			}

			return;
		}

		this->m_ground_ticks = 0;

		// Airborne: release the key - the landing command re-arms a fresh
		// press edge.
		cmd->buttons.value &= ~cstypes::command_buttons::in_jump;
		cmd->buttons.value_changed &= ~cstypes::command_buttons::in_jump;

		// Only while falling (never on the ascending half of the hop).
		if ( prestate.velocity.z > 0.0f || prestate.networked_velocity.z > 0.0f )
		{
			return;
		}

		// Predict the landing inside THIS tick: emit release slightly
		// before the contact and press exactly at it, so the server
		// resolves the takeoff on the landing frame at full speed. The
		// shared predictor uses the same hull trace as jumpbug/edgebug.
		const auto landing = utils::predict_landing( local.pawn, prestate.networked_origin, prestate.velocity, ( cmd->buttons.value & cstypes::command_buttons::in_duck ) != 0 );
		if ( !landing.hit )
		{
			return;
		}

		if ( landing.normal_z < utils::standable_normal( ) )
		{
			// Slopes land on the next command's grounded pass.
			return;
		}

		// Contact at the very start/end of the tick cannot be expressed
		// reliably - the grounded pass handles it next command.
		if ( landing.fraction < 0.03f || landing.fraction > 0.90f )
		{
			return;
		}

		const auto when = std::clamp( landing.fraction, 0.03f, 0.9f );

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		const auto subtick_moves = base->mutable_subtick_moves( );

		const auto emit_edge = [ & ]( float press_when )
			{
				if ( const auto up = systems::g_input.acquire_subtick_step( subtick_moves ) )
				{
					up->set_button( cstypes::command_buttons::in_jump );
					up->set_pressed( false );
					up->set_when( std::max( press_when - 0.001f, 0.0f ) );
					up->set_analog_forward_delta( 0.0f );
					up->set_analog_left_delta( 0.0f );
				}

				if ( const auto down = systems::g_input.acquire_subtick_step( subtick_moves ) )
				{
					down->set_button( cstypes::command_buttons::in_jump );
					down->set_pressed( true );
					down->set_when( press_when );
					down->set_analog_forward_delta( 0.0f );
					down->set_analog_left_delta( 0.0f );
				}
			};

		emit_edge( when );

		// Redundant press edges for server landing drift: the exact trace
		// can be off by several percent on real servers, so emit more
		// edges at tight intervals - the first one landing on the ground
		// frame takes off, later ones fire in the air harmlessly.
		const float retry_edges[ 4 ]{ when + 0.03f, when + 0.06f, when + 0.09f, when + 0.12f };
		for ( const auto retry_when : retry_edges )
		{
			emit_edge( std::min( retry_when, 0.97f ) );
		}
	}

} // namespace features::movement