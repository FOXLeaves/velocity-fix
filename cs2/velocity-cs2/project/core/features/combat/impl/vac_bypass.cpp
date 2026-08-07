#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/fnv1a.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	// FVA parity: zero the values of the usercmd-echo cvars every tick so
	// the engine never mirrors audited commands to the console/log (the
	// VAC-live-bypass side of the FVA create_move pipeline, which zeroes
	// [cvar + 0x58] twice per tick).  c_convar::m_value lives at +0x58.
	void vac_bypass::suppress_cheat_cvars( ) const
	{
		static std::uintptr_t showusercmd{};
		static std::uintptr_t pred_print{};
		static int retry_countdown{};

		if ( !showusercmd || !pred_print )
		{
			// Resolution walks the full cvar registry - throttle retries
			// so a slow start never turns into a per-tick registry walk.
			if ( retry_countdown > 0 )
			{
				--retry_countdown;
			}
			else
			{
				retry_countdown = 256;
				if ( const auto cvar = addresses::globals::cvar )
				{
					showusercmd = reinterpret_cast< std::uintptr_t >(
						cvar->find( fnv1a::runtime_hash( xs( "cl_showusercmd" ) ) ) );
					pred_print = reinterpret_cast< std::uintptr_t >(
						cvar->find( fnv1a::runtime_hash( xs( "cl_pred_print_every_cmd" ) ) ) );
				}
			}
		}

		if ( showusercmd )
		{
			memory::safe_write< int >( showusercmd + 0x58, 0 );
		}

		if ( pred_print )
		{
			memory::safe_write< int >( pred_print + 0x58, 0 );
		}
	}

	// FVA parity: keep the per-tick input history populated with the
	// current engine view angles.  The audited history then never diverges
	// from the wire viewangles while the aim itself travels through the
	// hidden command view (see rage::fire_gun's vac-bypass path).
	//
	// VACNet parity (AI behaviour detection): pure silent aim leaves
	// mousedx/mousedy at zero while the view angles move - a machine
	// signature.  Reflect the per-tick view rotation into the mouse
	// deltas so the audited input stream stays self-consistent.
	void vac_bypass::on_create_move( systems::input::usercmd* cmd )
	{
		if ( !settings::g_combat.m_ragebot.vac_bypass.value )
		{
			return;
		}

		this->suppress_cheat_cvars( );

		// On a firing tick the rage path already wrote the attack entry
		// into the input history (angles, attack timestamp). Appending a
		// fresh entry here with the current client tick would sit after the
		// attack entry and can shadow it on the server - leave the history
		// alone while firing.
		if ( features::combat::g_rage.is_firing_this_tick( ) )
		{
			return;
		}

		const auto base = cmd ? cmd->csgo_user_cmd.mutable_base( ) : nullptr;
		if ( !base )
		{
			return;
		}

		const auto va = base->viewangles( );
		if ( !va )
		{
			return;
		}

		static math::vector3 last_angles{};
		static bool have_last_angles{};

		const math::vector3 current_angles{ va->x( ), va->y( ), va->z( ) };
		const bool last_valid = have_last_angles;
		const auto moved = last_valid
			? std::fabsf( current_angles.x - last_angles.x ) + std::fabsf( current_angles.y - last_angles.y )
			: 0.0f;

		if ( last_valid )
		{
			const auto sens = CONVAR( "sensitivity" )->get<float>( );
			if ( sens > 0.0f )
			{
				const auto deg_per_count = sens * 0.022f;
				base->set_mousedx( static_cast< int >( std::roundf( std::remainderf( current_angles.y - last_angles.y, 360.0f ) / deg_per_count ) ) );
				base->set_mousedy( static_cast< int >( std::roundf( -std::remainderf( current_angles.x - last_angles.x, 360.0f ) / deg_per_count ) ) );
			}
		}

		// 7/29 VACNet hardening: only push a history entry when the view
		// actually rotated - a dense per-tick entry stream with zero
		// rotation is an automated signature the AI detector learns.
		// (Fires still get their history entry from the rage path.)
		last_angles = current_angles;
		have_last_angles = true;

		if ( !last_valid || moved < 0.05f )
		{
			return;
		}

		systems::input::input_history_params params{};
		params.view_angles = current_angles;
		params.render_tick = base->client_tick( );
		params.render_frac = 1.0f;
		params.frame_number = base->legacy_command_number( );
		params.player_tick = base->client_tick( );
		params.player_frac = 1.0f;

		systems::g_input.push_input_history( cmd, params );
	}

} // namespace features::combat
