#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>

#include "../movement.hpp"
#include "utils.hpp"
#include <protection/game_addresses.hpp>

#include <Windows.h>
#pragma comment( lib, "winmm.lib" )

extern "C" unsigned int __stdcall timeBeginPeriod( unsigned int uPeriod );

namespace features::movement {

	namespace {

		constexpr std::uint32_t k_flag_on_ground{ 1u };

		// Hysteria-style landing prediction: a fast fall that suddenly has
		// its vertical speed killed (vz snaps from very negative toward
		// zero) is the landing moment - the server is about to register the
		// contact, and pulsing the jump key right then makes the server
		// resolve the takeoff on the landing tick at full speed.
		[[nodiscard]] bool landing_signature( bool was_falling, float previous_vz, float vz )
		{
			const auto fall_was_fast = was_falling || previous_vz < -120.0f;
			const auto vertical_speed_killed = vz > -12.0f && vz < 45.0f;
			const auto sharp_slowdown = ( vz - previous_vz ) > 90.0f;
			return fall_was_fast && vertical_speed_killed && sharp_slowdown;
		}

		[[nodiscard]] bool space_key_state( bool down )
		{
			INPUT input{};
			input.type = INPUT_KEYBOARD;
			input.ki.wVk = VK_SPACE;
			input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
			return SendInput( 1, &input, sizeof( input ) ) == 1;
		}

		// Landing state machine (per ground touch): only one jump pulse per
		// touch, with a short retry window so a pulse lost to timing still
		// takes off on the same landing.
		struct landing_state
		{
			bool was_on_ground{ false };
			bool jumped_this_touch{ false };
			bool was_falling{ false };
			float previous_vz{ 0.0f };
			bool jump_held{ false };
			std::uint64_t jump_release_at{ 0 };
			std::uint64_t retry_at{ 0 };
			std::uint64_t jumpbug_at{ 0 };
			std::uint64_t hold_grace_until{ 0 };
			float prev_modifier{ 1.0f };
		};

		landing_state g_state{};

		DWORD WINAPI bhop_thread_main( LPVOID )
		{
			while ( true )
			{
				const auto now = GetTickCount64( );

				// Release the jump pulse after its hold window.
				if ( g_state.jump_held && now >= g_state.jump_release_at )
				{
					space_key_state( false );
					g_state.jump_held = false;
					g_state.retry_at = now + 10;
				}

				if ( !settings::g_movement.bhop.value )
				{
					Sleep( 2 );
					continue;
				}

				// The player must be holding jump (or have released it
				// within the grace window - our own pulse is transient, so
				// the key state alone would flake between our down/up).
				if ( ( GetAsyncKeyState( VK_SPACE ) & 0x8000 ) != 0 )
				{
					g_state.hold_grace_until = now + 90;
				}
				if ( now >= g_state.hold_grace_until )
				{
					g_state.jumped_this_touch = false;
					Sleep( 2 );
					continue;
				}

				const auto local = systems::g_local.get( );
				if ( !local.pawn || !local.is_alive )
				{
					g_state.was_on_ground = false;
					g_state.was_falling = false;
					g_state.previous_vz = 0.0f;
					g_state.jumped_this_touch = false;
					Sleep( 2 );
					continue;
				}

				if ( utils::is_restricted_move_type( local.pawn ) )
				{
					Sleep( 2 );
					continue;
				}

				const auto flags = memory::read<std::uint32_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );
				const auto on_ground = ( flags & k_flag_on_ground ) != 0;
				const auto vel = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );

				// Predicted ground from the create_move prestate (read
				// loosely across threads): when the prediction says the
				// next command lands, the pulse fires even if the raw
				// flags/signature lag - a failed takeoff then re-arms
				// instead of stalling on the ground.
				const auto predicted_ground = ( systems::g_prediction.pre( ).flags & k_flag_on_ground ) != 0;

				const auto can_jump = on_ground || landing_signature( g_state.was_falling, g_state.previous_vz, vel.z ) || predicted_ground;

				// Landing-penalty detection: a very low landing (failed
				// takeoff, tiny hop) triggers the velocity-modifier
				// penalty. Its airtime can be shorter than one poll, so
				// wasOnGround never flips and jumpedThisTouch is never
				// reset - the chain then stalls on the ground. While the
				// modifier is suppressed (penalty recovery, ~1s) the
				// touch is forced open so every landing takes off.
				const auto modifier = memory::read<float>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_flVelocityModifier"_hash ) );
				if ( std::isfinite( modifier ) )
				{
					if ( modifier < 0.95f )
					{
						g_state.jumped_this_touch = false;
					}
					g_state.prev_modifier = modifier;
				}

				// A jumpbug that just fired owns this ground touch - but
				// only for a short window (~1.5 landings): if the jumpbug
				// misses (it is less reliable than the pulse) the thread
				// takes the touch back instead of stalling the chain.
				if ( features::movement::g_jumpbug.active_this_tick( ) )
				{
					g_state.jumpbug_at = now;
				}
				if ( now - g_state.jumpbug_at < 25 )
				{
					g_state.jumped_this_touch = true;
				}

				// Reset per-touch bookkeeping while airborne.
				if ( !g_state.was_on_ground && !on_ground )
				{
					g_state.jumped_this_touch = false;
					g_state.retry_at = 0;
				}

				g_state.was_on_ground = on_ground;

				// Vertical tracking for the landing signature.
				if ( on_ground )
				{
					g_state.was_falling = false;
					g_state.previous_vz = 0.0f;
				}
				else
				{
					if ( vel.z < -35.0f )
					{
						g_state.was_falling = true;
					}
					g_state.previous_vz = vel.z;
				}

				// Jump pulse with a retry window (Hysteria semantics): the
				// touch flag only suppresses re-pulses during the short
				// retry window - a pulse that fails to produce a takeoff
				// (still on the ground) is re-issued after the window
				// instead of stalling the chain forever. The release is
				// sent before the press so a stuck prior pulse cannot
				// swallow the new edge. (A per-tick gate made the pulse
				// fire every tick while the predicted flags lagged
				// "grounded" after a takeoff - the held bit bled speed on
				// every hop; the fixed window keeps the air quiet.)
				if ( can_jump && !g_state.jump_held && now >= g_state.retry_at )
				{
					space_key_state( false );
					if ( space_key_state( true ) )
					{
						g_state.jump_held = true;
						g_state.jump_release_at = now + 14;
						g_state.retry_at = now + 10;
						g_state.jumped_this_touch = true;
					}
				}

				Sleep( 2 );
			}

			return 0;
		}

	} // namespace

	void bhop::on_create_move( systems::input::usercmd* cmd )
	{
		// Per-command jump-key management (predictive bhop): while the
		// player holds jump, commands whose PREDICTED state is airborne
		// clear IN_JUMP and commands whose predicted state is grounded
		// keep it. The 0->1 transition on the landing command is the
		// engine-generated press edge the server resolves on the landing
		// tick - a full-speed takeoff with no SendInput latency (the
		// thread below covers the instant-landing case with a raw key
		// pulse).
		if ( !settings::g_movement.bhop.value )
		{
			return;
		}

		// Jumpbug owns the landing tick; let it drive the jump input.
		if ( features::movement::g_jumpbug.active_this_tick( ) )
		{
			return;
		}

		// The player must be holding jump (command buttons or the space
		// key; the async check survives the thread's transient pulses).
		const auto jump_held = ( cmd->buttons.value & cstypes::command_buttons::in_jump ) != 0
			|| ( GetAsyncKeyState( VK_SPACE ) & 0x8000 ) != 0;
		if ( !jump_held )
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
		// current truth). Prediction desync - the prediction staying
		// "airborne" while the server already landed - used to stall the
		// recovery logic forever; the raw flags cover it.
		const auto actual_ground = ( memory::read<std::uint32_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) ) & k_flag_on_ground ) != 0;
		const auto grounded = on_ground || actual_ground;

		// Airborne (by both views): clear the jump bit so the landing
		// command re-arms a fresh press edge - but skip the clear while
		// the high-frequency thread's SendInput pulse is holding the key
		// down, otherwise the clear would swallow the pulse and a failed
		// takeoff could never recover.
		//
		// Grounded: the first grounded command keeps the player's held
		// bit (engine 0->1 edge, full-speed takeoff) and also queues a
		// subtick press edge as an independent fallback - it survives the
		// value clears and makes the server take off even when the engine
		// edge was lost. If the takeoff still fails, later commands
		// re-issue the whole-tick press edge.
		if ( !grounded )
		{
			this->m_ground_ticks = 0;

			if ( !g_state.jump_held )
			{
				cmd->buttons.value &= ~cstypes::command_buttons::in_jump;
			}
		}
		else
		{
			++this->m_ground_ticks;

			if ( const auto base = cmd->csgo_user_cmd.mutable_base( ) )
			{
				if ( const auto subtick_moves = base->mutable_subtick_moves( ) )
				{
					// Press early in the tick: the server's landing check
					// runs during the tick, so the bit is already set when
					// the ground registers (slightly slower than the
					// precise contact edge, but it never depends on the
					// engine-generated value edge).
					if ( const auto press_step = systems::g_input.acquire_subtick_step( subtick_moves ) )
					{
						press_step->set_button( cstypes::command_buttons::in_jump );
						press_step->set_pressed( true );
						press_step->set_when( 0.1f );
						press_step->set_analog_forward_delta( 0.0f );
						press_step->set_analog_left_delta( 0.0f );
					}
				}
			}

			if ( this->m_ground_ticks >= 2 )
			{
				cmd->buttons.value |= cstypes::command_buttons::in_jump;
				cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;
			}
		}
	}

	void bhop::start_thread( )
	{
		static bool started{};
		if ( started )
		{
			return;
		}

		started = true;

		// 1 ms timer resolution makes the 2 ms poll actually sleep ~2 ms
		// (the system default quantum is ~15.6 ms, which would degrade the
		// landing detection back to create_move granularity).
		timeBeginPeriod( 1 );

		CreateThread( nullptr, 0, bhop_thread_main, nullptr, 0, nullptr );
	}

} // namespace features::movement
