#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <utilities/diag.hpp>
#include <utilities/random/random.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

// Double tap module: the per-tick charge/claim state machine, the manual
// and ragebot attack rhythm and the charge preview, split out of rage.cpp.
// The linkage to the rest of the ragebot is preserved through the rage
// instance (update_aim / claimed_tick / set_no_spread_claim_tick) and the
// impacts tracker (on_shot_committed).
namespace features::combat {


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
				auto attack_committed = !dt_active || attack_config.no_spread.value;

				if ( !attack_config.no_spread.value )
				{
					if ( auto* base = cmd->csgo_user_cmd.mutable_base( ) )
					{
						if ( auto* step = systems::g_input.acquire_subtick_step( base->mutable_subtick_moves( ) ) )
						{
							step->set_button( cstypes::command_buttons::in_attack );
							step->set_pressed( true );
							attack_committed = true;
						}
					}
				}

				if ( attack_committed )
				{
					features::misc::g_impacts.on_shot_committed( cmd->command_number );
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


} // namespace features::combat
