#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>

#include "../movement.hpp"
#include "utils.hpp"
#include <protection/game_addresses.hpp>

namespace features::movement {

	namespace {

		// Only near-flat landings (normal.z >= this) count as a successful
		// jumpbug; slopes are left to the bunnyhop.
		constexpr float k_flat_normal{ 0.985f };
		// Binary refinement passes for the contact fraction (high speed
		// means large per-tick steps that coarsen the initial trace).
		constexpr int k_refine_passes{ 4 };

	} // namespace

	void jumpbug::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_active_this_tick = false;

		if ( !settings::g_movement.jumpbug.value )
		{
			return;
		}

		if ( features::movement::g_edgebug.active_this_tick( ) )
		{
			return;
		}

		if ( !( cmd->buttons.value & cstypes::command_buttons::in_jump ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		if ( utils::is_restricted_move_type( local.pawn ) )
		{
			return;
		}

		// Airborne only: a jumpbug is a landing-time jump, never a
		// grounded one, and never while ascending.
		const auto& prestate = systems::g_prediction.pre( );
		if ( prestate.flags & cstypes::entity_flags::on_ground )
		{
			return;
		}

		if ( prestate.velocity.z > 0.0f || prestate.networked_velocity.z > 0.0f )
		{
			return;
		}

		// Frame-rate tracker: while create_move commands are being dropped
		// the whole-tick jump bit must stay held so a landing whose command
		// never arrives still auto-takeoffs (the chain cannot stop).
		const auto current_tick = features::combat::g_shared.ctx( ).current_tick;
		if ( this->m_last_tick != 0 )
		{
			const auto gap = std::max( current_tick - this->m_last_tick, 1 );
			this->m_avg_tick_gap = this->m_avg_tick_gap * 0.9f + static_cast< float >( gap ) * 0.1f;
		}
		this->m_last_tick = current_tick;
		const auto dropping_frames = this->m_avg_tick_gap > 1.05f;

		// Calibrate the subtick `when` bias from the last attempt: if
		// the local prediction is grounded again the edge fired before
		// the server's contact - nudge it later. A takeoff that kept
		// the speed (precise contact jump) decays it slightly; a takeoff
		// that LOST horizontal speed fired after the landing and pulls
		// the bias back earlier - converging on the exact contact
		// instead of oscillating on the edge.
		if ( this->m_jump_fired_prev )
		{
			if ( ( prestate.flags & cstypes::entity_flags::on_ground ) && prestate.velocity.z <= 0.0f )
			{
				this->m_when_bias = std::min( this->m_when_bias + 0.02f, 0.10f );
			}
			else if ( prestate.velocity.z > 0.0f )
			{
				const auto speed_2d = prestate.velocity.length_2d( );
				if ( this->m_pre_takeoff_speed_2d > 0.0f && speed_2d < this->m_pre_takeoff_speed_2d * 0.93f )
				{
					this->m_when_bias = std::max( this->m_when_bias - 0.02f, 0.0f );
				}
				else
				{
					this->m_when_bias = std::max( this->m_when_bias - 0.005f, 0.0f );
				}
			}

			this->m_jump_fired_prev = false;
		}

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return;
		}

		// Trace the tick's motion with the player hull. When ducked, the
		// standing hull is restored for the trace so the contact fraction
		// matches the unduck at contact.
		const auto holding_duck = ( cmd->buttons.value & cstypes::command_buttons::in_duck ) != 0;
		const auto duck_amount = memory::read<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flDuckAmount"_hash ) );

		auto hull = utils::player_hull( local.pawn );

		auto trace_origin = prestate.networked_origin;
		if ( holding_duck && duck_amount > 0.0f )
		{
			constexpr float standing_height{ 72.0f };
			const auto duck_hull_diff = standing_height - hull.maxs.z;
			trace_origin.z -= duck_hull_diff * 0.5f;
			hull.maxs.z = standing_height;
		}

		const auto filter = utils::movement_filter( local.pawn, movement_services );
		const auto sv_standable_normal = CONVAR( "sv_standable_normal" )->get<float>( );

		// Trace velocity uses the LOCAL prediction (m_vecAbsVelocity) with
		// the half-tick gravity correction: the predicted state is what the
		// server is about to simulate, while the networked velocity lags a
		// packet and systematically shifts the contact fraction. The
		// origin stays on the networked side so both stay in the same
		// world-space coordinate system. (when is a fraction inside the
		// command tick - bunnyhop proves this coordinate is honored on
		// official servers - so the trace stays single-tick; the press
		// edge tolerance below absorbs the residual drift instead.)
		const auto velocity = utils::gravity_corrected_velocity( prestate.velocity, local.pawn );

		const utils::step_trace_input step{ trace_origin, velocity, hull, filter, movement_services };
		const auto result = utils::trace_movement_step( step );
		const auto trace_end = trace_origin + velocity * cstypes::tick_interval - math::vector3{ 0.0f, 0.0f, 2.0f };

		if ( !( result.fraction > 0.0f && result.fraction < 1.0f ) || result.normal.z < sv_standable_normal )
		{
			// No contact this tick, but a small remaining fall distance
			// means the ground is one tick away: pre-issue the whole-tick
			// jump button so the server auto-takeoffs on the landing -
			// the jumpbug triggers reliably (slightly slower than the
			// perfect contact jump, but it never silently drops).
			const auto fall_dist = trace_origin.z - trace_end.z;
			if ( result.normal.z >= sv_standable_normal && fall_dist < 8.0f )
			{
				this->m_active_this_tick = true;
				cmd->buttons.value |= cstypes::command_buttons::in_jump;
				cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;
				cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_jump;
			}
			return;
		}

		// Slopes are not jumpbug material - hand them to the bunnyhop so a
		// failed attempt cannot stall the chain.
		if ( result.normal.z < k_flat_normal )
		{
			return;
		}

		// Binary-refine the contact fraction so the jump `when` stays
		// accurate on fast servers.
		const auto refined = utils::refine_contact_fraction( result, step, k_refine_passes );

		// Landing near the START or END of the tick: the exact contact
		// fraction cannot be expressed reliably (too early is ignored,
		// too late crosses the tick boundary), so fall back to the
		// whole-tick jump button - the server registers the landing this
		// tick or next and the held edge still takes off (slightly slower,
		// but the jumpbug never silently drops). The next landing
		// re-traces a mid-tick contact and jumps clean again.
		if ( refined < 0.03f || refined > 0.90f )
		{
			this->m_active_this_tick = true;

			cmd->buttons.value |= cstypes::command_buttons::in_jump;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;
			cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_jump;
			return;
		}

		// The jumpbug must fire at the exact contact fraction: pressing
		// AFTER the landing means the server already zeroed vz (the fall
		// registers) and the takeoff carries no speed - an "imperfect"
		// jumpbug. Pressing exactly at the contact makes the server apply
		// the landing and the jump edge on the same moment, keeping full
		// speed. The calibrated bias absorbs the trace's systematic drift
		// against the server. The whole-tick jump button is set as well so
		// servers that ignore/drop the subtick edges still get a takeoff
		// on the landing tick.
		auto when = std::clamp( refined + this->m_when_bias, 0.03f, 0.9f );

		this->m_active_this_tick = true;

		// Whole-tick jump fallback: the server processes the button at the
		// landing tick either way, and the precise subtick edges below
		// override it with the exact contact moment when they are honored.
		// At full frame rate the button is cleared instead so the server
		// only sees the subtick edges (a held bit would auto-takeoff on
		// the next landing and bleed speed every hop); while commands are
		// dropped it stays held so a landing tick whose command never
		// arrives still auto-takeoffs - the chain cannot stop.
		if ( dropping_frames )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_jump;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;
		}
		else
		{
			cmd->buttons.value &= ~cstypes::command_buttons::in_jump;
			cmd->buttons.value_changed &= ~cstypes::command_buttons::in_jump;
		}
		cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_jump;

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		const auto subtick_moves = base->mutable_subtick_moves( );

		// Duck from the start of the tick, unduck slightly before contact
		// so the landing happens fully standing.
		if ( const auto duck_down = systems::g_input.acquire_subtick_step( subtick_moves ) )
		{
			duck_down->set_button( cstypes::command_buttons::in_duck );
			duck_down->set_pressed( true );
			duck_down->set_when( 0.0f );
			duck_down->set_analog_forward_delta( 0.0f );
			duck_down->set_analog_left_delta( 0.0f );
		}

		if ( const auto duck_up = systems::g_input.acquire_subtick_step( subtick_moves ) )
		{
			duck_up->set_button( cstypes::command_buttons::in_duck );
			duck_up->set_pressed( false );
			duck_up->set_when( std::max( when - 0.01f, 0.0f ) );
			duck_up->set_analog_forward_delta( 0.0f );
			duck_up->set_analog_left_delta( 0.0f );
		}
		// Release-then-press jump exactly at contact: a clean press edge
		// landing on the ground frame fires the jump before the landing is
		// registered.
		if ( const auto jump_up = systems::g_input.acquire_subtick_step( subtick_moves ) )
		{
			jump_up->set_button( cstypes::command_buttons::in_jump );
			jump_up->set_pressed( false );
			jump_up->set_when( when );
			jump_up->set_analog_forward_delta( 0.0f );
			jump_up->set_analog_left_delta( 0.0f );
		}

		if ( const auto jump_down = systems::g_input.acquire_subtick_step( subtick_moves ) )
		{
			jump_down->set_button( cstypes::command_buttons::in_jump );
			jump_down->set_pressed( true );
			jump_down->set_when( when );
			jump_down->set_analog_forward_delta( 0.0f );
			jump_down->set_analog_left_delta( 0.0f );
		}

		// Redundant press edges for server landing drift: the exact trace
		// can be off by several percent on real servers, so emit a clean
		// press edge at the contact and four more at tight intervals. The
		// first edge that lands on the ground frame takes off; any later
		// edge fires in the air and is harmlessly ignored.
		const float retry_edges[ 4 ]{ when + 0.03f, when + 0.06f, when + 0.09f, when + 0.12f };
		for ( const auto retry_when : retry_edges )
		{
			const auto clamped_when = std::min( retry_when, 0.97f );

			if ( const auto jump_up_retry = systems::g_input.acquire_subtick_step( subtick_moves ) )
			{
				jump_up_retry->set_button( cstypes::command_buttons::in_jump );
				jump_up_retry->set_pressed( false );
				jump_up_retry->set_when( std::max( clamped_when - 0.01f, 0.0f ) );
				jump_up_retry->set_analog_forward_delta( 0.0f );
				jump_up_retry->set_analog_left_delta( 0.0f );
			}

			if ( const auto jump_down_retry = systems::g_input.acquire_subtick_step( subtick_moves ) )
			{
				jump_down_retry->set_button( cstypes::command_buttons::in_jump );
				jump_down_retry->set_pressed( true );
				jump_down_retry->set_when( clamped_when );
				jump_down_retry->set_analog_forward_delta( 0.0f );
				jump_down_retry->set_analog_left_delta( 0.0f );
			}
		}

		this->m_jump_fired_prev = true;
		this->m_pre_takeoff_speed_2d = prestate.velocity.length_2d( );
	}

} // namespace features::movement
