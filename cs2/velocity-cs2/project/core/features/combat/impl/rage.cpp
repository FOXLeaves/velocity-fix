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

		// Ammo-confirmation console output: one queued shot log per bullet
		// that actually left the clip (see flush_console_shot_logs).
		this->flush_console_shot_logs( );

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
				out.predicted_velocity = out.velocity;
				out.spread = g_shared.get_spread( );
				out.predicted_inaccuracy = g_shared.get_inaccuracy( true );
			} );

		// Diagnostic: standing still must never report a 7-degree cone.
		// A large inaccuracy while the predicted velocity is (near) zero
		// means the weapon accuracy state is being corrupted somewhere
		// (engine getter drift / snapshot restore damage).
		{
			const auto speed = out.predicted_velocity.length_2d( );
			if ( out.predicted_inaccuracy > 0.02f && speed < 10.0f )
			{
				static bool inaccuracy_warned{};
				if ( !inaccuracy_warned )
				{
					inaccuracy_warned = true;
					diag::writef( diag::level::info, "[accuracy] WARNING: inaccuracy {:.4f} while standing (speed {:.2f} u/s) - weapon accuracy state corrupted", out.predicted_inaccuracy, speed );
				}
			}
		}

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

	bool rage::plan_stop( const aim_context& ctx, const target& best, const systems::local::snapshot& local, float needed_hc, float head_tolerance ) const
	{
		// Stop planning: a locked target while moving is always worth
		// stopping for. Gating the stop on a "would the stopped shot
		// land" evaluation is wrong - that evaluation runs against the
		// MOVING scan's aim data (interpolated shoot-history eye, moving
		// multipoints), which sits noticeably off at range and rejected
		// every stop until the target was point-blank. Stopping itself
		// is harmless (the player can keep moving any tick), so the stop
		// decision only asks "is there a target". The hit chance gate
		// lives where it belongs: the fire gate, evaluated with the REAL
		// standing accuracy once the velocity has landed.

		if ( !best.valid )
		{
			// Nothing to stop for - the geometry says no.
			return false;
		}

		// Airborne: the deceleration is executed by the airstrafe shift
		// air-stop, so any locked target arms it.
		if ( !ctx.on_ground )
		{
			return true;
		}

		// Ground: locked target + moving = stop. The fire gate (speed
		// landed + standing-accuracy hitchance) decides when the shot
		// actually leaves.
		return true;
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

		if ( config.no_spread.value )
		{
			return this->run_no_spread( cmd, ctx, candidates, eye_candidates, local, allow_fire );
		}

		const auto& prestate = systems::g_prediction.pre( );

		// Current-shot selection is always based on current engine shoot-history.
		auto current_hits = this->scan_from_eye_candidates( eye_candidates, candidates, {}, ctx.predicted_inaccuracy, k_max_scan_traces, ctx, local );
		const auto best = this->select_best( ctx, current_hits, ctx.predicted_inaccuracy );

		// DT fresh-aim guard: only feed the live aim decision when the scan
		// produced a target. On an empty tick best.hit is the zero vector -
		// feeding it would overwrite m_dt_aim with garbage, and on_fired
		// stamps every entry with m_dt_aim whenever m_has_dt_aim is set
		// (regardless of the DT switch), so the next shot would fly off.
		if ( best.valid )
		{
			this->m_double_tap.update_aim( best.hit.aim_angle - g_shared.get_aim_punch( local.pawn ) );
		}

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
		// Speed gate (movement-fix aware): the autostop executes the stop
		// by writing counter-movement into the usercmd, and it takes
		// several ticks for the velocity to actually land - during that
		// window the accuracy penalty is still live no matter what the
		// sampled hitchance says. A grounded shot is only allowed once
		// the predicted velocity (what the server executes this command)
		// is inside the accurate threshold. Force shots stay exempt (the
		// player explicitly asked to fire anyway).
		const auto stop_landed = !ctx.on_ground || ctx.predicted_velocity.length_2d( ) <= ctx.accurate_threshold;
		const auto accurate = best.valid && stop_landed && standing_hc >= needed_hc - 0.01f - head_tolerance - dt_hc_boost;
		const auto max_acc = g_shared.is_max_accuracy( standing_inaccuracy );
		// Force shot: by default only at max accuracy; when a minimum force
		// hitchance is configured, fire as soon as the standing hitchance
		// meets it instead.
		const auto force_hc = std::clamp( ctx.on_ground ? config.force_hitchance.value : config.force_hitchance_air.value, 0, 100 );
		const auto force_eligible = max_acc || ( force_hc > 0 && standing_hc * 100.0f >= static_cast< float >( force_hc ) - 0.5f - head_tolerance * 100.0f );
		const auto force = best.valid && ( ctx.on_ground ? ( config.force_shot.value && force_eligible ) : ( config.force_shot_air.value && force_eligible ) );
		auto shot_viable = accurate || force;

		// One-shot diagnostic: a shot considered accurate while the player
		// is moving fast is only possible when the sampled hitchance is
		// inflated (stale accuracy getters / zero inaccuracy). Surface it
		// once so the root cause is visible in the console instead of
		// guessing at "shots fire while moving".
		if ( best.valid && shot_viable && ctx.on_ground
			&& prestate.networked_velocity.length_2d( ) > ctx.accurate_threshold * 2.0f
			&& best.hitchance > 0.85f )
		{
			static bool hc_warned{};
			if ( !hc_warned )
			{
				hc_warned = true;
				diag::writef( diag::level::info, "[hitchance] WARNING: accurate shot while moving (hc {:.2f} at {:.0f} u/s) - accuracy getters may be stale", best.hitchance, prestate.networked_velocity.length_2d( ) );
			}
		}

		// Autostop planning (rewritten): stopping only shrinks the spread
		// cone, so the plan re-evaluates the CURRENT best hit with the
		// zero-velocity standing accuracy instead of re-scanning from a
		// predicted stop eye (O(1) per frame, works even when the moving
		// scan produced no hit at all). When the stopped shot becomes
		// viable, arm the stop and let the autostop / air-stop decelerate -
		// the bullet then leaves from the stopped spread.
		if ( !shot_viable && this->should_stop_movement( ctx ) )
		{
			this->m_should_stop = this->plan_stop( ctx, best, local, needed_hc, head_tolerance );

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
		// PENETRATED shots are exempt: the exposed point was found through
		// the wall, and pushing it sideways by the lead slides it off the
		// hitbox onto wall - the bullet clips the cover and the shot that
		// would have connected whiffs instead.
		if ( config.no_spread.value && tgt.hit.record && !tgt.hit.record->extrapolated && !tgt.hit.penetrated )
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

				auto lead = target_velocity * std::min( lead_time, 0.25f );

				// Jittering targets: a sideways lead pushes the aim point
				// across the hitbox edge and every corrected bullet misses.
				// Verify the led ray still crosses the RECORD capsule (the
				// pose the server rewinds to) and halve the lead until it
				// does; the un-led point is the fallback.
				if ( tgt.hit.bone_index >= 0 && tgt.hit.bone_index < 28 )
				{
					const auto& bone = tgt.hit.record->bones[ tgt.hit.bone_index ];
					const auto& hb = tgt.hit.hitbox;
					const auto capsule_start = bone.rotation.rotate_vector( hb.mins ) + bone.position;
					const auto capsule_end = bone.rotation.rotate_vector( hb.maxs ) + bone.position;
					const auto radius = hb.radius > 0.0f ? hb.radius : 1.8f;

					auto led_point = tgt.hit.position + lead;
					for ( auto iter = 0; iter < 3; ++iter )
					{
						auto fraction{ 1.0f };
						const auto dir = ( led_point - shoot_eye ).normalized( );
						if ( g_shared.ray_vs_capsule( shoot_eye, dir * shared_ctx.range, capsule_start, capsule_end, radius, fraction ) )
						{
							aim_position = led_point;
							break;
						}

						lead *= 0.5f;
						led_point = tgt.hit.position + lead;
					}
				}
				else
				{
					aim_position += lead;
				}
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

		if ( config.no_spread.value || seed_mode )
		{
			if ( !this->apply_no_spread( aim_angle, tgt, shoot_eye, stamp_tick, tick_base ) )
			{
				return;
			}

			if ( shared_ctx.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver )
			{
				this->log_revolver_aim( tgt, aim_angle, stamp_tick );
			}
		}

		g_shared.last_shoot_tick( ) = tick_base;

		this->log_shot( cmd, tgt, aim_angle, aim_position, shoot_eye, stamp_tick, was_forced, seed_mode );
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
				|| shared_ctx.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver
				|| config.no_spread.value )
			{
				cmd->buttons.value |= cstypes::command_buttons::in_attack;
				cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
				cmd->buttons.value_scroll |= cstypes::command_buttons::in_attack;

				if ( history_size > 0 )
				{
					cmd->csgo_user_cmd.set_attack1_start_history_index( history_size - 1 );
				}

				// This path has already written the real attack expression.
				// Commit here instead of asking double_tap::on_fired to infer
				// it through a second can_shoot check after last_shoot_tick moved.
				features::misc::g_impacts.on_shot_committed( cmd->command_number );
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
			// is now rejected by server-side viewangle validation, so both
			// hidden-heading paths use an engine-valid inverted pitch
			// instead (the heading reversal alone hides the aim; the bullet
			// still travels through the input history angle). Clamped so a
			// steep aim + aim punch can never push the wire pitch past the
			// ±90 validation bound.
			command_aim.x = vac_bypass ? -punched_aim.x : std::clamp( -punched_aim.x, -89.0f, 89.0f );
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
			// Rewritten: the old gate (speed * m_flInaccuracyMove >
			// m_flInaccuracyStand) depends on weapon-vdata schema offsets
			// that silently break on game updates and kill the autostop
			// entirely. The speed-vs-accurate-threshold comparison needs
			// no schema data at all and matches the fire speed gate, so
			// the stop plan and the fire gate agree on what "moving" is.
			return velocity.length_2d( ) > ctx.accurate_threshold;
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
