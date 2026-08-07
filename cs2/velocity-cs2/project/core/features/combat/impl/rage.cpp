#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <utilities/diag.hpp>
#include <utilities/random/random.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
namespace features::combat {

	namespace {

		

	} // namespace

	void rage::on_create_move( systems::input::usercmd* cmd )
	{
		auto& ctx = g_shared.ctx( );
		const auto local = systems::g_local.get( );
		this->update_penetration_crosshair( local );

		// The R8 runs its own cock/release cycle (auto_revolver) and never
		// uses the double-tap pair - skip the DT state machine for it so
		// the charge bar / prediction never touch the revolver.
		if ( ctx.item_def_idx != cstypes::item_definition_index::weapon_r8_revolver )
		{
			this->m_double_tap.on_create_move( cmd );
		}

		if ( !ctx.valid )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		this->m_should_stop = false;
		this->m_firing_this_tick = false;

		if ( !settings::g_combat.m_duckpeek.enabled.value )
		{
			this->m_release_duck_for_shot = false;
			this->m_duckpeek_reduck = false;
		}

		if ( this->m_zeus_fired )
		{
			this->m_zeus_fired = false;

			if ( settings::g_combat.m_zeusbot.drop_after && !systems::g_local.is_in_deathmatch( ) )
			{
				memory::call<void>(PATTERN (patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, "drop", 0x7ffef001 );
			}

			return;
		}

		const auto is_knife = ctx.weapon_type == cstypes::weapon_type::knife;
		const auto is_taser = ctx.weapon_type == cstypes::weapon_type::taser;

		if ( !is_knife && !is_taser && ( ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg ) )
		{
			return;
		}

		auto aim_ctx = this->build_context( cmd, local );

		if ( is_knife )
		{
			if ( !g_shared.can_shoot( cmd, local.controller ) )
			{
				this->m_double_tap.on_fired( cmd, false );
				return;
			}

			this->run_knife( cmd, aim_ctx, local );
			this->m_double_tap.on_fired( cmd, this->m_firing_this_tick );
		}
		else if ( is_taser )
		{
			if ( !g_shared.can_shoot( cmd, local.controller ) )
			{
				return;
			}

			this->run_taser( cmd, aim_ctx, local );
		}
		else if ( ctx.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver )
		{
			this->auto_revolver( cmd, aim_ctx, local );
		}
		else
		{
			this->m_revolver_cock_ticks = 0;

			// Manual fire takes priority: while the player presses/holds
			// the attack button the entire ragebot decision (including the
			// silent-aim angle writes) is skipped and the shot follows the
			// player's crosshair - otherwise a tick where the ragebot
			// decision passes would take over the attack and leave the
			// PREVIOUS shot's aim angles on the wire/entries, which made
			// the next manual shot fly at the old aim point.
			const auto manual_press = ( cmd->buttons.value & cstypes::command_buttons::in_attack ) != 0
				|| cmd->csgo_user_cmd.attack1_start_history_index( ) >= 0;
			if ( manual_press )
			{
				this->m_double_tap.on_fired( cmd, false );
				return;
			}

			// Double tap needs the per-tick attempt: the second shot of the
			// pair is accepted by the server as soon as the locally synced
			// ready tick (m_nNextPrimaryAttackTick) catches up to the value
			// claimed on the first shot - the pair fires one network round
			// apart. The cycle rate-limit inside can_shoot would otherwise
			// hold every attempt until the full cycle elapsed and the pair
			// would never fire close together. The server still validates
			// each attempt against its own cooldown, so the per-tick attack
			// expression is harmless - it mirrors the manual rhythm that is
			// already known to deliver both shots.
			const auto dt_active = settings::g_combat.m_ragebot.m_double_tap.enabled.value;
			if ( !g_shared.can_shoot( cmd, local.controller, !dt_active ) )
			{
				this->m_double_tap.on_fired( cmd, false );
				return;
			}

			// The ragebot owns every shot of the pair: it re-aims and
			// delivers each bullet itself (freshest target angles), the DT
			// only applies the claim and marks the pair rhythm.
			this->run_gun( cmd, aim_ctx, local );
			this->m_double_tap.on_fired( cmd, this->m_firing_this_tick );
		}
	}

	// Auto scope for snipers: engage the scope only when a viable target
	// is actually locked (has_best) and the scope is not up yet. The
	// press edge is issued exactly once per engagement - once is_scoped
	// is observed the session is locked and NO zoom input is sent at all
	// for the rest of the engagement, so the server keeps the scope and
	// the player can close it manually. CS2 zoom is toggle-based (each
	// right-click edge cycles 2.5x -> 5x -> closed), so re-issuing an
	// edge while already scoped would flip to the second magnification
	// level - or close the scope entirely - on every shot.
	//
	// CS2 registers the zoom on the secondary-attack (right mouse)
	// button; in_zoom is a client-side UI bit that servers ignore. Both
	// are sent so the scope actually engages.
	void rage::update_auto_scope( systems::input::usercmd* cmd, bool has_best )
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );

		if ( settings::g_combat.m_autos.scope.value
			&& shared_ctx.weapon_type == cstypes::weapon_type::sniper
			&& !( config.no_spread.value && config.no_spread_mode.value == settings::combat::ragebot::weapon_group::no_spread_mode::forced ) )
		{
			// Scope registered (toggle-based): lock the session and let go
			// entirely. Any further edge would cycle to the 2.5x->5x level
			// or close the scope - no more zoom input until the target
			// disappears and the next engagement starts fresh.
			if ( shared_ctx.is_scoped )
			{
				this->m_scope_open = true;
				return;
			}

			if ( has_best && !this->m_scope_open )
			{
				// First engagement edge: one press cycles the scope in
				// (2.5x). The edge is sent exactly once per engagement.
				cmd->buttons.value |= cstypes::command_buttons::in_zoom | cstypes::command_buttons::in_second_attack;
				cmd->buttons.value_changed |= cstypes::command_buttons::in_zoom | cstypes::command_buttons::in_second_attack;
				this->m_scope_open = true;
			}
			else if ( !has_best )
			{
				// Target gone: the engagement session ends, the next lock
				// may re-scope.
				this->m_scope_open = false;
			}
			// Session locked (m_scope_open, edge already issued or scope
			// confirmed): no zoom input of any kind - not even a held
			// value bit, since the toggle state must not be disturbed.
		}
		else
		{
			this->m_scope_open = false;
		}
	}

	void rage::on_render( xdraw::draw_list& draw_list )
	{
		// The R8 never uses the double-tap pair - no charge bar for it.
		if ( g_shared.ctx( ).item_def_idx != cstypes::item_definition_index::weapon_r8_revolver )
		{
			this->m_double_tap.on_render( draw_list );
		}

		this->draw_penetration_crosshair( draw_list );

		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		if ( !config.debug_multipoints.value )
		{
			return;
		}
		std::lock_guard lock( m_debug_mtx );

		for ( const auto& pt : m_debug_points )
		{
			const auto screen = systems::g_view.project( pt.position );
			if ( !systems::g_view.projection_valid( screen ) )
			{
				continue;
			}

			xdraw::color col{};
			switch ( pt.hitbox_index )
			{
			case 0:
				col = { 255, 80,  80  }; break; // head 閿?red
			case 2: case 3:
				col = { 220, 220, 60  }; break; // stomach 閿?yellow
			case 4: case 5: case 6:
				col = { 255, 160, 60  }; break; // chest 閿?orange
			case 7: case 8: case 9: case 10: case 11: case 12:
				col = { 80,  160, 255 }; break; // legs 閿?blue
			case 13: case 14: case 15: case 16: case 17: case 18:
				col = { 180, 80,  255 }; break; // arms 閿?purple
			default:
				col = { 200, 200, 200 }; break;
			}

			const auto alpha  = pt.is_center ? std::uint8_t{ 255 } : std::uint8_t{ 160 };
			const auto radius = pt.is_center ? 3.5f : 2.0f;

			draw_list.circle_filled( screen.x, screen.y, radius, col.alpha( alpha ) );
		}
	}

	// --- double tap ----------------------------------------------------------

	void rage::double_tap::reset( ) noexcept
	{
		this->m_state = charge_state::charging;
		this->m_fail_reason = fail_reason::none;
		this->m_shot_count = 0;
		this->m_last_fire_tick = -1;
		// Full fire-state reset on weapon switch: a stale rhythm flag or
		// pending auto second shot must not carry over to the new weapon.
		this->m_fired = false;
		this->m_auto_second = false;
		this->m_ready_tick = -1;
		this->m_state_hold = 0;
		this->m_ticks_to_ready = 0;
		this->m_progress = 0.0f;
	}

	void rage::double_tap::fail( fail_reason reason ) noexcept
	{
		this->m_state = charge_state::failed;
		this->m_fail_reason = reason;
		this->m_state_hold = 60;
		this->m_progress = 0.0f;
	}

	void rage::double_tap::on_create_move( systems::input::usercmd* cmd )
	{
		const auto& cfg = settings::g_combat.m_ragebot.m_double_tap;
		const auto& ctx = g_shared.ctx( );

		if ( !cfg.enabled.value || !ctx.valid )
		{
			this->reset( );
			return;
		}

		const auto is_gun = ctx.weapon_type >= cstypes::weapon_type::pistol && ctx.weapon_type <= cstypes::weapon_type::lmg;
		const auto is_knife = ctx.weapon_type == cstypes::weapon_type::knife;
		if ( ( !is_gun && !is_knife ) || !ctx.weapon )
		{
			this->reset( );
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.controller )
		{
			this->reset( );
			return;
		}

		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

		// Safety: if the auto second shot does not resolve within one fire
		// interval (plus a small buffer), the request is dropped and the
		// charge restarts - a stale request must not open a shot on its
		// own later.
		if ( this->m_auto_second && tick_base - this->m_last_fire_tick > std::max( this->m_cycle_ticks, 1 ) + 5 )
		{
			this->m_auto_second = false;
		}

		if ( ctx.weapon != this->m_weapon )
		{
			this->m_weapon = ctx.weapon;
			this->reset( );
		}

		const auto next_off = SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash );
		const auto ratio_off = SCHEMA( "C_BasePlayerWeapon", "m_flNextPrimaryAttackTickRatio"_hash );
		if ( !next_off || !ratio_off )
		{
			this->reset( );
			return;
		}

		const auto next_opt = memory::safe_read<int>( ctx.weapon + next_off );
		const auto ratio_opt = memory::safe_read<float>( ctx.weapon + ratio_off );
		if ( !next_opt || !ratio_opt || !std::isfinite( *ratio_opt ) || std::abs( *next_opt ) > 100000 )
		{
			// Cooldown fields unreadable (schema drift or stale weapon):
			// charge is unusable this tick.
			this->reset( );
			return;
		}

		this->m_next_attack = *next_opt;
		this->m_ratio = *ratio_opt;

		// Claimed tick for the wire AND the aim alignment. Normally the
		// weapon-ready tick; in the second-shot window (1-2 ticks after the
		// last delivered attack) the PREDICTED server cooldown - the server
		// sets next to exactly that value when it accepts the first shot,
		// so claim == next passes immediately and the second bullet fires
		// reliably without waiting for the local sync. Keeping the aim on
		// the same tick (claimed_tick()) makes the rewound pose match the
		// aimed one.
		//
		// The prediction is gated on the local ready tick NOT having synced
		// yet (still equal to the last delivered claim): on local/zero-
		// latency servers (practice maps with bots) the sync is nearly
		// instant, so predicting there would overshoot by a full cycle and
		// the server would rewind the target to a future pose - the second
		// bullet then flies past a moving target.
		const auto since_last_claim = tick_base - this->m_last_fire_tick;
		const auto predicted_next = this->m_next_attack + this->m_cycle_ticks;
		this->m_claim_predicted = since_last_claim >= 1 && since_last_claim <= 2
			&& this->m_next_attack == this->m_last_claimed_tick;
		this->m_claimed_for_aim = this->m_claim_predicted
			? predicted_next
			: this->m_next_attack;

		// m_flWatTickOffset is a newer schema field; when it is missing or
		// reads garbage the charge arithmetic simply ignores it.
		this->m_wat_offset = 0.0f;
		if ( const auto wat_off = SCHEMA( "C_BasePlayerWeapon", "m_flWatTickOffset"_hash ) )
		{
			if ( const auto wat = memory::safe_read<float>( ctx.weapon + wat_off ) )
			{
				if ( std::isfinite( *wat ) && std::abs( *wat ) < 1000.0f )
				{
					this->m_wat_offset = *wat;
				}
			}
		}

		// Weapon-ready tick (classic charge arithmetic): the sub-tick part
		// of the weapon's attack-time offset joins the ratio, and the
		// integer part shifts the cooldown base.
		double offset_tick{};
		const auto wat_fraction = std::modf( static_cast< double >( this->m_wat_offset ), &offset_tick );
		const auto temp = static_cast< double >( this->m_ratio ) + wat_fraction;

		auto shoot_tick = this->m_next_attack + static_cast< int >( offset_tick );
		if ( temp >= 1.0 )
		{
			++shoot_tick;
		}
		else if ( temp < 0.0 )
		{
			--shoot_tick;
		}
		this->m_shoot_tick = shoot_tick;
		this->m_ticks_to_ready = std::max( this->m_shoot_tick - tick_base, 0 );

		float cycle{ 0.1f };
		if ( ctx.weapon_vdata )
		{
			cycle = memory::read<float>( ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flCycleTime"_hash ) );
		}
		this->m_cycle_ticks = std::max( 1, static_cast< int >( std::ceil( cycle / cstypes::tick_interval ) ) );

		const auto clip = memory::read<int>( ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_hash ) );
		const auto reloading = memory::read<bool>( ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_bInReload"_hash ) );
		const auto ready_ok = tick_base >= this->m_shoot_tick && clip > 0 && !reloading;

		// Charge tracking by AMMO, not by attack-expression bookkeeping:
		// the clip difference between ticks is exactly how many rounds
		// really left the barrel (server-synced), so a real two-shot pair
		// always counts two and the bar resets - expression counting
		// missed shots delivered outside the alternation and left the bar
		// stuck at 50%.
		if ( this->m_prev_clip >= 0 && clip < this->m_prev_clip )
		{
			this->m_shot_count = std::min( this->m_shot_count + ( this->m_prev_clip - clip ), 2 );
		}
		this->m_prev_clip = clip;

		switch ( this->m_state )
		{
		case charge_state::charging:
		case charge_state::ready:
			if ( reloading )
			{
				this->fail( fail_reason::reloading );
			}
			else if ( clip <= 0 )
			{
				this->fail( fail_reason::empty_clip );
			}
			else
			{
				this->m_state = ready_ok ? charge_state::ready : charge_state::charging;
			}
			break;
		case charge_state::failed:
			if ( this->m_state_hold > 0 )
			{
				--this->m_state_hold;
			}
			if ( this->m_state_hold == 0 )
			{
				this->m_fail_reason = fail_reason::none;
				this->m_state = ready_ok ? charge_state::ready : charge_state::charging;
			}
			break;
		default:
			break;
		}

		// Knife: the bar is pinned at 100%; a melee attack resets it to
		// zero for a tick, then it jumps straight back to 100%.
		if ( g_shared.ctx( ).weapon_type == cstypes::weapon_type::knife )
		{
			this->m_state = charge_state::ready;
			this->m_progress = ( tick_base - this->m_last_fire_tick ) <= 1 ? 0.0f : 1.0f;
			return;
		}

		// Preview progress - charge state machine for the pair:
		//   shot_count == 0: linear charge 0 -> 100% over TWO weapon
		//                    cycles (one shot charged = 50%, both = 100%)
		//   shot_count == 1: stuck at 50% - one bullet left, the bar does
		//                    NOT recharge: only firing the second bullet,
		//                    reloading or switching weapons resets it
		//   shot_count >= 2: both bullets gone - reset to 0 and recharge
		//                    from zero
		switch ( this->m_state )
		{
		case charge_state::charging:
		case charge_state::ready:
		{
			const auto window = std::max( this->m_cycle_ticks, 1 );

			// Manual reload (or the ragebot auto-reloading between fights)
			// restarts the charge from zero.
			if ( reloading )
			{
				this->m_shot_count = 0;
			}

			// Both shots of the pair delivered: recharge from zero.
			if ( this->m_shot_count >= 2 )
			{
				this->m_shot_count = 0;
			}

			if ( this->m_shot_count == 1 )
			{
				// One bullet fired, one left: stuck at 50% until the
				// second bullet leaves (or reload / weapon switch).
				this->m_progress = 0.5f;
				break;
			}

			// Linear charge from zero: two weapon cycles fill the bar.
			float charge{};
			if ( this->m_ticks_to_ready > 1 )
			{
				// First cycle: weapon cooldown, 0 -> 50%. The 1-tick
				// tolerance absorbs the predicted ready-tick jitter.
				this->m_ready_tick = -1;
				const auto elapsed = static_cast< float >( window - std::max( this->m_ticks_to_ready - 1, 0 ) );
				charge = elapsed / ( 2.0f * static_cast< float >( window ) );
			}
			else
			{
				// Second cycle: pre-load after ready, 50 -> 100%.
				if ( this->m_ready_tick < 0 )
				{
					this->m_ready_tick = tick_base;
				}

				const auto since_ready = std::max( tick_base - this->m_ready_tick, 0 );
				const auto elapsed = static_cast< float >( window ) + static_cast< float >( std::min( since_ready, window ) );
				charge = elapsed / ( 2.0f * static_cast< float >( window ) );
			}

			this->m_progress = std::clamp( charge, 0.0f, 1.0f );
			break;
		}
		case charge_state::failed:
		default:
			this->m_progress = 0.0f;
			break;
		}
	}
	void rage::double_tap::on_fired( systems::input::usercmd* cmd, bool fired )
	{
		// UC reference (Pa1nt4r): static bool Fired, NextAttackTick claim,
		// subtick attack edge, RemoveButton on the skipped beat and
		// InvalidatetAttackIndex - kept structurally identical, only the
		// API calls are adapted to this codebase.
		//
		// Two mirrored rhythms, one per firing source:
		//  - Manual rhythm  (fired == false): the engine produces the
		//    attack (button + index), DT alternates and claims the ticks.
		//  - Ragebot rhythm (fired == true ): fire_gun only writes the
		//    silent-aim angles into the entries; this block is the SOLE
		//    attack expression (button + index + edge), alternated the
		//    same way so the server resolves each edge against its
		//    claimed tick instead of treating the stream as auto-fire.

		const auto& ctx = g_shared.ctx( );
		if ( this->m_weapon != ctx.weapon )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.controller )
		{
			return;
		}

		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

		// Knife: only the attack tick matters for the bar; the DT attack
		// logic does not apply to melee.
		if ( g_shared.ctx( ).weapon_type == cstypes::weapon_type::knife )
		{
			if ( fired )
			{
				this->m_last_fire_tick = tick_base;
			}
			return;
		}

		const auto dt_active = settings::g_combat.m_ragebot.m_double_tap.enabled.value;
		const auto history_size = cmd->csgo_user_cmd.input_history_size( );

		const auto stamp_entries = [ & ]( int claimed_tick )
			{
				for ( auto i = 0; i < history_size; ++i )
				{
					if ( auto* entry = cmd->csgo_user_cmd.mutable_input_history( i ) )
					{
						entry->set_player_tick_count( claimed_tick );
						entry->set_player_tick_fraction( 0.0f );
					}
				}
			};

		if ( fired )
		{
			// ============================================================
			// Ragebot double-tap rhythm - wire pattern identical to the
			// manual rhythm (Pa1nt4r reference), fire_gun provides the aim:
			//   ShouldAttack = CanAttack && dt   (dt = ragebot decision)
			//   claim on every entry = m_claimed_for_aim:
			//     1st shot = weapon-ready tick
			//     2nd shot = predicted cooldown (1st + cycle) - the server
			//                accepts it right after the 1st
			//   if (ShouldAttack && !Fired) { subtick IN_ATTACK pressed }
			//   Fired = ShouldAttack && !Fired
			//   InvalidatetAttackIndex()
			// The alternation keeps the edge stream off the wire: a
			// continuous edge stream would be resolved as plain auto-fire
			// and the claimed ticks would stop mattering.
			// The SECOND shot of the pair bypasses the alternation on the
			// very next tick (1-tick pair): its claim equals the server's
			// next, so it is accepted immediately and the pair lands 1 tick
			// apart. m_fired stays latched through that tick so no third
			// edge follows and the wire does not become a stream.
			// ============================================================
			const auto should_attack = g_shared.can_shoot( cmd, local.controller, !dt_active );

			// Claim sanity: a claim far ahead of the local tick means the
			// synced ready tick (or the prediction) is garbage - expressing
			// the attack anyway would be silently rejected by the server
			// (an empty shot that still plays the fire animation) or push
			// its cooldown onto a wrong frame. No lower bound: a weapon
			// that has been ready for a long time legitimately claims an
			// old ready tick and the server accepts it (old >= old).
			const auto claim_valid = this->m_claimed_for_aim <= tick_base + 256;

			const auto& attack_config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
			const auto no_spread_comp = attack_config.no_spread.value
				|| attack_config.no_spread_mode.value == settings::combat::ragebot::weapon_group::no_spread_mode::seed;

			// No-spread stamps its own entries with the exact tick the
			// seed correction was computed against (m_no_spread_claim_tick
			// set by fire_gun): overwriting them with the DT claim (or 0
			// when the claim is invalid / DT is off) desyncs the server's
			// spread seed from the correction and every bullet flies off.
			stamp_entries( no_spread_comp
				? this->no_spread_claim_tick( )
				: ( should_attack && claim_valid ? this->m_claimed_for_aim : 0 ) );

			const auto immediate_second = should_attack && ( tick_base - this->m_last_fire_tick ) == 1;

			if ( should_attack && claim_valid && ( !this->m_fired || immediate_second ) )
			{
				this->m_last_fire_tick = tick_base;
				this->m_last_shot_manual = false;
				this->m_last_claimed_tick = this->m_claimed_for_aim;

				// Attack edge: subtick only, exactly like the reference.
				// fire_gun wrote the silent-aim angles into EVERY entry, so
				// the server resolves the shot against the ragebot's aim
				// whichever entry it matches to the claimed tick.
				//
				// Fresh-angle guard: fire_gun only refreshes the entries on
				// ticks its fire decision passes - on a beat where the
				// decision flickers, the entries would keep the PREVIOUS
				// shot's angles and this bullet would fly at the old aim
				// point. m_dt_aim is updated by run_gun every tick from the
				// live scan, so stamp it over the entries as the fallback.
				// No-spread keeps fire_gun's corrected angles untouched.
				{
					const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
					if ( !config.no_spread.value && this->m_has_dt_aim && history_size > 0 )
					{
						const auto& angles = this->m_dt_aim;
						for ( auto i = 0; i < history_size; ++i )
						{
							if ( auto* entry = cmd->csgo_user_cmd.mutable_input_history( i ) )
							{
								if ( auto* va = entry->mutable_view_angles( ) )
								{
									va->set_x( angles.x );
									va->set_y( angles.y );
									va->set_z( angles.z );
								}
							}
						}
					}
				}

				// Attack edge: the subtick move only, exactly like the
				// reference. No-spread expresses its shot through the
				// button + attack index that fire_gun set (the index is
				// what makes the server use the corrected entry angles), so
				// no extra subtick edge is added for it.
				if ( !attack_config.no_spread.value )
				{
					if ( auto* base = cmd->csgo_user_cmd.mutable_base( ) )
					{
						if ( auto* step = systems::g_input.acquire_subtick_step( base->mutable_subtick_moves( ) ) )
						{
							step->set_button( cstypes::command_buttons::in_attack );
							step->set_pressed( true );
						}
					}
				}

				// Remember the freshest aim angles.
				if ( history_size > 0 )
				{
					if ( auto* e0 = cmd->csgo_user_cmd.mutable_input_history( 0 ) )
					{
						if ( e0->has_view_angles( ) )
						{
							this->m_last_angles = math::vector3{ e0->view_angles( )->x( ), e0->view_angles( )->y( ), e0->view_angles( )->z( ) };
							this->m_has_last_angles = true;
						}
					}
				}

				// Latch through the immediate second shot so the following
				// tick skips the attack (no third consecutive edge).
				if ( immediate_second )
				{
					this->m_fired = true;
				}
			}

			if ( !immediate_second )
			{
				this->m_fired = should_attack && !this->m_fired;
			}

			// Cmd->InvalidatetAttackIndex() - kept for the regular path.
			// No-spread keeps the index fire_gun pointed at the corrected
			// entry (that index is what resolves the shot through the
			// compensated angles).
			if ( !attack_config.no_spread.value )
			{
				cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
			}
			return;
		}

		// ================================================================
		// Manual rhythm - reference structure (Pa1nt4r):
		//   ShouldAttack = CanAttack(nClientTick) && dt
		//   nPlayerTickCount = ShouldAttack ? NextAttackTick : 0 (all entries)
		//   if (ShouldAttack && !Fired) { subtick IN_ATTACK pressed }
		//   else { RemoveButton(IN_ATTACK); dt = false; }
		//   Fired = ShouldAttack && !Fired
		//   InvalidatetAttackIndex()
		// dt is driven by the engine-delivered attack / the held button;
		// the alternating subtick edge is the ONLY attack expression, and
		// the claimed tick compresses the server cooldown so the next
		// attempt is accepted right after the sync - that is what makes
		// the pair land ~one beat apart.
		// ================================================================
		const auto manual_attack = cmd->csgo_user_cmd.attack1_start_history_index( ) >= 0;
		const auto button_held = ( cmd->buttons.value & cstypes::command_buttons::in_attack ) != 0;

		const auto dt = manual_attack || button_held;
		const auto should_attack = dt && g_shared.can_shoot( cmd, local.controller, !dt_active );

		stamp_entries( should_attack ? this->m_next_attack : 0 );

		if ( should_attack && !this->m_fired )
		{
			this->m_last_fire_tick = tick_base;
			this->m_last_shot_manual = true;
			this->m_last_claimed_tick = this->m_next_attack;

			// A manual attack must fire along the player's OWN view.
			// fire_gun may have written silent-aim angles into the entries
			// on an earlier ragebot decision (and it is not running this
			// tick), so without restoring them the manual shot would be
			// resolved against the LAST bot aim point - the "fires at the
			// previous aim point" bug. The wire base viewangles are left
			// untouched (anti-aim owns them).
			if ( dt && history_size > 0 )
			{
				const auto view = systems::g_input.get_view_angles( );

				for ( auto i = 0; i < history_size; ++i )
				{
					if ( auto* entry = cmd->csgo_user_cmd.mutable_input_history( i ) )
					{
						if ( auto* va = entry->mutable_view_angles( ) )
						{
							va->set_x( view.x );
							va->set_y( view.y );
							va->set_z( view.z );
						}
					}
				}
			}

			// Attack edge: the subtick move only, exactly like the
			// reference - the engine button (if any) stays as-is.
			if ( auto* base = cmd->csgo_user_cmd.mutable_base( ) )
			{
				if ( auto* step = systems::g_input.acquire_subtick_step( base->mutable_subtick_moves( ) ) )
				{
					step->set_button( cstypes::command_buttons::in_attack );
					step->set_pressed( true );
				}
			}

			// Remember the freshest aim angles.
			if ( history_size > 0 )
			{
				if ( auto* e0 = cmd->csgo_user_cmd.mutable_input_history( 0 ) )
				{
					if ( e0->has_view_angles( ) )
					{
						this->m_last_angles = math::vector3{ e0->view_angles( )->x( ), e0->view_angles( )->y( ), e0->view_angles( )->z( ) };
						this->m_has_last_angles = true;
					}
				}
			}
		}
		else
		{
			// Cmd->nButtons.RemoveButton(IN_ATTACK); dt = false;
			cmd->buttons.value &= ~cstypes::command_buttons::in_attack;
			cmd->buttons.value_changed &= ~cstypes::command_buttons::in_attack;
			cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_attack;
		}

		// Fired = ShouldAttack && !Fired;
		this->m_fired = should_attack && !this->m_fired;

		// Cmd->InvalidatetAttackIndex();
		cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
	}
	void rage::double_tap::on_render( xdraw::draw_list& draw_list ) const
	{
		const auto& cfg = settings::g_combat.m_ragebot.m_double_tap;
		if ( !cfg.enabled.value || !cfg.preview.value )
		{
			return;
		}

		if ( systems::g_local.is_in_cinematic( ) || systems::g_local.is_in_time_freeze( ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.controller || !local.is_alive )
		{
			return;
		}

		const auto [ screen_w, screen_h ] = xdraw::viewport_size( );
		const auto cx = static_cast< float >( screen_w ) * 0.5f;
		const auto& s = xui::ctx( ).style;

		// State is expressed purely through the bar color.
		xdraw::color state_col{ 255, 255, 255, 255 };
		switch ( this->m_state )
		{
		case charge_state::charging: state_col = cfg.charging_color; break;
		case charge_state::ready:    state_col = cfg.ready_color;    break;
		case charge_state::failed:   state_col = cfg.failed_color;   break;
		default: break;
		}

		constexpr auto bar_w{ 214.0f };
		constexpr auto bar_h{ 16.0f };
		constexpr auto pad{ 2.0f };
		constexpr auto r{ 8.0f };
		constexpr auto bottom_offset{ 170.0f };

		const auto bar_x = std::floor( cx - bar_w * 0.5f );
		const auto bar_y = std::floor( static_cast< float >( screen_h ) - bottom_offset - bar_h );

		draw_list.rect_filled_blurred( bar_x - pad, bar_y - pad, bar_w + pad * 2.0f, bar_h + pad * 2.0f, xdraw::corner_radius{ r } );
		draw_list.rect_filled( bar_x - pad, bar_y - pad, bar_w + pad * 2.0f, bar_h + pad * 2.0f, s.window_bg, xdraw::corner_radius{ r } );
		draw_list.rect_filled( bar_x, bar_y, bar_w, bar_h, s.child_bg, xdraw::corner_radius{ r - pad } );

		const auto fill_w = std::floor( bar_w * this->m_progress );
		if ( fill_w >= 1.0f )
		{
			draw_list.rect_filled( bar_x, bar_y, fill_w, bar_h, state_col.alpha( 220 ), xdraw::corner_radius{ r - pad } );
		}

		char pct_buf[ 8 ]{};
		std::snprintf( pct_buf, sizeof( pct_buf ), "%.0f%%", this->m_progress * 100.0f );
		const auto [ pct_w, pct_h ] = xdraw::measure_text( pct_buf );
		draw_list.text( bar_x + bar_w - pct_w - 6.0f, bar_y + ( bar_h - pct_h ) * 0.5f + 0.5f, pct_buf, s.text );
	}

	rage::aim_context rage::build_context( systems::input::usercmd* cmd, const systems::local::snapshot& local ) const
	{
		auto& ctx = g_shared.ctx( );
		const auto& prestate = systems::g_prediction.pre( );

		aim_context out{};
		out.velocity = prestate.velocity;
		out.spread = g_shared.get_spread( );
		out.predicted_inaccuracy = g_shared.get_inaccuracy( true );

		systems::g_prediction.simulate( cmd, local, [ & ]
			{
				g_shared.sh( ).snapshot( local.pawn, ctx.weapon_services );

				out.velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
				out.spread = g_shared.get_spread( );
				out.predicted_inaccuracy = g_shared.get_inaccuracy( true );
			} );

		ctx.spread = out.spread;
		ctx.inaccuracy = out.predicted_inaccuracy;

		out.view_angles = systems::g_input.get_view_angles( );
		out.on_ground = ( prestate.flags & cstypes::entity_flags::on_ground ) != 0;
		out.is_scoped = ctx.is_scoped;
		out.weapon_max_speed = ctx.weapon_max_speed;
		out.accurate_threshold = ctx.weapon_max_speed * 0.34f;

		return out;
	}

	void rage::clear_attack_button( systems::input::usercmd* cmd ) const
	{
		// The attack edge is expressed exclusively through the button state
		// and the attack-start history index. Leaving a "pressed this tick" bit
		// (buttonstate2) set on every command makes the server treat the button
		// as continuously toggling, which fires non-stop while doubletap is
		// active.
		cmd->buttons.value &= ~cstypes::command_buttons::in_attack;
		cmd->buttons.value_changed &= ~cstypes::command_buttons::in_attack;
		cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_attack;
		cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
	}

	std::optional<rage::stop_prediction> rage::predict_stop( const aim_context& ctx, const math::vector3& current_eye, const systems::local::snapshot& local ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto& prestate = systems::g_prediction.pre( );
		const auto speed = prestate.networked_velocity.length_2d( );
		const auto will_stop = ctx.on_ground && ( speed > ctx.accurate_threshold || ( ctx.is_scoped && speed > 1.0f ) );

		if ( !will_stop )
		{
			return std::nullopt;
		}

		auto sim_vel = prestate.networked_velocity;
		sim_vel.z = 0.0f;

		const auto sv_friction = CONVAR("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR("sv_stopspeed")->get<float>( );
		const auto sv_accelerate = CONVAR("sv_accelerate")->get<float>( );
		const auto surface_friction = prestate.surface_friction;

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		const auto max_move_speed = movement_services ? memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) ) : 250.0f;

		for ( auto i = 0; i < 15; ++i )
		{
			const auto sim_speed = sim_vel.length_2d( );
			if ( sim_speed < 1.0f )
			{
				break;
			}

			const auto control = std::fmaxf( sim_speed, sv_stopspeed );
			const auto drop = sv_friction * surface_friction * control * cstypes::tick_interval;
			auto new_speed = std::fmaxf( sim_speed - drop, 0.0f );
			auto accel = sv_accelerate;

			if ( shared_ctx.is_scoped )
			{
				const auto weapon_ratio = std::fminf( 1.0f, shared_ctx.weapon_max_speed / 250.0f );
				const auto scoped_max = std::fmaxf( 250.0f, max_move_speed ) * weapon_ratio * 0.52f;

				if ( new_speed > scoped_max - 5.0f )
				{
					const auto t = 1.0f - std::fmaxf( 0.0f, new_speed - ( scoped_max - 5.0f ) ) / std::fmaxf( 0.01f, 5.0f );
					accel *= std::clamp( t, 0.0f, 1.0f );
				}
			}

			const auto accel_speed = std::fminf( accel * shared_ctx.weapon_max_speed * surface_friction * cstypes::tick_interval, new_speed );
			new_speed = std::fmaxf( new_speed - accel_speed, 0.0f );

			if ( new_speed > 0.0f )
			{
				sim_vel *= ( new_speed / sim_speed );
			}
			else
			{
				sim_vel = {};
				break;
			}
		}

		const auto avg_vel = ( prestate.networked_velocity + sim_vel ) * 0.5f;
		const auto stop_ticks = g_shared.calculate_stop_ticks( prestate.networked_velocity, shared_ctx.weapon_max_speed, local.pawn );
		const auto stop_time = static_cast< float >( stop_ticks ) * cstypes::tick_interval;

		return stop_prediction
		{
			.eye =
			{
				current_eye.x + avg_vel.x * stop_time,
				current_eye.y + avg_vel.y * stop_time,
				current_eye.z
			},
			.inaccuracy = g_shared.get_inaccuracy_at_velocity( local.pawn, sim_vel )
		};
	}

	std::vector<rage::candidate> rage::gather_candidates( const systems::local::snapshot& local, float max_distance_sq ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto players = systems::g_entities.get_by_type( systems::entities::type::player );

		std::vector<candidate> out;
		out.reserve( players.size( ) );

		// The extrapolated records are consumed by the render thread
		// (extrapolation display); the vector is cleared and re-populated
		// here on the create_move thread, so the whole write side shares
		// the mutex with the render-side copy accessor.
		{
			std::unique_lock lock( const_cast<rage*>( this )->m_extrapolated_mtx );
			const_cast<rage*>( this )->m_extrapolated_records.clear( );
			const_cast<rage*>( this )->m_extrapolated_records.reserve( players.size( ) );
		}

		for ( const auto& p : players )
		{
			if ( !p.ptr || p.ptr == local.controller )
			{
				continue;
			}

			if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );

			if ( !pawn || pawn == local.pawn )
			{
				continue;
			}

			const auto team = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
			{
				continue;
			}

			const auto health = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( health <= 0 )
			{
				continue;
			}

			if ( memory::read<bool>( pawn + SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_hash ) ) )
			{
				continue;
			}

			auto records = g_shared.lc( ).get_valid_records( pawn );

			// Extrapolation is generated on every tick, not only when no
			// record exists: the server resolves the attack against the
			// target's current pose, so the extrapolated (predicted) pose is
			// the one closest to what the server sees - it deserves priority
			// over the newest record and far over rewind/backtrack poses.
			if ( max_distance_sq > 0.0f )
			{
				const auto& origin = systems::g_prediction.pre( ).origin;
				const auto scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				if ( scene_node )
				{
					const auto pawn_origin = memory::read<math::vector3>( scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
					const auto dx = pawn_origin.x - origin.x;
					const auto dy = pawn_origin.y - origin.y;
					const auto dz = pawn_origin.z - origin.z;

					if ( dx * dx + dy * dy + dz * dz > max_distance_sq )
					{
						continue;
					}
				}
			}

			auto extrap = g_shared.lc( ).extrapolate( pawn );
			if ( extrap.has_value( ) )
			{
				{
					std::unique_lock lock( const_cast<rage*>( this )->m_extrapolated_mtx );
					const_cast<rage*>( this )->m_extrapolated_records.push_back( std::move( *extrap ) );
				}
				records.push_back( &const_cast<rage*>( this )->m_extrapolated_records.back( ) );
			}

			// No usable pose: the target has no valid records and no
			// extrapolation could be produced (stationary target, the
			// feature disabled, air-only gaps, etc.). records.front() below
			// would dereference an empty vector - skip the player instead.
			if ( records.empty( ) )
			{
				continue;
			}

			if ( max_distance_sq > 0.0f )
			{
				const auto& origin = systems::g_prediction.pre( ).origin;
				const auto delta_front = records.front( )->origin - origin;
				auto closest_sq = delta_front.x * delta_front.x + delta_front.y * delta_front.y + delta_front.z * delta_front.z;

				if ( records.size( ) > 1 )
				{
					const auto delta_back = records.back( )->origin - origin;
					const auto back_sq = delta_back.x * delta_back.x + delta_back.y * delta_back.y + delta_back.z * delta_back.z;
					closest_sq = std::min( closest_sq, back_sq );
				}

				if ( closest_sq > max_distance_sq )
				{
					continue;
				}
			}

			candidate c{};
			c.pawn = pawn;
			c.health = health;
			c.armor = memory::read<int>( pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );
			c.velocity = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );

			// Hitboxes are a per-entity attribute; query once per candidate and
			// reuse it for every record and penetration pass instead of
			// re-reading the set on each (candidate, record) pair.
			{
				const auto scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				if ( scene_node )
				{
					c.hitboxes = systems::g_hitboxes.query( scene_node );
				}
			}

			const auto pick_record_indices = [ &records, this ]( std::array<int, k_max_scan_records>& out_indices ) -> int
				{
					const auto count = records.size( );
					if ( count == 0 )
					{
						return 0;
					}

					auto picked{ 0 };
					const auto add_index = [ & ]( int idx )
						{
							if ( picked >= k_max_scan_records )
							{
								return;
							}

							for ( auto i = 0; i < picked; ++i )
							{
								if ( out_indices[ i ] == idx )
								{
									return;
								}
							}

							out_indices[ picked++ ] = idx;
						};

					// Backtrack switch: with it disabled only the extrapolated
					// prediction and the newest live record are attackable -
					// no rewound poses. With it enabled, near-live records up
					// to the configured max_backtrack_ticks are scanned.
					const auto& lg = settings::g_combat.m_lagcomp;
					const auto backtrack_enabled = lg.backtrack.value;
					const auto max_backtrack_ticks = backtrack_enabled
						? std::clamp( lg.max_backtrack_ticks.value, 1, static_cast< int >( rage::k_max_lagcomp_records ) )
						: 0;

					// Double tap alignment: every attack claims the weapon
					// ready tick on the input history and the server rewinds
					// the TARGET to that tick, so the pose matching the
					// claim is the only one the server resolves against.
					// Give it scan priority over the live/extrapolated poses.
					const auto dt_active = settings::g_combat.m_ragebot.m_double_tap.enabled.value;
					if ( dt_active )
					{
						const auto claim_tick = const_cast<rage*>( this )->m_double_tap.claimed_tick( );
						auto best_idx{ -1 };
						auto best_diff = std::numeric_limits<int>::max( );

						for ( auto i = static_cast< int >( count ) - 1; i >= 0; --i )
						{
							const auto diff = std::abs( records[ static_cast< std::size_t >( i ) ]->tick - claim_tick );
							if ( diff < best_diff )
							{
								best_diff = diff;
								best_idx = i;
							}
						}

						if ( best_idx >= 0 )
						{
							add_index( best_idx );
						}
					}

					// Priority order: extrapolated (predicted current) pose
					// first - it is the closest match to what the server
					// resolves against for a moving target, and scanning it
					// first lets a direct hit on it fire immediately
					// instead of waiting on the (lagged) live body. The
					// newest live record always comes next; rewound records
					// are added afterwards up to the configured tick window.
					for ( auto i = static_cast< int >( count ) - 1; i >= 0; --i )
					{
						if ( records[ static_cast< std::size_t >( i ) ]->extrapolated )
						{
							add_index( i );
							break;
						}
					}

					add_index( 0 );

					if ( backtrack_enabled )
					{
						const auto current_tick = g_shared.ctx( ).current_tick;
						for ( auto i = 1; i < static_cast< int >( count ); ++i )
						{
							if ( ( current_tick - records[ static_cast< std::size_t >( i ) ]->tick ) > max_backtrack_ticks )
							{
								break;
							}

							add_index( i );
							if ( picked >= k_max_scan_records )
							{
								break;
							}
						}
					}

					return picked;
				};

			std::array<int, k_max_scan_records> record_indices{};
			const auto picked_count = pick_record_indices( record_indices );

			for ( auto i = 0; i < picked_count; ++i )
			{
				c.records[ i ] = records[ static_cast< std::size_t >( record_indices[ i ] ) ];
			}

			c.record_count = picked_count;

			if ( shared_ctx.weapon_type >= cstypes::weapon_type::pistol && shared_ctx.weapon_type <= cstypes::weapon_type::lmg )
			{
				const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
				c.min_damage = this->get_min_damage( config, health, config.min_damage_override.value );
			}

			out.push_back( c );
		}

		// Force-lethal + double tap kill efficiency: with a single visible
		// target the pair kills it together - each bullet only needs half
		// its HP (first shot softens, second finishes), which opens up body
		// shots instead of waiting for a headshot-only lethal point and
		// speeds up the kill. The first bullet gets it right away (no
		// "stable" window wait). Multiple targets keep the lethal floor per
		// bullet (two bullets, two kills); when the first target dies on
		// bullet one the follow-up softens the second - the cost is only an
		// extra bullet, never a wasted pair.
		if ( settings::g_combat.m_ragebot.m_double_tap.enabled.value && out.size( ) == 1 )
		{
			const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
			if ( !config.min_damage_override.value && config.min_damage >= 101 )
			{
				auto& cand = out.front( );
				cand.min_damage = std::max( 1.0f, std::ceil( static_cast< float >( cand.health ) * 0.5f ) );
			}
		}

		return out;
	}

	bool rage::run_gun( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local, bool allow_fire )
	{
		diag::set_exception_phase( "rage: run_gun" );

		if ( !settings::g_combat.m_ragebot.enabled )
		{
			return false;
		}

		auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );

		// Signature-drift sentinel: an unscoped weapon can never have a zero
		// spread cone. If the accuracy getters return ~0 while unscoped, the
		// engine calls (get_inaccuracy / get_spread / weapon_update_accuracy)
		// are likely drifted after a game update and every hitchance will be
		// a fake 100%.
		if ( !ctx.is_scoped && shared_ctx.inaccuracy <= 0.0001f && ctx.predicted_inaccuracy <= 0.0001f )
		{
			static bool warned{};
			if ( !warned )
			{
				warned = true;
				diag::writef( diag::level::info, "[hitchance] WARNING: unscoped weapon reports zero inaccuracy - engine accuracy getters likely drifted, hitchance is unreliable" );
			}
		}

		auto candidates = this->gather_candidates( local );

		{
			std::lock_guard lock( m_debug_mtx );
			m_debug_points.clear( );
		}

		if ( candidates.empty( ) )
		{
			return false;
		}

		auto eye_candidates = g_shared.sh( ).get_candidates( );
		if ( eye_candidates.count == 0 )
		{
			eye_candidates.entries[ 0 ].position = g_shared.get_shoot_position( );
			eye_candidates.entries[ 0 ].is_uninterpolated = true;
			eye_candidates.count = 1;
		}

		const auto scan_from_eye_candidates = [ & ]( const math::vector3& eye_offset, float inaccuracy )
		{
			std::vector<scan_hit> hits_out;

			for ( auto i = 0; i < eye_candidates.count; ++i )
			{
				const auto eye = eye_candidates.entries[ i ].position + eye_offset;
				auto hits = this->scan_players( eye, inaccuracy, ctx, candidates, local );
				auto found_direct{ false };

				for ( auto& hit : hits )
				{
					auto source_eye = eye_candidates.entries[ i ];
					source_eye.position = eye;
					hit.source_eye = source_eye;
					found_direct = found_direct || !hit.penetrated;
					hits_out.push_back( std::move( hit ) );
				}

				if ( found_direct )
				{
					break;
				}
			}

			return hits_out;
		};

		if ( config.no_spread.value )
		{
			shared_ctx.inaccuracy = g_shared.get_inaccuracy( false );
			auto all_hits = scan_from_eye_candidates( {}, shared_ctx.inaccuracy );

			if ( all_hits.empty( ) )
			{
				return false;
			}

			const auto best = this->select_best( ctx, all_hits, shared_ctx.inaccuracy );
			if ( !best.valid )
			{
				this->update_auto_scope( cmd, false );
				return false;
			}

			this->m_double_tap.update_aim( best.hit.aim_angle - g_shared.get_aim_punch( local.pawn ) );

			this->update_auto_scope( cmd, true );

			// Seed mode carries only a small, legal spread correction - fire
			// it from a stop like the regular path, otherwise the moving
			// spread floor can exceed the correction and the shot misses.
			if ( config.no_spread_mode.value == settings::combat::ragebot::weapon_group::no_spread_mode::seed
				&& this->should_stop_movement( ctx ) )
			{
				this->m_should_stop = true;
				return false;
			}

			if ( !allow_fire )
			{
				return true;
			}

		diag::set_exception_phase( "rage: fire_gun" );
		this->fire_gun( cmd, best, false, best.hit.source_eye.position, local, false );
		return true;
	}

		const auto primary_eye = eye_candidates.entries[ 0 ].position;
		const auto& prestate = systems::g_prediction.pre( );

		// Current-shot selection is always based on current engine shoot-history.
		auto current_hits = scan_from_eye_candidates( {}, ctx.predicted_inaccuracy );
		const auto best = this->select_best( ctx, current_hits, ctx.predicted_inaccuracy );
		this->m_double_tap.update_aim( best.hit.aim_angle - g_shared.get_aim_punch( local.pawn ) );

		this->update_auto_scope( cmd, best.valid );

		const auto needed_hc = config.hitchance_override.value ? static_cast< float >( config.hitchance_override_value ) / 100.0f : static_cast< float >( config.hitchance ) / 100.0f;
		const auto duckpeek_active = settings::g_combat.m_duckpeek.enabled.value && ctx.on_ground;
		const auto is_ducked = ( prestate.flags & cstypes::entity_flags::ducking ) != 0;

		const auto standing_inaccuracy = duckpeek_active ? this->get_standing_inaccuracy( local, ctx ) : ctx.predicted_inaccuracy;
		const auto standing_hc = best.valid
			? ( duckpeek_active ? this->evaluate_hitchance( best.hit, ctx, standing_inaccuracy ) : best.hitchance )
			: 0.0f;

		// 1% tolerance on both gates: the sampled hitchance carries +/- a
		// couple of percent of noise, so a shot sitting right at the
		// configured threshold used to flicker between "fire" and "wait"
		// every tick - the tolerance makes it fire consistently. Head
		// shots get a wider tolerance: the small head capsule can never
		// reach the body-level sampled hit rate, and a visible head should
		// be fired on rather than stared at - 25% when the head is the
		// only lethal point (full-hp target with a 100/force-lethal
		// damage floor), 10% otherwise.
		const auto best_can_kill = best.valid && best.hit.damage >= static_cast< float >( best.hit.health );
		const auto best_is_head = best.valid && systems::g_hitboxes.hitgroup_from_hitbox( best.hit.hitbox_index ) == 1;
		const auto head_tolerance = best_is_head ? ( best_can_kill ? 0.25f : 0.10f ) : 0.0f;
		// Double tap fires a tight pair: a hitchance sitting a few percent
		// under the threshold would otherwise stall the alternating beat
		// and delay the second bullet. A small 3% relaxation keeps the
		// fire decision on every beat while the aligned pose still puts
		// the bullet inside the hitbox.
		const auto dt_hc_boost = settings::g_combat.m_ragebot.m_double_tap.enabled.value ? 0.03f : 0.0f;
		const auto accurate = best.valid && standing_hc >= needed_hc - 0.01f - head_tolerance - dt_hc_boost;
		const auto max_acc = g_shared.is_max_accuracy( standing_inaccuracy );
		// Force shot: by default only at max accuracy; when a minimum force
		// hitchance is configured, fire as soon as the standing hitchance
		// meets it instead.
		const auto force_hc = std::clamp( ctx.on_ground ? config.force_hitchance.value : config.force_hitchance_air.value, 0, 100 );
		const auto force_eligible = max_acc || ( force_hc > 0 && standing_hc * 100.0f >= static_cast< float >( force_hc ) - 0.5f - head_tolerance * 100.0f );
		const auto force = best.valid && ( ctx.on_ground ? ( config.force_shot.value && force_eligible ) : ( config.force_shot_air.value && force_eligible ) );
		auto shot_viable = accurate || force;

		// Autostop planning is independent from firing. Ground movement can use a
		// predicted stopped eye; airborne stopping keeps the current target context.
		//
		// Planning runs on every moving frame in both modes: the "fire stop"
		// mode arms once the stopped-position scan finds a viable shot (the
		// moving hitchance may look sufficient at e.g. 13% while the real
		// moving spread misses - "reason=spread"); the "early stop" mode arms
		// earlier, at a fraction of the required hitchance. The speed gate
		// below then holds the trigger until the player is actually
		// stationary, so the bullet always leaves from the stopped spread.
		if ( !shot_viable && this->should_stop_movement( ctx ) )
		{
			const auto stop = this->predict_stop( ctx, primary_eye, local );
			if ( stop )
			{
				const auto future_offset = stop->eye - primary_eye;
				auto planned_hits = scan_from_eye_candidates( future_offset, stop->inaccuracy );
				const auto planned = this->select_best( ctx, planned_hits, stop->inaccuracy );
				this->m_should_stop = planned.valid;
			}
			else
			{
				this->m_should_stop = best.valid;
			}

			// Air stop: while airborne the deceleration is executed by the
			// airstrafe shift air-stop - auto-hold shift so it engages
			// without the player touching the key.
			if ( this->m_should_stop && !ctx.on_ground && settings::g_combat.m_autos.air_stop.value )
			{
				cmd->buttons.value |= cstypes::command_buttons::in_sprint;
				cmd->buttons.value_changed |= cstypes::command_buttons::in_sprint;
			}

			// Full stop for snipers (AWP / auto-snipers): the local engine
			// does not consume the usercmd sprint button (walk), so emulate
			// a hard walk by clamping m_flMaxspeed to nearly zero - the
			// engine cuts the velocity every tick and no movement is allowed
			// until the shot fires. Restored by the engine on the next tick.
			if ( this->m_should_stop && ctx.on_ground )
			{
				const auto is_sniper = shared_ctx.item_def_idx == cstypes::item_definition_index::weapon_awp
					|| shared_ctx.item_def_idx == cstypes::item_definition_index::weapon_g3sg1
					|| shared_ctx.item_def_idx == cstypes::item_definition_index::weapon_scar_20;
				if ( is_sniper )
				{
					const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
					if ( movement_services )
					{
						memory::write<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ), 5.0f );
					}
				}
			}
		}



		if ( !best.valid )
		{
			return false;
		}

		if ( duckpeek_active && allow_fire )
		{
			if ( shot_viable )
			{
				this->m_release_duck_for_shot = true;
			}
			else if ( !this->m_duckpeek_reduck )
			{
				this->m_release_duck_for_shot = false;
			}
		}

		auto ready_to_fire = shot_viable;
		if ( duckpeek_active )
		{
			if ( is_ducked )
			{
				ready_to_fire = false;
			}
			else
			{
				ready_to_fire = ready_to_fire && this->m_release_duck_for_shot;
			}
		}

		if ( ready_to_fire && allow_fire )
		{
			// The autostop (misc::autostop / airstrafe wants_stop) decelerates
			// while the scan re-evaluates: once the stopped hitchance passes,
			// shot_viable goes true and the shot fires - matching the old
			// (known-good) behaviour of firing as soon as the stop is viable.
			diag::set_exception_phase( "rage: fire_gun" );
			this->fire_gun( cmd, best, !accurate && force, best.hit.source_eye.position, local, false );

			if ( duckpeek_active )
			{
				this->m_duckpeek_reduck = true;
				this->m_release_duck_for_shot = false;
			}
		}
		else
		{
		}

		// best.valid held through the viability gate above - a target exists
		// this tick (it may simply not be ready to fire yet).
		return true;
	}



	void rage::auto_revolver( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !settings::g_combat.m_ragebot.enabled )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		// Reference the known-good legacy behaviour: full can_shoot gate
		// (next-primary included), fixed 13-tick cock hold, and the release
		// edge expressed before the fire pass - the revolver fires on the
		// button release.
		if ( !g_shared.can_shoot( cmd, local.controller ) )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		if ( !settings::g_combat.m_autos.revolver.value )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		constexpr auto cock_ticks{ 13 };
		if ( this->m_revolver_cock_ticks >= cock_ticks )
		{
			// End the held cycle. Target selection adds attack back on this
			// command only when the revolver should actually fire.
			cmd->buttons.value &= ~cstypes::command_buttons::in_attack;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
			cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_attack;
			cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
			this->m_revolver_cock_ticks = 0;
			this->run_gun( cmd, ctx, local );
			return;
		}

		// Keep target and hitchance planning active throughout the cock cycle.
		// Autostop consumes this command's decision on the following command.
		this->run_gun( cmd, ctx, local, false );

		cmd->buttons.value |= cstypes::command_buttons::in_attack;
		cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
		cmd->buttons.value_scroll |= cstypes::command_buttons::in_attack;

		const auto history_index = cmd->csgo_user_cmd.input_history_size( ) - 1;
		if ( history_index >= 0 )
		{
			cmd->csgo_user_cmd.set_attack1_start_history_index( history_index );
		}

		++this->m_revolver_cock_ticks;
	}

	void rage::fire_gun( systems::input::usercmd* cmd, const target& tgt, bool was_forced, const math::vector3& shoot_eye, const systems::local::snapshot& local, bool subtick_attack )
	{

		if ( !tgt.hit.record || !tgt.hit.record->valid )
		{
			return;
		}
		if ( tgt.hit.pawn )
		{
			const auto live_health = memory::read<int>( tgt.hit.pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( live_health <= 0 )
			{
				return;
			}
		}
		else
		{
			return;
		}

		this->m_firing_this_tick = true;

		const auto& shared_ctx = g_shared.ctx( );
		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			this->m_firing_this_tick = false;
			return;
		}

		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
		const auto aim_punch = g_shared.get_aim_punch( local.pawn );

		// The attack is stamped with the LOCAL interpolated shoot tick
		// (old build behaviour). Official servers run m_flSimulationTime on
		// a fixed time-base offset from the client tick (~24t in every
		// record regardless of freshness), so stamping the record tick
		// rewound the ATTACKER too ("shoot position mismatch" whiffs).
		// The record skeleton is always read fresh from the bone cache, so
		// the newest record IS the current pose and the server resolves it
		// at the local tick - no velocity*latency lead on top (it aimed
		// ahead of the resolved pose and whiffed behind moving targets;
		// the extrapolated record already carries the predicted lead).
		auto aim_position = tgt.hit.position;
		// Aligned with the previous build: no-spread aims from the exact
		// trace position, the regular path keeps the scan-selected angle.
		//
		// High-speed targets: the scan pose is read at scan time, but the
		// server resolves the shot against the target's pose at the attack
		// tick + latency - a fast target keeps moving during that window
		// and every no-spread bullet lands behind it (no spread cone to
		// absorb the miss). Lead the aim position by the target's velocity
		// over the network latency (plus one tick of server processing),
		// like the extrapolation does. The extrapolated record already
		// carries the predicted lead, so never double-compensate it.
		if ( config.no_spread.value && tgt.hit.record && !tgt.hit.record->extrapolated )
		{
			const auto target_velocity = memory::read<math::vector3>( tgt.hit.pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
			if ( target_velocity.length_2d( ) > 1.0f )
			{
				auto lead_time{ cstypes::tick_interval };
				const auto net_channel = memory::call<std::uintptr_t>( PATTERN( patterns::get_net_channel ), 0, 0 );
				if ( net_channel )
				{
					const auto latency = memory::call_vfunc<float>( net_channel, 10, 0 );
					if ( std::isfinite( latency ) && latency > 0.0f && latency < 1.0f )
					{
						lead_time += latency;
					}
				}

				aim_position += target_velocity * std::min( lead_time, 0.25f );
			}
		}

		auto aim_angle = config.no_spread.value
			? math::helpers::calculate_angle( shoot_eye, aim_position )
			: tgt.hit.aim_angle;

		// The attack stamp (local interpolated shoot tick): the server
		// rewinds the TARGET to this tick, so the true backtrack depth is
		// stamp_tick - record->tick (both server-side frames). Using the
		// client current_tick instead adds the fixed client/server time-base
		// offset (~24t) and makes every miss log show a bogus depth.
		auto stamp_tick = tick_base;
		auto stamp_frac{ 0.0f };
		if ( !tgt.hit.source_eye.is_uninterpolated )
		{
			auto tick_add = [ ]( int t, float f, int dt, float df )
				{
					f += df;
					auto carry = static_cast< int >( std::floor( f ) );
					f -= static_cast< float >( carry );
					return std::pair{ t + dt + carry, f };
				};

			std::tie( stamp_tick, stamp_frac ) = tick_add( tgt.hit.source_eye.player_tick, tgt.hit.source_eye.player_frac, tgt.hit.source_eye.lerp_ticks_int, tgt.hit.source_eye.lerp_ticks_frac );
		}

		const auto seed_mode = config.no_spread_mode.value == settings::combat::ragebot::weapon_group::no_spread_mode::seed;

		// Seed mode is independent of the no-spread switch: it never
		// touches the view angles (zero compensation), it only verifies
		// the server-side spread seed lands on the hitbox naturally. That
		// is fully legal on servers that reject no-spread, so the mode
		// stays usable with the switch off - in that case a scattered
		// seed simply drops the shot (no compensation fallback, since
		// corrected angles are not allowed there).
		if ( config.no_spread.value || seed_mode )
		{
			// The spread seed uses the tick the server consumes the shot on
			// (the attack stamp above); the compensation must match it.
			//
			// Double tap rewrites the entry claim to the weapon-ready tick
			// (m_next_attack), so the server derives its seed from THAT
			// tick - a correction computed against the local interpolated
			// stamp would target the wrong seed bucket, fail its own
			// self-consistency check and drop the shot. Use the claimed
			// tick when double tap is active so the compensation always
			// matches what the server resolves.
			auto claim_tick = stamp_tick;
			if ( settings::g_combat.m_ragebot.m_double_tap.enabled.value
				&& shared_ctx.item_def_idx != cstypes::item_definition_index::weapon_r8_revolver )
			{
				const auto dt_claim = this->m_double_tap.claimed_tick( );
				// Same sanity bound as the DT rhythm (claim_valid): a
				// garbage claim must not feed the seed math.
				if ( dt_claim >= 0 && dt_claim <= tick_base + 256 )
				{
					claim_tick = dt_claim;
				}
			}

			// on_fired stamps the entries with this exact tick (the DT
			// rhythm would otherwise overwrite them with the claim or 0
			// and desync the server-side seed from the correction).
			this->m_double_tap.set_no_spread_claim_tick( claim_tick );

			// Seed mode (legit-style, non-forced): no angle compensation at
			// all - the view keeps the plain aim at the target. Instead,
			// mirror the legit triggerbot seed constraint: predict the
			// server-side spread seed for this shot and only fire when the
			// natural bullet direction still lands on the target's hitbox.
			// When the seed scatters the bullet off the body, drop the shot
			// and let the next tick re-roll the seed.
			//
			// On servers that block client/server seed sync the predicted
			// seed never matches the server's, so the verification would
			// gate shots on a random check. The shot-result tracker
			// (shared::note_seed_shot) detects the collapsed hit rate and
			// flips seed_synced() false - the mode then degrades to the
			// plain ragebot instead of dropping shots.
			if ( seed_mode && g_shared.seed_synced( ) )
			{
				math::vector3 forward{}, left{}, up{};
				math::helpers::angle_vectors_left( aim_angle, &forward, &left, &up );

				const auto seed = g_shared.get_spread_seed( aim_angle, claim_tick );
				const auto spread = g_shared.calculate_spread(
					static_cast< int >( seed ),
					shared_ctx.inaccuracy,
					shared_ctx.spread,
					shared_ctx.recoil_index,
					shared_ctx.item_def_idx,
					shared_ctx.num_bullets );

				const auto bullet_dir = ( forward + left * spread.x + up * spread.y ).normalized( );

				if ( !tgt.hit.record || tgt.hit.bone_index < 0 || tgt.hit.bone_index >= 28 )
				{
					this->m_firing_this_tick = false;
					return;
				}

				const auto& bone = tgt.hit.record->bones[ tgt.hit.bone_index ];
				const auto& hb = tgt.hit.hitbox;
				const auto capsule_start = bone.rotation.rotate_vector( hb.mins ) + bone.position;
				const auto capsule_end = bone.rotation.rotate_vector( hb.maxs ) + bone.position;
				const auto radius = hb.radius > 0.0f ? hb.radius * 0.9f : 1.8f;

				auto fraction{ 1.0f };
				if ( !g_shared.ray_vs_capsule( shoot_eye, bullet_dir * shared_ctx.range, capsule_start, capsule_end, radius, fraction ) )
				{
					// The seed scattered off the body. Waiting for a lucky
					// seed is weak (moving targets almost never get one);
					// when no-spread is enabled fall back to a focused
					// angle compensation for this tick so the shot count
					// stays up. Without no-spread (servers that reject
					// corrected angles) drop the shot instead.
					if ( config.no_spread.value )
					{
						const auto corrected = g_shared.find_spread_correction( aim_angle, claim_tick );
						if ( corrected.x == 0.0f && corrected.y == 0.0f && corrected.z == 0.0f )
						{
							this->m_firing_this_tick = false;
							return;
						}

						aim_angle = corrected;
					}
					else
					{
						this->m_firing_this_tick = false;
						return;
					}
				}
			}
			else
			{
				// Forced no-spread (rewritten): the correction is computed
				// DIRECTLY from the shot's own seed - pitch raised by the
				// spread cone and rolled opposite the spread vector, so
				// "corrected angle + spread" lands exactly on the aim. The
				// seed tick must match what the SERVER consumes: double tap
				// rewrites the entry claim to the weapon-ready tick
				// (m_claimed_for_aim), so the server derives its seed from
				// THAT tick - correcting against the local interpolated
				// stamp would target a different seed and every bullet
				// would drift. Without DT the claim is the stamp itself.
				// The result must stay inside the same seed bucket as the
				// original angle (otherwise the server, resolving through
				// the corrected entry angle, would apply a different seed's
				// spread); when it does not, fall back to the full
				// 720-probe bucket search.
				const auto seed = g_shared.get_spread_seed( aim_angle, claim_tick );
				if ( seed == 0 )
				{
					this->m_firing_this_tick = false;
					return;
				}

				const auto spread = g_shared.calculate_spread(
					static_cast< int >( seed ),
					shared_ctx.inaccuracy,
					shared_ctx.spread,
					shared_ctx.recoil_index,
					shared_ctx.item_def_idx,
					shared_ctx.num_bullets );

				auto corrected = aim_angle;
				corrected.x += math::helpers::rad_to_deg( std::atan( std::sqrt( spread.x * spread.x + spread.y * spread.y ) ) );
				corrected.z = -math::helpers::rad_to_deg( std::atan2( spread.x, spread.y ) );

				if ( g_shared.get_spread_seed( corrected, claim_tick ) != seed )
				{
					// Different bucket: fall back to the full sweep.
					corrected = g_shared.find_spread_correction( aim_angle, claim_tick );
					if ( corrected.x == 0.0f && corrected.y == 0.0f && corrected.z == 0.0f )
					{
						this->m_firing_this_tick = false;
						return;
					}
				}

				aim_angle = corrected;
			}

			if ( settings::g_misc.m_impacts.console_log.value && shared_ctx.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver )
			{
				logging::console::print(
					xs( "[r8] aim ({:.2f},{:.2f}) -> final ({:.2f},{:.2f}) tick {}" ),
					tgt.hit.aim_angle.x, tgt.hit.aim_angle.y,
					aim_angle.x, aim_angle.y,
					stamp_tick
				);
			}
		}

		g_shared.last_shoot_tick( ) = tick_base;

		if ( settings::g_misc.m_impacts.console_log.value )
		{
			const auto hitgroup_name = systems::g_hitboxes.hitgroup_to_name( tgt.hit.hitgroup );
			const auto bt_delta = std::max( stamp_tick - tgt.hit.record->tick, 0 );
				logging::console::print_severity(
				2,
				xs( "[velocity] 射击 {}，生命 {}，伤害 {:.0f}（{}），命中率 {:.0f}%，回溯 {}t{}" ),
				hitgroup_name,
				tgt.hit.health,
				tgt.hit.damage,
				hitgroup_name,
				tgt.hitchance * 100.0f,
				bt_delta,
				was_forced ? xs( "（强制）" ) : ""
			);
		}

		features::misc::g_impacts.on_boom(
			{
				.victim_pawn = tgt.hit.pawn,
				.hitgroup = tgt.hit.hitgroup,
				.damage = tgt.hit.damage,
				.hitchance = tgt.hitchance,
				.inaccuracy = shared_ctx.inaccuracy,
				.spread = shared_ctx.spread,
				.aim_angle = aim_angle,
				.aim_position = tgt.hit.position,
				.shoot_position = shoot_eye,
				.tick = tgt.hit.record->tick,
				.stamp_tick = stamp_tick,
				.skeleton = g_shared.lc( ).get_skeleton( *tgt.hit.record ),
				.forced = was_forced,
				.extrapolated = tgt.hit.record->extrapolated,
				.seed_mode = seed_mode,
				.dt = settings::g_combat.m_ragebot.m_double_tap.enabled.value,
			} );
		features::esp::player::g_chams.os ().push (tgt.hit.pawn);
		const auto record_time = cstypes::tick_fraction::from_value( tgt.hit.record->simulation_time / cstypes::tick_interval );
		const auto history_size = cmd->csgo_user_cmd.input_history_size( );
		for ( auto i = 0; i < history_size; ++i )
		{
			const auto entry = cmd->csgo_user_cmd.mutable_input_history( i );
			if ( !entry )
			{
				continue;
			}

			if ( const auto angles = entry->mutable_view_angles( ) )
			{
				angles->set_x( aim_angle.x - aim_punch.x );
				angles->set_y( aim_angle.y - aim_punch.y );

				if ( config.no_spread.value )
				{
					angles->set_z( aim_angle.z );
				}
			}

			entry->set_render_tick_count( record_time.tick + 1 );
			entry->set_render_tick_fraction( 0.0f );

			// Old-build behaviour: the attack timestamp is the LOCAL
			// interpolated shoot tick. Official servers run simulation
			// times on a fixed offset from the client tick, so stamping
			// the record tick rewound the ATTACKER as well (the "shoot
			// position mismatch" whiffs). With the local stamp the server
			// resolves the shot at the current tick against the current
			// pose - which is exactly what the newest record's fresh
			// skeleton represents. (stamp_tick above was computed the same
			// way; only the fractional part differs per entry.)
			if ( !tgt.hit.source_eye.is_uninterpolated )
			{
				entry->set_player_tick_count( stamp_tick );
				entry->set_player_tick_fraction( stamp_frac );
			}

			if ( entry->has_sv_interp0( ) )
			{
				const auto interp = entry->mutable_sv_interp0( );
				interp->set_src_tick( -1 );
				interp->set_dst_tick( -1 );
				interp->set_frac( 0.0f );
			}

			if ( entry->has_sv_interp1( ) )
			{
				const auto interp = entry->mutable_sv_interp1( );
				interp->set_src_tick( -1 );
				interp->set_dst_tick( -1 );
				interp->set_frac( 0.0f );
			}

			if ( entry->has_cl_interp( ) )
			{
				const auto interp = entry->mutable_cl_interp( );
				interp->set_frac( 0.0f );
			}
		}

		if ( !subtick_attack )
		{
			// Attack expression (button / edge / attack index) is unified
			// in double_tap::on_fired when double tap is enabled: the
			// ragebot decision carries only the aim angles written above,
			// and the DT rhythm - alternation + claimed tick - is the
			// single source of attack edges for both manual and ragebot
			// firing. Any expression here would double up with the rhythm
			// and hand the server a per-tick edge stream (auto-fire
			// resolution, claimed ticks ignored, pair collapses).
			//
			// NO-SPREAD is the exception: the corrected angles only take
			// effect when the server resolves the shot through the attack
			// INDEX (which points at the corrected entry). With the index
			// invalidated the server falls back to the wire base viewangles
			// (uncompensated) and the bullet flies off - the no-spread
			// feature looked completely broken ("all shots whiff"). The
			// previous build expressed no-spread shots with button + index
			// and it worked, so restore that expression for it.
			if ( !settings::g_combat.m_ragebot.m_double_tap.enabled.value
				|| config.no_spread.value )
			{
				cmd->buttons.value |= cstypes::command_buttons::in_attack;
				cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
				cmd->buttons.value_scroll |= cstypes::command_buttons::in_attack;

				if ( history_size > 0 )
				{
					cmd->csgo_user_cmd.set_attack1_start_history_index( history_size - 1 );
				}
			}
		}

		math::vector3 forward{};
		{
			if ( const auto angles = base->viewangles( ) )
			{
				math::helpers::angle_vectors_left( { angles->x( ), angles->y( ), angles->z( ) }, &forward );
			}
		}

		const auto punched_aim = math::vector3{ aim_angle.x - aim_punch.x, aim_angle.y - aim_punch.y, 0.0f };
		const auto facing_away = forward.dot( ( tgt.hit.record->origin - systems::g_prediction.pre( ).networked_origin ).normalized( ) ) < 0.707107f;

		// VAC live bypass: mirror the FVA view-angle spoofer - always send a
		// fixed hidden view (reversed heading) in the outgoing usercmd so the
		// wire angles never match the aim, while the local view stays on the
		// aim (silent). The reversed heading still faces the target, so the
		// bullet hits normally.
		const auto vac_bypass = settings::g_combat.m_ragebot.vac_bypass.value;

		auto command_aim = punched_aim;
		if ( !subtick_attack && ( vac_bypass || ( facing_away && settings::g_combat.m_antiaim.hide_shots.value ) ) )
		{
			// 7/29 VACNet hardening: the old over-the-horizon pitch (179.9)
			// is now rejected by server-side viewangle validation, so the
			// vac-bypass path uses an engine-valid inverted pitch instead
			// (the heading reversal alone hides the aim; the bullet still
			// travels through the input history angle).
			command_aim.x = vac_bypass ? -punched_aim.x : 179.9f;
			// VACNet parity: jitter the hidden heading slightly every shot
			// so the wire never carries a perfectly constant inverted
			// angle - a repeatable signature the AI detector can learn.
			command_aim.y = std::remainderf( punched_aim.y + 180.0f + ( vac_bypass ? random::floating( -0.35f, 0.35f ) : 0.0f ), 360.0f );
		}

		if ( const auto angles = base->mutable_viewangles( ) )
		{
			angles->set_x( command_aim.x );
			angles->set_y( command_aim.y );
		}

		if ( !config.silent.value && !vac_bypass )
		{
			systems::g_input.set_view_angles( punched_aim );
		}
	}



	bool rage::should_stop_movement( const aim_context& ctx ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto& prestate = systems::g_prediction.pre( );
		const auto velocity = prestate.networked_velocity;

		if ( shared_ctx.weapon_type == cstypes::weapon_type::sniper && !ctx.is_scoped )
		{
			return false;
		}

		if ( ctx.on_ground )
		{
			const auto speed_2d = velocity.length_2d( );
			if ( speed_2d <= 0.1f )
			{
				return false;
			}

			const auto inaccuracy_move = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyMove"_hash ) );
			const auto inaccuracy_stand = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyStand"_hash ) );

			return speed_2d * inaccuracy_move > inaccuracy_stand;
		}

		// Airborne stopping is sniper-only by default; the "air stop" setting
		// extends the same deceleration planning to every weapon. The actual
		// deceleration is executed by auto-holding shift while airborne, which
		// drives the airstrafe shift air-stop (see run_gun).
		if ( shared_ctx.weapon_type != cstypes::weapon_type::sniper )
		{
			return settings::g_combat.m_autos.air_stop.value;
		}

		if ( velocity.z > 140.0f )
		{
			return false;
		}

		const auto sv_gravity = CONVAR ("sv_gravity")->get<float>( );
		const auto sv_friction = CONVAR ("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR ("sv_stopspeed")->get<float>( );

		const auto inac_jump_initial = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpInitial"_hash ) );
		const auto inac_jump_apex = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );
		const auto shootable_threshold = inac_jump_apex + 0.001f;
		const auto early_threshold = inac_jump_initial * 0.55f + inac_jump_apex * 0.45f;
		const auto air_inaccuracy = g_shared.get_air_inaccuracy( velocity.z, inac_jump_initial, inac_jump_apex );

		if ( air_inaccuracy <= shootable_threshold || air_inaccuracy <= early_threshold )
		{
			return true;
		}

		auto sim_vz = velocity.z;
		auto ticks_to_shootable{ 0 };

		for ( auto i = 1; i <= 32; ++i )
		{
			sim_vz -= sv_gravity * cstypes::tick_interval;

			if ( g_shared.get_air_inaccuracy( sim_vz, inac_jump_initial, inac_jump_apex ) <= shootable_threshold )
			{
				ticks_to_shootable = i;
				break;
			}
		}

		if ( ticks_to_shootable == 0 )
		{
			return false;
		}

		const auto speed_2d = velocity.length_2d( );
		const auto max_speed = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_hash ) );
		const auto accurate_threshold = max_speed * 0.34f;

		if ( speed_2d <= accurate_threshold )
		{
			return true;
		}

		auto sim_speed = speed_2d;
		auto ticks_to_stop{ 32 };

		for ( auto i = 1; i <= 32; ++i )
		{
			const auto drop = std::fmaxf( sim_speed, sv_stopspeed ) * sv_friction * cstypes::tick_interval;
			sim_speed -= drop;

			if ( sim_speed <= accurate_threshold )
			{
				ticks_to_stop = i;
				break;
			}
		}

		return ticks_to_shootable <= ticks_to_stop + 2;
	}

	float rage::get_min_damage( const settings::combat::ragebot::weapon_group& config, int target_health, bool override_active ) const
	{
		if ( override_active )
		{
			return static_cast< float >( config.min_damage_override_value );
		}

		const auto base = static_cast< float >( config.min_damage );
		const auto hp = static_cast< float >( target_health );

		// Force-lethal (FL): slider at 101 (players cap at 100 hp) switches
		// the damage floor to the target's remaining health - any point
		// dealing >= hp fires, so the shot always kills a full-health
		// target. Capped at 101: no legal player has more than 100 hp, so
		// a higher requirement could never be met by the weapon.
		if ( base >= 101.0f )
		{
			return std::min( hp, 101.0f );
		}

		if ( hp < base )
		{
			return hp + 1.0f;
		}

		return base;
	}




	void rage::update_penetration_crosshair( const systems::local::snapshot& local )
	{
		const auto& cfg = settings::g_combat.m_penetration_crosshair;
		const auto& ctx = g_shared.ctx( );

		if ( !cfg.enabled.value || !ctx.valid || !local.is_alive || local.team < 2
			|| !local.pawn || !ctx.weapon
			|| ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg )
		{
			this->m_penetration_crosshair_state.store( penetration_crosshair_state::unavailable, std::memory_order_relaxed );
			return;
		}

		// get_shoot_position() can be zero before prediction has populated the
		// weapon-services shoot history. The crosshair needs the current eye now.
		const auto eye_pos = g_shared.get_eye_position( local.pawn );
		auto view_angles = systems::g_input.get_view_angles( );
		const auto aim_punch = g_shared.get_aim_punch( local.pawn );
		view_angles.x += aim_punch.x;
		view_angles.y += aim_punch.y;

		math::vector3 forward{};
		math::helpers::angle_vectors_left( view_angles, &forward );

		auto pen_damage{ 0.0f };
		const auto can_pen = g_shared.pen( ).can( eye_pos, forward, pen_damage, local );
		this->m_penetration_crosshair_state.store(
			can_pen ? penetration_crosshair_state::penetrable : penetration_crosshair_state::blocked,
			std::memory_order_relaxed );
	}

	void rage::draw_penetration_crosshair( xdraw::draw_list& draw_list ) const
	{
		const auto& cfg = settings::g_combat.m_penetration_crosshair;
		if ( !cfg.enabled.value )
		{
			return;
		}

		const auto state = this->m_penetration_crosshair_state.load( std::memory_order_relaxed );
		const auto local = systems::g_local.get( );
		if ( state == penetration_crosshair_state::unavailable || !local.is_alive || systems::g_local.is_in_cinematic( ) )
		{
			return;
		}

		const auto can_pen = state == penetration_crosshair_state::penetrable;

		const auto& fill = can_pen ? cfg.can_penetrate_fill : cfg.blocked_fill;
		const auto& outline = can_pen ? cfg.can_penetrate_outline : cfg.blocked_outline;
		const auto [ screen_w, screen_h ] = xdraw::viewport_size( );
		const auto cx = std::floorf( static_cast< float >( screen_w ) * 0.5f );
		const auto cy = std::floorf( static_cast< float >( screen_h ) * 0.5f );
		constexpr auto half_size{ 3.0f };
		constexpr auto outline_size{ 1.0f };

		if ( cfg.glow )
		{
			auto& glow = xdraw::get_glow( );
			const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( outline.value.a ) * cfg.glow_strength );
			const auto glow_col = xdraw::color{ outline.value.r, outline.value.g, outline.value.b, glow_a };

			glow.rect_filled( cx - half_size - outline_size, cy - half_size - outline_size,
				( half_size + outline_size ) * 2.0f, ( half_size + outline_size ) * 2.0f, glow_col );
		}

		draw_list.rect_filled( cx - half_size - outline_size, cy - half_size - outline_size,
			( half_size + outline_size ) * 2.0f, ( half_size + outline_size ) * 2.0f, outline );
		draw_list.rect_filled( cx - half_size, cy - half_size, half_size * 2.0f, half_size * 2.0f, fill );
	}

} // namespace features::combat
