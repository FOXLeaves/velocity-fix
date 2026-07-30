#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
namespace features::combat {

	void rage::on_create_move( systems::input::usercmd* cmd )
	{
		auto& ctx = g_shared.ctx( );
		if ( !ctx.valid )
		{
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
		const auto local = systems::g_local.get( );

		if ( !is_knife && !is_taser && ( ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg ) )
		{
			return;
		}

		auto aim_ctx = this->build_context( cmd, local );

		if ( is_knife )
		{
			if ( !g_shared.can_shoot( cmd, local.controller ) )
			{
				return;
			}

			this->run_knife( cmd, aim_ctx, local );
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
			//this->auto_revolver( cmd, aim_ctx, local );
		}
		else
		{
			if ( !g_shared.can_shoot( cmd, local.controller ) )
			{
				return;
			}

			this->run_gun( cmd, aim_ctx, local );
		}
	}

	void rage::on_render( xdraw::draw_list& draw_list )
	{
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
				col = { 255, 80,  80  }; break; // head — red
			case 2: case 3:
				col = { 220, 220, 60  }; break; // stomach — yellow
			case 4: case 5: case 6:
				col = { 255, 160, 60  }; break; // chest — orange
			case 7: case 8: case 9: case 10: case 11: case 12:
				col = { 80,  160, 255 }; break; // legs — blue
			case 13: case 14: case 15: case 16: case 17: case 18:
				col = { 180, 80,  255 }; break; // arms — purple
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

	void rage::process_doubletap( systems::input::usercmd* cmd, const systems::local::snapshot& local, bool charge_dt )
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
		if ( !config.doubletap.value )
		{
			return;
		}

		const auto base_cmd = cmd->csgo_user_cmd.mutable_base( );
		if ( !base_cmd )
		{
			return;
		}

		const auto client_tick = base_cmd->client_tick( );
		const auto next_primary = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

		const auto can_attack = client_tick >= next_primary && tick_base >= g_shared.last_shoot_tick( ) + 2 && !memory::read<bool>( shared_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_bInReload"_hash ) ) && memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_hash ) ) > 0;

		const auto should_attack = can_attack && charge_dt;

		const auto history_n = cmd->csgo_user_cmd.input_history_size( );
		for ( auto i = 0; i < history_n; ++i )
		{
			if ( const auto entry = cmd->csgo_user_cmd.mutable_input_history( i ) )
			{
				entry->set_player_tick_count( should_attack ? next_primary : 0 );
			}
		}

		if ( should_attack && !this->m_doubletap_fired )
		{
			if ( const auto subtick_moves = base_cmd->mutable_subtick_moves( ) )
			{
				if ( const auto press = systems::g_input.acquire_subtick_step( subtick_moves ) )
				{
					press->set_button( cstypes::command_buttons::in_attack );
					press->set_pressed( true );
					press->set_when( 0.0f );
					press->set_analog_forward_delta( 0.0f );
					press->set_analog_left_delta( 0.0f );
				}

				if ( const auto release = systems::g_input.acquire_subtick_step( subtick_moves ) )
				{
					release->set_button( cstypes::command_buttons::in_attack );
					release->set_pressed( false );
					release->set_when( 1.0f );
					release->set_analog_forward_delta( 0.0f );
					release->set_analog_left_delta( 0.0f );
				}
			}
		}
		else
		{
			cmd->buttons.value &= ~cstypes::command_buttons::in_attack;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
		}

		this->m_doubletap_fired = should_attack && !this->m_doubletap_fired;

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

		const_cast<rage*>( this )->m_extrapolated_records.clear( );
		const_cast<rage*>( this )->m_extrapolated_records.reserve( players.size( ) );

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

			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CCSPlayerController", "m_hPlayerPawn"_hash ) );
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

			if ( records.empty( ) )
			{
				auto extrap = g_shared.lc( ).extrapolate( pawn );
				if ( !extrap.has_value( ) )
				{
					continue;
				}

				const_cast<rage*>( this )->m_extrapolated_records.push_back( std::move( *extrap ) );
				records.push_back( &const_cast<rage*>( this )->m_extrapolated_records.back( ) );
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

			const auto pick_record_indices = [ &records ]( std::array<int, k_max_scan_records>& out_indices ) -> int
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

					add_index( 0 );

					if ( count > 1 )
					{
						add_index( static_cast< int >( count - 1 ) );
					}

					if ( count > 2 )
					{
						add_index( static_cast< int >( count / 2 ) );
					}

					if ( count > 3 )
					{
						add_index( static_cast< int >( ( count * 3 ) / 4 ) );
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

		return out;
	}

	void rage::run_gun( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !settings::g_combat.m_ragebot.enabled )
		{
			return;
		}

		auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );

		const auto finish_doubletap = [ & ]( bool charge_dt )
		{
			if ( config.doubletap.value )
			{
				this->process_doubletap( cmd, local, charge_dt );
			}
		};

	auto candidates = this->gather_candidates( local );

	{
		std::lock_guard lock( m_debug_mtx );
		m_debug_points.clear( );
	}

	if ( candidates.empty( ) )
	{
		finish_doubletap( false );
		return;
	}

	auto eye_candidates = g_shared.sh( ).get_candidates( );
		if ( eye_candidates.count == 0 )
		{
			eye_candidates.entries[ 0 ].position = g_shared.get_shoot_position( );
			eye_candidates.entries[ 0 ].is_uninterpolated = true;
			eye_candidates.count = 1;
		}

		const auto primary_eye = eye_candidates.entries[ 0 ].position;

		if ( config.no_spread.value )
		{
			shared_ctx.inaccuracy = g_shared.get_inaccuracy( false );

			std::vector<scan_hit> all_hits;

			for ( auto i = 0; i < eye_candidates.count; ++i )
			{
				auto hits = this->scan_players( eye_candidates.entries[ i ].position, shared_ctx.inaccuracy, ctx, candidates, local );

				for ( auto& h : hits )
				{
					h.source_eye = eye_candidates.entries[ i ];
					all_hits.push_back( std::move( h ) );
				}
			}

			if ( all_hits.empty( ) )
			{
				finish_doubletap( false );
				return;
			}

			const auto best = this->select_best( ctx, all_hits, shared_ctx.inaccuracy );
			if ( !best.valid )
			{
				finish_doubletap( false );
				return;
			}

			finish_doubletap( true );
			this->fire_gun( cmd, best, false, best.hit.source_eye.position, local );
			return;
		}

		const auto stop = this->predict_stop( ctx, primary_eye, local );
		const auto scan_inaccuracy = stop ? stop->inaccuracy : ctx.predicted_inaccuracy;

		std::vector<scan_hit> all_hits;

		for ( auto i = 0; i < eye_candidates.count; ++i )
		{
			const auto eye = ( stop && i == 0 ) ? stop->eye : eye_candidates.entries[ i ].position;
			auto hits = this->scan_players( eye, scan_inaccuracy, ctx, candidates, local );

			for ( auto& h : hits )
			{
				h.source_eye = eye_candidates.entries[ i ];
				all_hits.push_back( std::move( h ) );
			}
		}

		if ( all_hits.empty( ) && stop )
		{
			for ( auto i = 0; i < eye_candidates.count; ++i )
			{
				auto hits = this->scan_players( eye_candidates.entries[ i ].position, ctx.predicted_inaccuracy, ctx, candidates, local );

				for ( auto& h : hits )
				{
					h.source_eye = eye_candidates.entries[ i ];
					all_hits.push_back( std::move( h ) );
				}
			}
		}

		if ( all_hits.empty( ) )
		{
			finish_doubletap( false );
			return;
		}

		const auto best = this->select_best( ctx, all_hits, scan_inaccuracy );
		if ( !best.valid )
		{
			finish_doubletap( false );
			return;
		}

		this->m_should_stop = this->should_stop_movement( ctx );

		const auto& prestate = systems::g_prediction.pre( );
		const auto predicted_speed = ctx.velocity.length_2d( );
		const auto pre_speed = prestate.networked_velocity.length_2d( );

		const auto needed_hc = config.hitchance_override.value ? static_cast< float >( config.hitchance_override_value ) / 100.0f : static_cast< float >( config.hitchance ) / 100.0f;
		const auto duckpeek_active = settings::g_combat.m_duckpeek.enabled.value && ctx.on_ground;
		const auto is_ducked = ( prestate.flags & cstypes::entity_flags::ducking ) != 0;

		const auto standing_inaccuracy = duckpeek_active ? this->get_standing_inaccuracy( local, ctx ) : ctx.predicted_inaccuracy;
		const auto standing_hc = duckpeek_active ? this->evaluate_hitchance( best.hit, ctx, standing_inaccuracy ) : best.hitchance;

		const auto accurate = standing_hc >= needed_hc;
		const auto max_acc = g_shared.is_max_accuracy( standing_inaccuracy );
		const auto force = ctx.on_ground ? ( config.force_shot.value && max_acc ) : ( config.force_shot_air.value && max_acc );
		const auto shot_viable = accurate || force;

		if ( duckpeek_active )
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

		auto can_shoot_now = pre_speed <= ctx.accurate_threshold && predicted_speed <= ctx.accurate_threshold;

		auto ready_to_fire = can_shoot_now && shot_viable;
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

		if ( ready_to_fire )
		{
			finish_doubletap( true );
			this->fire_gun( cmd, best, !accurate && force, best.hit.source_eye.position, local );

			if ( duckpeek_active )
			{
				this->m_duckpeek_reduck = true;
				this->m_release_duck_for_shot = false;
			}
		}
		else
		{
			finish_doubletap( false );
		}
	}

	void rage::run_taser( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !settings::g_combat.m_zeusbot.enabled )
		{
			return;
		}

		auto candidates = this->gather_candidates( local );
		if ( candidates.empty( ) )
		{
			return;
		}

		auto eye_candidates = g_shared.sh( ).get_candidates( );
		if ( eye_candidates.count == 0 )
		{
			eye_candidates.entries[ 0 ].position = g_shared.get_shoot_position( );
			eye_candidates.entries[ 0 ].is_uninterpolated = true;
			eye_candidates.count = 1;
		}

		std::vector<scan_hit> all_hits;

		for ( auto i = 0; i < eye_candidates.count; ++i )
		{
			auto hits = this->scan_taser( eye_candidates.entries[ i ].position, ctx, candidates, local );

			for ( auto& h : hits )
			{
				h.source_eye = eye_candidates.entries[ i ];
				all_hits.push_back( std::move( h ) );
			}
		}

		if ( all_hits.empty( ) )
		{
			return;
		}

		target best{};

		for ( const auto& h : all_hits )
		{
			if ( !best.valid || h.score > best.score )
			{
				best.hit = h;
				best.hitchance = 1.0f;
				best.score = h.score;
				best.valid = true;
			}
		}

		if ( best.valid )
		{
			this->m_zeus_fired = true;
			this->fire_melee( cmd, best, local );
		}
	}

	void rage::run_knife( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !settings::g_combat.m_knifebot.enabled )
		{
			return;
		}

		const auto info = this->get_knife_info( local );
		if ( !info.can_slash && !info.can_stab )
		{
			return;
		}

		constexpr auto max_knife_dist_sq = 150.0f * 150.0f;
		auto candidates = this->gather_candidates( local, max_knife_dist_sq );
		if ( candidates.empty( ) )
		{
			return;
		}

		auto eye_candidates = g_shared.sh( ).get_candidates( );
		if ( eye_candidates.count == 0 )
		{
			eye_candidates.entries[ 0 ].position = g_shared.get_shoot_position( );
			eye_candidates.entries[ 0 ].is_uninterpolated = true;
			eye_candidates.count = 1;
		}

		std::vector<scan_hit> all_hits;

		for ( auto i = 0; i < eye_candidates.count; ++i )
		{
			auto hits = this->scan_knife( eye_candidates.entries[ i ].position, ctx, info, candidates, local );

			for ( auto& h : hits )
			{
				h.source_eye = eye_candidates.entries[ i ];
				all_hits.push_back( std::move( h ) );
			}
		}

		if ( all_hits.empty( ) )
		{
			return;
		}

		target best{};
		target best_backstab{};

		for ( const auto& h : all_hits )
		{
			auto& dest = h.is_backstab ? best_backstab : best;

			if ( !dest.valid || h.score > dest.score )
			{
				dest.hit = h;
				dest.hitchance = 1.0f;
				dest.score = h.score;
				dest.valid = true;
			}
		}

		auto& chosen = best_backstab.valid ? best_backstab : best;
		if ( !chosen.valid )
		{
			return;
		}

		this->m_knife_attack = static_cast< std::uint8_t >( chosen.hit.attack_type );
		this->fire_melee( cmd, chosen, local );
	}

	void rage::auto_revolver( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !settings::g_combat.m_ragebot.enabled )
		{
			return;
		}

		const auto& shared_ctx = g_shared.ctx( );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto ready_tick = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_nPostponeFireReadyTicks"_hash ) );
		const auto ready_frac = memory::read<float>( shared_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_flPostponeFireReadyFrac"_hash ) );
		const auto is_hauled = memory::read<bool>( shared_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_bIsHauledBack"_hash ) );
		const auto next_attack = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );

		if ( tick_base < next_attack )
		{
			this->m_revolver_cocking = false;
			return;
		}

		const auto& sh = g_shared.sh( );
		const auto lead = sh.client_tick( ) - sh.server_tick( );
		const auto server_fire_tick = tick_base + lead + 1;

		if ( is_hauled && server_fire_tick >= ready_tick )
		{
			auto candidates = this->gather_candidates( local );
			if ( candidates.empty( ) )
			{
				this->m_revolver_cocking = false;
				return;
			}

			auto eye_candidates = g_shared.sh( ).get_candidates( );
			if ( eye_candidates.count == 0 )
			{
				eye_candidates.entries[ 0 ].position = g_shared.get_shoot_position( );
				eye_candidates.entries[ 0 ].is_uninterpolated = true;
				eye_candidates.count = 1;
			}

			const auto primary_eye = eye_candidates.entries[ 0 ].position;
			const auto stop = this->predict_stop( ctx, primary_eye, local );
			const auto scan_eye = stop ? stop->eye : primary_eye;
			const auto scan_inac = stop ? stop->inaccuracy : ctx.predicted_inaccuracy;

			std::vector<scan_hit> all_hits;

			for ( auto i = 0; i < eye_candidates.count; ++i )
			{
				const auto eye = ( stop && i == 0 ) ? scan_eye : eye_candidates.entries[ i ].position;
				auto hits = this->scan_players( eye, scan_inac, ctx, candidates, local );

				for ( auto& h : hits )
				{
					h.source_eye = eye_candidates.entries[ i ];
					all_hits.push_back( std::move( h ) );
				}
			}

			if ( all_hits.empty( ) )
			{
				this->m_revolver_cocking = false;
				return;
			}

			const auto best = this->select_best( ctx, all_hits, scan_inac );
			if ( !best.valid )
			{
				this->m_revolver_cocking = false;
				return;
			}

			const auto record_time = cstypes::tick_fraction::from_value( best.hit.record->simulation_time / cstypes::tick_interval );
			const auto entry = cmd->csgo_user_cmd.mutable_input_history( 0 );

			if ( entry )
			{
				const auto aim_punch = g_shared.get_aim_punch( local.pawn );

				if ( const auto angles = entry->mutable_view_angles( ) )
				{
					angles->set_x( best.hit.aim_angle.x - aim_punch.x );
					angles->set_y( best.hit.aim_angle.y - aim_punch.y );
				}

				entry->set_render_tick_count( record_time.tick + 1 );
				entry->set_render_tick_fraction( 0.0f );

				if ( !best.hit.source_eye.is_uninterpolated )
				{
					auto tick_add = [ ]( int t, float f, int dt, float df )
						{
							f += df;
							auto carry = static_cast< int >( std::floor( f ) );
							f -= static_cast< float >( carry );
							return std::pair{ t + dt + carry, f };
						};

					const auto [stamp_tick, stamp_frac] = tick_add( best.hit.source_eye.player_tick, best.hit.source_eye.player_frac, best.hit.source_eye.lerp_ticks_int, best.hit.source_eye.lerp_ticks_frac );

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

				cmd->csgo_user_cmd.set_attack1_start_history_index( 0 );
			}

			cmd->buttons.value |= cstypes::command_buttons::in_attack;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
			this->m_revolver_cocking = true;
			this->m_firing_this_tick = true;
			return;
		}

		cmd->buttons.value |= cstypes::command_buttons::in_attack;
		cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
		cmd->csgo_user_cmd.set_attack1_start_history_index( 0 );
		this->m_revolver_cocking = true;
	}

	std::vector<rage::scan_hit> rage::scan_players( const math::vector3& eye, float inaccuracy, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		std::vector<std::vector<scan_hit>> per_candidate( candidates.size( ) );

		threadpool::parallel_for( 0, static_cast< int >( candidates.size( ) ), [ & ]( int begin, int end )
			{
				for ( auto ci = begin; ci < end; ++ci )
				{
					auto& cand = candidates[ ci ];

					for ( auto ri = 0; ri < cand.record_count; ++ri )
					{
						if ( !cand.records[ ri ] || !cand.records[ ri ]->valid )
						{
							continue;
						}

						auto hits = this->scan_player( eye, inaccuracy, ctx, cand, cand.records[ ri ], local );

						for ( auto& h : hits )
						{
							per_candidate[ ci ].push_back( std::move( h ) );
						}
					}
				}
			}, 1 );

		std::vector<scan_hit> flat;

		for ( auto& v : per_candidate )
		{
			for ( auto& h : v )
			{
				flat.push_back( std::move( h ) );
			}
		}

		return flat;
	}

	std::vector<rage::scan_hit> rage::scan_player( const math::vector3& eye, float inaccuracy, const aim_context& ctx, candidate& cand, shared::lagcomp::record* record, const systems::local::snapshot& local ) const
	{
		// idk how this happens
		if (!cand.pawn || cand.record_count <= 0 || cand.health <= 0)
			return {};

		const auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );

		const auto game_scene_node = memory::read<std::uintptr_t>( cand.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
		const auto skeleton = g_shared.lc( ).get_skeleton( *record );
		const auto pen_ctx = g_shared.pen( ).prepare_target( cand.pawn, record );

		const auto target_health = static_cast< float >( memory::read<int>( cand.pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) ) );
		const auto legs_lethal = g_shared.pen( ).get_max_damage( 6, pen_ctx.target_armor, pen_ctx.has_helmet, pen_ctx.target_team ) >= target_health;
		const auto arms_lethal = g_shared.pen( ).get_max_damage( 4, pen_ctx.target_armor, pen_ctx.has_helmet, pen_ctx.target_team ) >= target_health;

		const auto force_body = config.body_aim.value;

		std::array<int, 19> scan_order{};
		auto scan_count{ 0 };

		if ( !force_body && config.hitboxes.values[ 0 ] )
		{
			scan_order[ scan_count++ ] = 0;
		}

		if ( config.hitboxes.values[ 1 ] )
		{
			scan_order[ scan_count++ ] = 4;
			scan_order[ scan_count++ ] = 5;
			scan_order[ scan_count++ ] = 6;
		}

		if ( config.hitboxes.values[ 2 ] )
		{
			scan_order[ scan_count++ ] = 3;
			scan_order[ scan_count++ ] = 2;
		}

		if ( config.hitboxes.values[ 3 ] || arms_lethal )
		{
			for ( auto idx : { 13, 14, 15, 16, 17, 18 } )
			{
				scan_order[ scan_count++ ] = idx;
			}
		}

		if ( config.hitboxes.values[ 4 ] || legs_lethal )
		{
			for ( auto idx : { 7, 8, 9, 10 } )
			{
				scan_order[ scan_count++ ] = idx;
			}
		}

		if ( config.hitboxes.values[ 5 ] || legs_lethal )
		{
			for ( auto idx : { 11, 12 } )
			{
				scan_order[ scan_count++ ] = idx;
			}
		}

		struct trace_point
		{
			math::vector3 position;
			int hitbox_index;
			int bone_index;
			systems::hitboxes::entry hitbox;
			bool is_center;
		};

		std::vector<trace_point> points;
		points.reserve( static_cast< std::size_t >( scan_count ) * 12 );

		for ( auto idx = 0; idx < scan_count; ++idx )
		{
			const auto hitbox_index = scan_order[ idx ];
			const systems::hitboxes::entry* hb{ nullptr };

			for ( const auto& entry : hitbox_set )
			{
				if ( entry.index == hitbox_index )
				{
					hb = &entry;
					break;
				}
			}

			if ( !hb || hb->bone < 0 || hb->bone >= 28 )
			{
				continue;
			}

			const auto& bone = skeleton[ hb->bone ];
			if ( bone.position.length_sqr( ) < 1.0f )
			{
				continue;
			}

			const auto hitbox_center = ( hb->mins + hb->maxs ) * 0.5f;
			const auto center = bone.rotation.rotate_vector( hitbox_center ) + bone.position;

		trace_point cp{};
		cp.position = center;
		cp.hitbox_index = hitbox_index;
		cp.bone_index = hb->bone;
		cp.hitbox = *hb;
		cp.is_center = true;
		points.push_back( cp );

		if ( config.debug_multipoints.value )
		{
			std::lock_guard lock( m_debug_mtx );
			m_debug_points.push_back( { center, hitbox_index, true } );
		}

		if ( config.pointscale > 0.0f )
		{
			const auto mps = this->generate_multipoints( *hb, center, bone.rotation, config.pointscale, eye, inaccuracy );

			for ( const auto& mp : mps )
			{
				trace_point tp{};
				tp.position = mp;
				tp.hitbox_index = hitbox_index;
				tp.bone_index = hb->bone;
				tp.hitbox = *hb;
				tp.is_center = false;
				points.push_back( tp );

				if ( config.debug_multipoints.value )
				{
					std::lock_guard lock( m_debug_mtx );
					m_debug_points.push_back( { mp, hitbox_index, false } );
				}
			}
		}
		}

		if ( points.empty( ) )
		{
			return {};
		}

		const auto needed_hc = config.hitchance_override.value ? static_cast< float >( config.hitchance_override_value ) / 100.0f : static_cast< float >( config.hitchance ) / 100.0f;

		const auto record_applied = !record->is_applied;
		if ( record_applied )
		{
			record->apply( );
		}

		std::vector<scan_hit> results;

		for ( const auto& tp : points )
		{
			const auto aim = math::helpers::calculate_angle( eye, tp.position );
			const auto fov = math::helpers::angle_distance( ctx.view_angles, aim );

			if ( fov > config.max_fov )
			{
				continue;
			}

			if ( !shared_ctx.is_jump_scouting && !config.no_spread.value )
			{
				const auto rough_hc = g_shared.calculate_hitchance_fast( eye, aim, tp.hitbox, skeleton[ tp.bone_index ], inaccuracy, ctx.spread );
				if ( rough_hc < ( needed_hc * 0.85f ) )
				{
					continue;
				}
			}

			const auto hg = systems::g_hitboxes.hitgroup_from_hitbox( tp.hitbox_index );

			shared::penetration::result pen{};
			if ( !g_shared.pen( ).run( eye, tp.position, pen_ctx, local.pawn, local.team, pen, hg ) )
			{
				continue;
			}

			if ( pen.damage < cand.min_damage )
			{
				continue;
			}

			if ( !tp.is_center && tp.hitbox_index == 0 )
			{
				if ( pen.hitgroup != systems::g_hitboxes.hitgroup_from_hitbox( tp.hitbox_index ) )
				{
					continue;
				}
			}

			scan_hit h{};
			h.position = tp.position;
			h.aim_angle = aim;
			h.damage = pen.damage;
			h.fov = fov;
			h.hitbox_index = tp.hitbox_index;
			h.hitgroup = pen.hitgroup;
			h.bone_index = tp.bone_index;
			h.hitbox = tp.hitbox;
			h.is_center = tp.is_center;
			h.pawn = cand.pawn;
			h.health = cand.health;
			h.record = record;

			results.push_back( h );
		}

		if ( record_applied )
		{
			record->restore( );
		}

		return results;
	}

	rage::target rage::select_best( const aim_context& aim_ctx, const std::vector<scan_hit>& hits, float eval_inaccuracy ) const
	{
		auto hitgroup_priority = [ ]( int hitbox_index ) -> int
			{
				if ( hitbox_index == 0 ) { return 4; }
				if ( hitbox_index >= 1 && hitbox_index <= 6 ) { return 3; }
				if ( hitbox_index >= 13 && hitbox_index <= 18 ) { return 2; }
				if ( hitbox_index >= 7 && hitbox_index <= 12 ) { return 1; }
				return 0;
			};

		struct record_group
		{
			shared::lagcomp::record* record;
			std::vector<int> hit_indices;
		};

		std::vector<record_group> groups;
		groups.reserve( 16 );

		for ( auto i = 0; i < static_cast< int >( hits.size( ) ); ++i )
		{
			auto rec = hits[ i ].record;
			auto found{ false };

			for ( auto& g : groups )
			{
				if ( g.record == rec )
				{
					g.hit_indices.push_back( i );
					found = true;
					break;
				}
			}

			if ( !found )
			{
				record_group g{};
				g.record = rec;
				g.hit_indices.reserve( 16 );
				g.hit_indices.push_back( i );
				groups.push_back( std::move( g ) );
			}
		}

		constexpr auto top_k_per_record{ 24 };

		auto cheap_score = [ & ]( const scan_hit& h ) -> float
			{
				const auto prio = static_cast< float >( hitgroup_priority( h.hitbox_index ) );
				const auto dmg_norm = h.damage / 300.0f;
				const auto hp = static_cast< float >( h.health );
				const auto can_kill_bonus = h.damage >= hp ? 100.0f : 0.0f;
				const auto center_bonus = h.is_center ? 0.5f : 0.0f;
				const auto fov_penalty = h.fov * 0.01f;

				return prio * 1000.0f + can_kill_bonus + dmg_norm + center_bonus - fov_penalty;
			};

		for ( auto& group : groups )
		{
			if ( static_cast< int >( group.hit_indices.size( ) ) <= top_k_per_record )
			{
				continue;
			}

			std::partial_sort
			(
				group.hit_indices.begin( ),
				group.hit_indices.begin( ) + top_k_per_record,
				group.hit_indices.end( ),
				[ & ]( int a, int b ) { return cheap_score( hits[ a ] ) > cheap_score( hits[ b ] ); }
			);

			group.hit_indices.resize( top_k_per_record );
		}

		struct evaluated_hit
		{
			int hit_index;
			float hitchance;
			float score;
			bool can_kill;
		};

		std::vector<evaluated_hit> evaluated;
		evaluated.reserve( hits.size( ) );

		for ( auto& group : groups )
		{
			group.record->apply( );

			const auto skeleton = g_shared.lc( ).get_skeleton( *group.record );

			for ( const auto idx : group.hit_indices )
			{
				const auto& h = hits[ idx ];

				if ( !h.record || !h.record->valid )
				{
					continue;
				}

				if ( h.bone_index < 0 || h.bone_index >= 28 )
				{
					continue;
				}

				const auto& bone = skeleton[ h.bone_index ];
				const auto fast_hc = g_shared.calculate_hitchance_fast( h.source_eye.position, h.aim_angle, h.hitbox, bone, eval_inaccuracy, aim_ctx.spread );

				if ( fast_hc < 0.05f )
				{
					continue;
				}

				const auto samples = fast_hc > 0.5f ? 256 : ( fast_hc > 0.2f ? 128 : 64 );
				const auto hc = g_shared.calculate_hitchance( h.source_eye.position, h.aim_angle, h.hitbox, bone, eval_inaccuracy, aim_ctx.spread, samples );
				const auto hp = static_cast< float >( h.health );
				const auto can_kill = h.damage >= hp;
				auto score{ 0.0f };

				if ( can_kill )
				{
					const auto kill_prob = 1.0f - std::powf( 1.0f - hc, 2.0f );
					score = 10000.0f + kill_prob * 1000.0f + hc * 100.0f;
				}
				else
				{
					score = h.damage * hc;
				}

				evaluated.push_back( evaluated_hit{ idx, hc, score, can_kill } );
			}

			group.record->restore( );
		}

		target best{};

		for ( const auto& e : evaluated )
		{
			if ( e.hit_index < 0 || e.hit_index >= static_cast< int >( hits.size( ) ) )
			{
				continue;
			}

			const auto& h = hits[ e.hit_index ];

			if ( !h.record || !h.record->valid )
			{
				continue;
			}

			auto is_better{ false };

			if ( !best.valid )
			{
				is_better = true;
			}
			else
			{
				const auto best_kills = best.hit.damage >= static_cast< float >( best.hit.health );

				if ( e.can_kill && !best_kills )
				{
					is_better = true;
				}
				else if ( !e.can_kill && best_kills )
				{
					is_better = false;
				}
				else if ( e.can_kill && best_kills )
				{
					const auto hc_diff = e.hitchance - best.hitchance;

					if ( std::fabsf( hc_diff ) > 0.05f )
					{
						is_better = hc_diff > 0.0f;
					}
					else
					{
						const auto this_prio = hitgroup_priority( h.hitbox_index );
						const auto best_prio = hitgroup_priority( best.hit.hitbox_index );

						if ( this_prio != best_prio )
						{
							is_better = this_prio > best_prio;
						}
						else if ( h.is_center != best.hit.is_center )
						{
							is_better = h.is_center;
						}
						else
						{
							const auto this_size = ( h.hitbox.maxs - h.hitbox.mins ).length_sqr( );
							const auto best_size = ( best.hit.hitbox.maxs - best.hit.hitbox.mins ).length_sqr( );

							if ( std::fabsf( this_size - best_size ) > 1.0f )
							{
								is_better = this_size > best_size;
							}
							else
							{
								is_better = h.fov < best.hit.fov;
							}
						}
					}
				}
				else
				{
					is_better = h.damage > best.hit.damage;
				}
			}

			if ( is_better )
			{
				best.hit = h;
				best.hitchance = e.hitchance;
				best.score = e.score;
				best.valid = true;
			}
		}

		return best;
	}

	float rage::evaluate_hitchance( const scan_hit& hit, const aim_context& ctx, float inaccuracy ) const
	{
		if ( !hit.record || !hit.record->valid )
		{
			return 0.0f;
		}

		hit.record->apply( );
		const auto hc = g_shared.calculate_hitchance( hit.source_eye.position, hit.aim_angle, hit.hitbox, g_shared.lc( ).get_skeleton( *hit.record )[ hit.bone_index ], inaccuracy, ctx.spread );
		hit.record->restore( );

		return hc;
	}

	float rage::get_standing_inaccuracy( const systems::local::snapshot& local, const aim_context& ctx ) const
	{
		const auto& prestate = systems::g_prediction.pre( );
		auto velocity = prestate.networked_velocity;
		velocity.z = 0.0f;

		const auto speed = velocity.length_2d( );
		if ( speed > ctx.accurate_threshold )
		{
			return g_shared.get_inaccuracy_at_velocity( local.pawn, velocity );
		}

		const auto& shared_ctx = g_shared.ctx( );
		if ( !shared_ctx.weapon_vdata )
		{
			return ctx.predicted_inaccuracy;
		}

		const auto inaccuracy_stand = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyStand"_hash ) );
		return std::max( inaccuracy_stand, g_shared.get_inaccuracy_at_velocity( local.pawn, velocity ) );
	}

	std::vector<rage::scan_hit> rage::scan_taser( const math::vector3& eye, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		std::vector<scan_hit> results;

		for ( auto& cand : candidates )
		{
			for ( auto ri = 0; ri < cand.record_count; ++ri )
			{
				auto* record = cand.records[ ri ];
				if ( !record || !record->valid )
				{
					continue;
				}

				const auto game_scene_node = memory::read<std::uintptr_t>( cand.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				if ( !game_scene_node )
				{
					continue;
				}

				const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
				if ( hitbox_set.count <= 0 )
				{
					continue;
				}

				record->apply( );
				const auto skeleton = g_shared.lc( ).get_skeleton( *record );

				for ( auto i = 0; i < hitbox_set.count; ++i )
				{
					const auto& hb = hitbox_set.entries[ i ];

					if ( hb.bone < 0 || hb.bone >= 28 )
					{
						continue;
					}

					const auto& bone = skeleton[ hb.bone ];
					if ( bone.position.length_sqr( ) < 1.0f )
					{
						continue;
					}

					const auto center = bone.rotation.rotate_vector( ( hb.mins + hb.maxs ) * 0.5f ) + bone.position;
					const auto aim = math::helpers::calculate_angle( eye, center );
					const auto fov = math::helpers::angle_distance( ctx.view_angles, aim );

					if ( fov > settings::g_combat.m_zeusbot.max_fov )
					{
						continue;
					}

					math::vector3 forward{};
					math::helpers::angle_vectors_left( aim, &forward );

					const auto trace = this->trace_taser_hit( eye, forward, shared_ctx.range * 0.85f, cand.pawn, local.pawn );
					if ( trace.hit_entity != cand.pawn )
					{
						continue;
					}

					const auto dist = ( center - eye ).length( );
					const auto range_fraction = dist / shared_ctx.range;

					scan_hit h{};
					h.position = center;
					h.aim_angle = aim;
					h.damage = 500.0f;
					h.score = ( 10000.0f - dist ) * ( range_fraction > 0.92f ? 0.8f : 1.0f );
					h.fov = fov;
					h.hitbox_index = hb.index;
					h.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( hb.index );
					h.bone_index = hb.bone;
					h.hitbox = hb;
					h.is_center = true;
					h.pawn = cand.pawn;
					h.health = cand.health;
					h.record = record;

					results.push_back( h );
				}

				record->restore( );
			}
		}

		return results;
	}

	rage::knife_info rage::get_knife_info( const systems::local::snapshot& local ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto next_primary = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );
		const auto next_secondary = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_hash ) );
		const auto last_shot_time = memory::read<float>( shared_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_fLastShotTime"_hash ) );
		const auto cur_time = static_cast< float >( tick_base ) * cstypes::tick_interval;

		return knife_info
		{
			.can_slash = tick_base >= next_primary,
			.can_stab = tick_base >= next_secondary,
			.charged = ( cur_time - last_shot_time ) > 0.4f,
			.armor_ratio = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flArmorRatio"_hash ) )
		};
	}

	std::vector<rage::scan_hit> rage::scan_knife( const math::vector3& eye, const aim_context& ctx, const knife_info& info, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		constexpr auto stab_range{ 50.0f };
		constexpr auto slash_range{ 66.0f };

		std::vector<scan_hit> results;

		for ( auto& cand : candidates )
		{
			const auto eye_angles = memory::read<math::vector3>( cand.pawn + SCHEMA( "C_CSPlayerPawn", "m_angEyeAngles"_hash ) );
			const auto hp = static_cast< float >( cand.health );

			const auto frontal_slash_dmg = this->get_knife_damage( info.charged ? 40.0f : 25.0f, cand.armor, info.armor_ratio );
			const auto frontal_stab_dmg = this->get_knife_damage( 65.0f, cand.armor, info.armor_ratio );
			const auto frontal_can_kill = ( info.can_slash && frontal_slash_dmg >= hp ) || ( info.can_stab && frontal_stab_dmg >= hp );

			for ( auto ri = 0; ri < cand.record_count; ++ri )
			{
				auto* record = cand.records[ ri ];
				if ( !record || !record->valid )
				{
					continue;
				}

				const auto game_scene_node = memory::read<std::uintptr_t>( cand.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				if ( !game_scene_node )
				{
					continue;
				}

				const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
				if ( hitbox_set.count <= 0 )
				{
					continue;
				}

				record->apply( );
				const auto skeleton = g_shared.lc( ).get_skeleton( *record );

				auto backstab{ false };
				{
					const auto delta = record->origin - systems::g_prediction.pre( ).origin;
					const auto dist_2d = std::sqrtf( delta.x * delta.x + delta.y * delta.y );

					if ( dist_2d > 0.001f )
					{
						const auto dir_x = delta.x / dist_2d;
						const auto dir_y = delta.y / dist_2d;

						math::vector3 body_forward{};
						math::helpers::angle_vectors_left( record->rotation, &body_forward );

						math::vector3 eye_forward{};
						math::helpers::angle_vectors_left( eye_angles, &eye_forward );

						backstab = ( dir_x * body_forward.x + dir_y * body_forward.y ) > 0.475f ||
							( dir_x * eye_forward.x + dir_y * eye_forward.y ) > 0.475f;
					}
				}

				const auto wait_for_backstab = backstab && !frontal_can_kill;

				for ( auto i = 0; i < hitbox_set.count; ++i )
				{
					const auto& hb = hitbox_set.entries[ i ];

					if ( hb.bone < 0 || hb.bone >= 28 )
					{
						continue;
					}

					const auto& bone = skeleton[ hb.bone ];
					if ( bone.position.length_sqr( ) < 1.0f )
					{
						continue;
					}

					const auto center = bone.rotation.rotate_vector( ( hb.mins + hb.maxs ) * 0.5f ) + bone.position;
					const auto dist = ( center - eye ).length( );
					const auto max_reach = info.can_slash ? slash_range : stab_range;

					if ( dist > max_reach )
					{
						continue;
					}

					const auto aim = math::helpers::calculate_angle( eye, center );
					const auto fov = math::helpers::angle_distance( ctx.view_angles, aim );

					if ( fov > settings::g_combat.m_knifebot.max_fov )
					{
						continue;
					}

					math::vector3 forward{};
					math::helpers::angle_vectors_left( aim, &forward );

					for ( const auto try_stab : { true, false } )
					{
						if ( try_stab && !info.can_stab )
						{
							continue;
						}

						if ( !try_stab && !info.can_slash )
						{
							continue;
						}

						const auto reach = try_stab ? stab_range : slash_range;
						if ( dist > reach )
						{
							continue;
						}

						const auto raw_dmg = try_stab ? ( backstab ? 180.0f : 65.0f ) : ( backstab ? 90.0f : ( info.charged ? 40.0f : 25.0f ) );
						const auto damage = this->get_knife_damage( raw_dmg, cand.armor, info.armor_ratio );
						const auto can_kill = damage >= hp;

						if ( wait_for_backstab && !can_kill )
						{
							continue;
						}

						const auto trace = this->trace_knife_hit( eye, forward, reach, cand.pawn, local.pawn );
						if ( trace.hit_entity != cand.pawn )
						{
							continue;
						}

						const auto reach_margin = 1.0f - ( dist / reach );

						scan_hit h{};
						h.position = center;
						h.aim_angle = aim;
						h.damage = damage;
						h.score = can_kill ? ( 10000.0f + damage * reach_margin ) : ( damage * 100.0f * reach_margin );
						h.fov = fov;
						h.hitbox_index = hb.index;
						h.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( hb.index );
						h.bone_index = hb.bone;
						h.hitbox = hb;
						h.is_center = true;
						h.is_backstab = backstab;
						h.attack_type = try_stab ? 1 : 0;
						h.pawn = cand.pawn;
						h.health = cand.health;
						h.record = record;

						results.push_back( h );
						break;
					}
				}

				record->restore( );
			}
		}

		return results;
	}

	void rage::fire_gun( systems::input::usercmd* cmd, const target& tgt, bool was_forced, const math::vector3& shoot_eye, const systems::local::snapshot& local )
	{
		if ( !tgt.hit.record || !tgt.hit.record->valid )
		{
			return;
		}

		this->m_firing_this_tick = true;

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
		const auto aim_punch = g_shared.get_aim_punch( local.pawn );
		auto aim_angle = config.no_spread.value ? math::helpers::calculate_angle( shoot_eye, tgt.hit.position ) : tgt.hit.aim_angle;

		if ( config.no_spread.value )
		{
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

			const auto corrected = g_shared.find_spread_correction( aim_angle, stamp_tick );
			if ( corrected.x == 0.0f && corrected.y == 0.0f && corrected.z == 0.0f )
			{
				this->m_firing_this_tick = false;
				return;
			}

			aim_angle = corrected;
		}

		g_shared.last_shoot_tick( ) = tick_base;

		if ( settings::g_misc.m_impacts.console_log.value )
		{
			const auto hitgroup_name = systems::g_hitboxes.hitgroup_to_name( tgt.hit.hitgroup );
			const auto bt_delta = g_shared.ctx( ).current_tick - tgt.hit.record->tick;
			logging::console::print(
				xs( "[rage] shot target hp {} for {:.0f} in {} (hc {:.0f}%, bt {}t{})" ),
				tgt.hit.health,
				tgt.hit.damage,
				hitgroup_name,
				tgt.hitchance * 100.0f,
				bt_delta,
				was_forced ? xs( ", forced" ) : ""
			);
		}

		features::misc::g_impacts.on_boom( tgt.hit.pawn, tgt.hit.hitgroup, tgt.hit.damage, tgt.hitchance, shared_ctx.inaccuracy, aim_angle, shoot_eye, tgt.hit.record->tick, g_shared.lc( ).get_skeleton( *tgt.hit.record ), was_forced );
		features::esp::player::g_chams.os ().push (tgt.hit.pawn);
		const auto record_time = cstypes::tick_fraction::from_value( tgt.hit.record->simulation_time / cstypes::tick_interval );
		const auto entry = cmd->csgo_user_cmd.mutable_input_history( 0 );

		if ( entry )
		{
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

			if ( !tgt.hit.source_eye.is_uninterpolated )
			{
				auto tick_add = [ ]( int t, float f, int dt, float df )
					{
						f += df;
						auto carry = static_cast< int >( std::floor( f ) );
						f -= static_cast< float >( carry );
						return std::pair{ t + dt + carry, f };
					};

				const auto [stamp_tick, stamp_frac] = tick_add( tgt.hit.source_eye.player_tick, tgt.hit.source_eye.player_frac, tgt.hit.source_eye.lerp_ticks_int, tgt.hit.source_eye.lerp_ticks_frac );

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

			cmd->buttons.value |= cstypes::command_buttons::in_attack;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
			cmd->csgo_user_cmd.set_attack1_start_history_index( 0 );
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

		if ( facing_away || !config.silent.value )
		{
			auto aim = punched_aim;

			if ( facing_away && settings::g_combat.m_antiaim.hide_shots.value )
			{
				aim.x = 179.9f;
				aim.y = std::remainderf( punched_aim.y + 180.0f, 360.0f );
			}

			if ( const auto angles = base->mutable_viewangles( ) )
			{
				angles->set_x( aim.x );
				angles->set_y( aim.y );
			}
		}

		if ( !config.silent.value )
		{
			systems::g_input.set_view_angles( punched_aim );
		}
	}

	void rage::fire_melee( systems::input::usercmd* cmd, const target& tgt, const systems::local::snapshot& local )
	{
		if ( !tgt.hit.record || !tgt.hit.record->valid )
		{
			return;
		}

		this->m_firing_this_tick = true;

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

		g_shared.last_shoot_tick( ) = tick_base;

		const auto record_time = cstypes::tick_fraction::from_value( tgt.hit.record->simulation_time / cstypes::tick_interval );
		const auto entry = cmd->csgo_user_cmd.mutable_input_history( 0 );

		if ( entry )
		{
			if ( const auto angles = entry->mutable_view_angles( ) )
			{
				angles->set_x( tgt.hit.aim_angle.x );
				angles->set_y( tgt.hit.aim_angle.y );
			}

			entry->set_render_tick_count( record_time.tick + 1 );
			entry->set_render_tick_fraction( 0.0f );

			if ( !tgt.hit.source_eye.is_uninterpolated )
			{
				auto tick_add = [ ]( int t, float f, int dt, float df )
					{
						f += df;
						auto carry = static_cast< int >( std::floor( f ) );
						f -= static_cast< float >( carry );
						return std::pair{ t + dt + carry, f };
					};

				const auto [stamp_tick, stamp_frac] = tick_add( tgt.hit.source_eye.player_tick, tgt.hit.source_eye.player_frac, tgt.hit.source_eye.lerp_ticks_int, tgt.hit.source_eye.lerp_ticks_frac );

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

			const auto is_secondary = tgt.hit.attack_type == 1;

			if ( is_secondary )
			{
				cmd->buttons.value |= cstypes::command_buttons::in_second_attack;
				cmd->buttons.value_changed |= cstypes::command_buttons::in_second_attack;
				cmd->csgo_user_cmd.set_attack2_start_history_index( 0 );
			}
			else
			{
				cmd->buttons.value |= cstypes::command_buttons::in_attack;
				cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
				cmd->csgo_user_cmd.set_attack1_start_history_index( 0 );
			}
		}

		if ( const auto angles = base->mutable_viewangles( ) )
		{
			angles->set_x( tgt.hit.aim_angle.x );
			angles->set_y( tgt.hit.aim_angle.y );
		}
	}

	std::vector<math::vector3> rage::generate_multipoints( const systems::hitboxes::entry& hitbox, const math::vector3& center, const math::quaternion& bone_rot, float pointscale, const math::vector3& shoot_pos, float inaccuracy ) const
	{
		std::vector<math::vector3> out;

		const auto scale = std::clamp( pointscale / 100.0f, 0.0f, 1.0f );
		if ( scale <= 0.01f )
		{
			out.push_back( center );
			return out;
		}

		const auto radius = hitbox.radius > 0.0f ? hitbox.radius : 2.0f;
		const auto sr     = radius * scale;

		if ( sr < 0.1f )
		{
			out.push_back( center );
			return out;
		}

		const auto hb_mid   = ( hitbox.mins + hitbox.maxs ) * 0.5f;
		const auto capsule_a = center + bone_rot.rotate_vector( hitbox.mins - hb_mid );
		const auto capsule_b = center + bone_rot.rotate_vector( hitbox.maxs - hb_mid );

		// Clamp a proposed point to stay within radius * inward of the capsule surface.
		const auto safe_point = [ & ]( const math::vector3& point, float inward ) -> math::vector3
		{
			const auto ab     = capsule_b - capsule_a;
			const auto ab_sq  = ab.dot( ab );
			const auto t      = ab_sq < 1.0e-8f ? 0.0f : std::clamp( ( point - capsule_a ).dot( ab ) / ab_sq, 0.0f, 1.0f );
			const auto closest = capsule_a + ab * t;
			const auto delta  = point - closest;
			const auto dist   = delta.length( );
			const auto max_d  = radius * inward;

			if ( dist <= max_d )   return point;
			if ( dist < 1.0e-6f ) return closest;
			return closest + delta * ( max_d / dist );
		};

		// Build a proper view-relative coordinate frame oriented toward the shooter.
		// Using angle_vectors_left (which gives forward / LEFT / up) so that the
		// right/up pair tilts with pitch — e.g. when aiming up at a head the "up"
		// direction correctly points away from the shooter, not straight world-up.
		const auto shoot_dir = ( center - shoot_pos ).normalized( );
		const auto ang       = math::helpers::vector_to_angle( shoot_dir );

		math::vector3 fwd{}, left{}, up{};
		math::helpers::angle_vectors_left( ang, &fwd, &left, &up );

		// angle_vectors_left returns the LEFT vector; negate for right.
		const auto right = math::vector3{ -left.x, -left.y, -left.z };

		switch ( hitbox.index )
		{
		case 0: // head — top-cap anchor + arc ring from CSGO multipoints
		{
			const auto top_cap = ( capsule_a.z > capsule_b.z ) ? capsule_a : capsule_b;
			out.reserve( 11 );

			// Primary axis
			out.push_back( safe_point( top_cap + up * sr,                               0.85f ) );
			out.push_back( safe_point( top_cap - fwd * sr,                              0.85f ) ); // back toward shooter
			out.push_back( safe_point( top_cap + ( up - fwd ).normalized( ) * sr,       0.85f ) );

			// Side points at center height
			out.push_back( safe_point( center + right * sr, 0.85f ) );
			out.push_back( safe_point( center - right * sr, 0.85f ) );

			// Arc ring: lerp from up toward ±right in 3 steps — ensures coverage
			// of the top hemisphere regardless of the player's fake-angle.
			for ( int i = 1; i < 4; ++i )
			{
				const auto t    = static_cast<float>( i ) / 3.0f;
				const auto arc_r = ( up * ( 1.0f - t ) + right * t ).normalized( );
				const auto arc_l = ( up * ( 1.0f - t ) - right * t ).normalized( );
				out.push_back( safe_point( top_cap + arc_r * sr * 0.9f, 0.85f ) );
				out.push_back( safe_point( top_cap + arc_l * sr * 0.9f, 0.85f ) );
			}
			break;
		}

		case 2: case 3: // stomach / pelvis
		{
			out.reserve( 5 );
			out.push_back( safe_point( center + right * sr,        0.9f ) );
			out.push_back( safe_point( center - right * sr,        0.9f ) );
			out.push_back( safe_point( center + up * sr * 0.7f,    0.9f ) );
			out.push_back( safe_point( capsule_a,                  0.9f ) );
			out.push_back( safe_point( capsule_b,                  0.9f ) );
			break;
		}

		case 4: case 5: case 6: // chest (lower, upper, neck)
		{
			out.reserve( 6 );
			out.push_back( safe_point( center + right * sr,        0.9f ) );
			out.push_back( safe_point( center - right * sr,        0.9f ) );
			// up/down coverage — far more useful than forward/back for chest
			out.push_back( safe_point( center + up * sr * 0.65f,  0.9f ) );
			out.push_back( safe_point( center - up * sr * 0.5f,   0.9f ) );
			// diagonal: upper-right and lower-left for maximum spread utilisation
			out.push_back( safe_point( center + ( right + up ).normalized( ) * sr * 0.75f, 0.9f ) );
			out.push_back( safe_point( center - ( right + up ).normalized( ) * sr * 0.75f, 0.9f ) );
			break;
		}

		case 7: case 8: case 9: case 10: case 11: case 12: // legs / feet
		{
			out.reserve( 4 );
			out.push_back( safe_point( capsule_a,                  0.9f ) );
			out.push_back( safe_point( capsule_b,                  0.9f ) );
			out.push_back( safe_point( center + right * sr,        0.85f ) );
			out.push_back( safe_point( center - right * sr,        0.85f ) );
			break;
		}

		case 13: case 14: case 15: case 16: case 17: case 18: // arms
		{
			out.reserve( 3 );
			out.push_back( safe_point( capsule_a,                  0.9f ) );
			out.push_back( safe_point( capsule_b,                  0.9f ) );
			out.push_back( safe_point( center - fwd * sr * 0.7f,  0.9f ) ); // near side
			break;
		}

		default:
		{
			out.reserve( 2 );
			out.push_back( safe_point( center + right * sr, 0.85f ) );
			out.push_back( safe_point( center - right * sr, 0.85f ) );
			break;
		}
		}

		// Dynamic scale: pull each point toward the capsule center proportionally
		// to the probability that the inaccuracy cone can actually reach it.
		// Equivalent to the CSGO dynamic_scale feature:
		//   p = Lerp(1 - probability, p, center)  →  p = center + (p - center) * probability
		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		if ( config.dynamic_pointscale.value && inaccuracy > 0.001f && hitbox.radius > 0.0f )
		{
			const auto dist = ( center - shoot_pos ).length( );
			for ( auto& p : out )
			{
				const auto delta        = p - center;
				const auto effective_r  = ( radius * 2.0f ) - delta.length( );
				const auto factor       = effective_r / ( inaccuracy * dist );
				const auto probability  = std::min( factor * factor, 1.0f );
				p = center + delta * probability;
			}
		}

		return out;
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

		if ( shared_ctx.weapon_type != cstypes::weapon_type::sniper )
		{
			return false;
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
		if ( hp < base )
		{
			return hp + 1.0f;
		}

		return base;
	}

	float rage::get_knife_damage( float raw, int armor, float armor_ratio ) const
	{
		if ( armor <= 0 )
		{
			return raw;
		}

		const auto ratio = armor_ratio * 0.5f;
		auto damage_to_health = raw * ratio;
		const auto damage_to_armor = ( raw - damage_to_health ) * 0.5f;

		if ( damage_to_armor > static_cast< float >( armor ) )
		{
			damage_to_health = raw - static_cast< float >( armor ) * 2.0f;
		}

		return std::max( 0.0f, std::floorf( damage_to_health ) );
	}

	systems::tracing::result rage::trace_taser_hit( const math::vector3& origin, const math::vector3& forward, float range, std::uintptr_t target_pawn, std::uintptr_t local_pawn ) const
	{
		const auto end = origin + forward * range;
		const int filter_extras[ ]{ 0, 15 };

		for ( const auto extra : filter_extras )
		{
			const auto filter = extra == 0 ? systems::g_tracing.make_filter( local_pawn, 0x001c1003, 4 ) : systems::g_tracing.make_filter( local_pawn, 0x001c1003, 4, 15 );
			auto result = systems::g_tracing.trace( origin, end, filter );

			if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
			{
				return result;
			}

			for ( auto radius = 2.0f; radius <= 4.0f; radius += 2.0f )
			{
				const auto sweep_end = end - forward * radius;
				result = systems::g_tracing.trace_sphere( origin, sweep_end, radius, filter );

				if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
				{
					return result;
				}
			}
		}

		systems::tracing::result miss{};
		miss.fraction = 1.0f;
		miss.hit_entity = 0;
		return miss;
	}

	systems::tracing::result rage::trace_knife_hit( const math::vector3& origin, const math::vector3& forward, float reach, std::uintptr_t target_pawn, std::uintptr_t local_pawn ) const
	{
		const auto end = origin + forward * reach;
		const auto knife_filter = systems::g_tracing.make_filter( local_pawn, 0x0c3001, 4 );
		auto result = systems::g_tracing.trace( origin, end, knife_filter );

		if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
		{
			return result;
		}

		const auto weapon_filter = systems::g_tracing.make_filter( local_pawn, 0x0c3001, 4, 15 );
		result = systems::g_tracing.trace( origin, end, weapon_filter );

		if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
		{
			return result;
		}

		for ( auto radius = 14.0f; radius > 0.0f; radius -= 3.0f )
		{
			const auto sweep_end = end - forward * radius;
			result = systems::g_tracing.trace_sphere( origin, sweep_end, radius, weapon_filter );

			if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
			{
				return result;
			}
		}

		result.fraction = 1.0f;
		result.hit_entity = 0;
		return result;
	}

	void rage::draw_penetration_crosshair( xdraw::draw_list& draw_list ) const
	{
		const auto& cfg = settings::g_combat.m_penetration_crosshair;
		if ( !cfg.enabled.value )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		const auto& ctx = g_shared.ctx( );

		if ( !ctx.valid || ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg )
		{
			return;
		}

		if ( systems::g_local.is_in_cinematic( ) || local.team < 2 || !systems::g_frame_data.valid( ) || !local.controller || !systems::g_entities.exists( local.controller ) )
		{
			return;
		}

		const auto view_offset = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
		const auto eye_pos = systems::g_frame_data.origin( ) + view_offset;
		const auto view_angles = systems::g_view.angles( );

		math::vector3 forward{};
		math::helpers::angle_vectors_left( view_angles, &forward );

		const auto first = systems::g_tracing.trace( eye_pos, eye_pos + forward * g_shared.pen( ).get_weapon_data( ).range, local.pawn );
		if ( first.fraction >= 1.0f )
		{
			return;
		}

		const auto& n = first.normal;
		const auto ref = ( std::fabsf( n.z ) < 0.9f ) ? math::vector3{ 0.0f, 0.0f, 1.0f } : math::vector3{ 1.0f, 0.0f, 0.0f };

		const auto d = ref.dot( n );
		const auto tangent = ( ref - n * d ).normalized( );
		const auto bitangent = n.cross( tangent );

		const auto center = first.position + n * 0.05f;
		const auto half_size = 4.5f;

		const math::vector3 corners[ 4 ]
		{
			center - tangent * half_size - bitangent * half_size,
			center + tangent * half_size - bitangent * half_size,
			center + tangent * half_size + bitangent * half_size,
			center - tangent * half_size + bitangent * half_size,
		};

		float sx[ 4 ]{}, sy[ 4 ]{};

		for ( auto i = 0; i < 4; ++i )
		{
			const auto proj = systems::g_view.project( corners[ i ] );
			if ( !systems::g_view.projection_valid( proj ) )
			{
				return;
			}

			sx[ i ] = proj.x;
			sy[ i ] = proj.y;
		}

		auto pen_damage{ 0.0f };
		const auto can_pen = g_shared.pen( ).can( eye_pos, forward, pen_damage, local );

		const auto& fill = can_pen ? cfg.can_penetrate_fill : cfg.blocked_fill;
		const auto& outline = can_pen ? cfg.can_penetrate_outline : cfg.blocked_outline;

		if ( cfg.glow )
		{
			auto& glow = xdraw::get_glow( );
			const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( outline.value.a ) * cfg.glow_strength );
			const auto glow_col = xdraw::color{ outline.value.r, outline.value.g, outline.value.b, glow_a };

			float poly[ 8 ]{ sx[ 0 ], sy[ 0 ], sx[ 1 ], sy[ 1 ], sx[ 2 ], sy[ 2 ], sx[ 3 ], sy[ 3 ] };
			glow.convex_filled( { poly, 8 }, glow_col );
			glow.polyline( { poly, 8 }, glow_col, true, 1.0f );
		}

		float poly[ 8 ]{ sx[ 0 ], sy[ 0 ], sx[ 1 ], sy[ 1 ], sx[ 2 ], sy[ 2 ], sx[ 3 ], sy[ 3 ] };
		draw_list.convex_filled( { poly, 8 }, fill );
		draw_list.polyline( { poly, 8 }, outline, true, 1.0f );
	}

} // namespace features::combat