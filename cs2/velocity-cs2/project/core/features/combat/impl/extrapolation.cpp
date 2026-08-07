#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	void shared::lagcomp::predict_movement( extrapolation_data& data, std::uintptr_t skip_entity ) const
	{
		if ( !addresses::globals::game_trace_manager )
		{
			return;
		}

		const auto sv_gravity = CONVAR ("sv_gravity")->get<float>( );

		if ( data.flags & cstypes::entity_flags::on_ground )
		{
			data.velocity.z = 0.0f;
		}
		else
		{
			data.velocity.z -= sv_gravity * cstypes::tick_interval;
		}

		const auto move_end = data.origin + data.velocity * cstypes::tick_interval;

		auto trace_result = systems::g_tracing.trace_hull(
			data.origin, move_end,
			data.obb_mins, data.obb_maxs,
			skip_entity, 0x1c3003, 4
		);

		if ( trace_result.fraction != 1.0f )
		{
			static auto last_wall_log{ 0 };
			const auto now_tick = memory::read<int>( addresses::globals::global_vars + 0x44 );
			if ( now_tick - last_wall_log > 64 )
			{
				last_wall_log = now_tick;
				logging::console::print( xs( "[extrap] predict_movement: wall hit (frac {:.2f}, normal {:.2f} {:.2f} {:.2f})\n" ),
					trace_result.fraction, trace_result.normal.x, trace_result.normal.y, trace_result.normal.z );
			}

			for ( auto i = 0; i < 2; ++i )
			{
				const auto dot = data.velocity.dot( trace_result.normal );
				data.velocity -= trace_result.normal * dot;

				const auto adjust = data.velocity.dot( trace_result.normal );
				if ( adjust < 0.0f )
				{
					data.velocity -= trace_result.normal * adjust;
				}

				const auto remaining_fraction = 1.0f - trace_result.fraction;
				const auto clip_end = trace_result.end_pos + data.velocity * ( cstypes::tick_interval * remaining_fraction );

				trace_result = systems::g_tracing.trace_hull(
					trace_result.end_pos, clip_end,
					data.obb_mins, data.obb_maxs,
					skip_entity, 0x1c3003, 4
				);

				if ( trace_result.fraction == 1.0f )
				{
					break;
				}
			}
		}

		// The final trace may start at a collision point. Its end position is the
		// clipped destination even when that follow-up trace completes fully.
		data.origin = trace_result.end_pos;

		const auto ground_end = math::vector3{ data.origin.x, data.origin.y, data.origin.z - 2.0f };
		const auto ground_trace = systems::g_tracing.trace_hull(
			data.origin, ground_end,
			data.obb_mins, data.obb_maxs,
			skip_entity, 0x1c3003, 4
		);

		data.flags &= ~cstypes::entity_flags::on_ground;

		if ( ground_trace.fraction != 1.0f && ground_trace.normal.z > 0.7f )
		{
			data.flags |= cstypes::entity_flags::on_ground;
		}
	}

	std::optional<shared::lagcomp::record> shared::lagcomp::extrapolate( std::uintptr_t pawn )
	{
		if ( !settings::g_combat.m_lagcomp.extrapolation.value )
		{
			return std::nullopt;
		}

		std::shared_lock records_lock( this->m_records_mtx );

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.empty( ) )
		{
			return std::nullopt;
		}

		const auto& latest = it->second.front( );
		if ( !latest.is_valid( ) )
		{
			return std::nullopt;
		}

		const auto net_client = addresses::globals::network_client_service;
		if ( !net_client )
		{
			return std::nullopt;
		}

		const auto tick_state = memory::call_vfunc<std::uintptr_t>( net_client, 23 );
		if ( !tick_state )
		{
			return std::nullopt;
		}

		const auto server_tick = memory::read<int>( tick_state + 892 );
		const auto delta_ticks = server_tick - latest.tick;

		if ( delta_ticks <= 0 )
		{
			return std::nullopt;
		}

		const auto& lg = settings::g_combat.m_lagcomp;

		// Manual mode caps the extrapolation window; auto mode follows the
		// full server-visible gap. Both are bounded to the player-tunable
		// 1..18 tick range (see the ragebot menu slider).
		const auto max_extrap = lg.extrapolation_auto.value ? 18 : std::clamp( lg.max_extrapolate_ticks.value, 1, 18 );

		// The window CLAMPS, it does not gate: when the record lag exceeds
		// the window (high latency / dropped packets), giving up leaves only
		// the stale live record - a guaranteed behind-the-body whiff. A
		// window-clamped extrapolation still advances the stale pose up to
		// the server's current position for a steady target, which is the
		// best information available.
		auto ticks_to_extrapolate = std::min( delta_ticks, max_extrap );

		// The server consumes the command at client_tick + latency: by the
		// time it processes this attack the target has moved latency more
		// than the extrapolated pose covers. Extrapolate to that moment,
		// otherwise the shot lands behind the target - the recurring
		// "lag-compensation deviation" whiffs on real servers.
		{
			const auto net_channel = memory::call<std::uintptr_t>( PATTERN( patterns::get_net_channel ), 0, 0 );
			if ( net_channel )
			{
				const auto latency = memory::call_vfunc<float>( net_channel, 10, 0 );
				if ( std::isfinite( latency ) && latency > 0.0f )
				{
					ticks_to_extrapolate += static_cast< int >( std::ceil( latency / cstypes::tick_interval ) );
				}
			}
		}

		ticks_to_extrapolate = std::min( ticks_to_extrapolate, max_extrap );

		auto velocity = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
		auto derived_speed_2d{ 0.0f };

		// Networked velocity is quantized and lags by one packet. When two
		// records are available, derive the planar velocity from their origin
		// delta instead; the direction follows the player's actual movement
		// while the magnitude is clamped against the (lagged) network speed so
		// an extrapolation never runs ahead of the player's real acceleration.
		if ( it->second.size( ) > 1 )
		{
			const auto& prev = it->second[ 1 ];
			if ( prev.valid && latest.valid )
			{
				const auto dt = latest.simulation_time - prev.simulation_time;
				if ( dt > 0.001f && dt < 0.25f )
				{
					const auto derived = ( latest.origin - prev.origin ) / dt;
					derived_speed_2d = std::sqrtf( derived.x * derived.x + derived.y * derived.y );
					const auto net_speed_2d = std::sqrtf( velocity.x * velocity.x + velocity.y * velocity.y );

					if ( derived_speed_2d > 0.001f )
					{
						// Magnitude takes the LARGER of the derived (real
						// record displacement) and network speed. The
						// network field lags one packet and underestimates
						// fast targets (360+ u/s bhop chains), which makes
						// the extrapolation trail behind - shots land behind
						// the target ("lag-compensation deviation"). A
						// slight overshoot is far better than a guaranteed
						// behind-the-body whiff.
						const auto scale = net_speed_2d > derived_speed_2d ? net_speed_2d / derived_speed_2d : 1.0f;

						velocity.x = derived.x * scale;
						velocity.y = derived.y * scale;
					}
				}
			}
		}

		const auto speed = std::sqrtf( velocity.x * velocity.x + velocity.y * velocity.y );

		if ( speed < 0.1f )
		{
			// Logged at most once per second - stationary targets otherwise
			// flood the log on every tick.
			static auto last_stationary_log{ 0 };
			const auto now = memory::read<int>( addresses::globals::global_vars + 0x44 );
			if ( now - last_stationary_log > 64 )
			{
				last_stationary_log = now;
				logging::console::print( xs( "[extrap] {:x} | skip: player stationary (speed {:.2f})\n" ), pawn, speed );
			}
			return std::nullopt;
		}

		// A player cannot out-accelerate their own recorded movement by much
		// within a few ticks; cap the simulated speed slightly above the
		// observed baseline. No hard 320 cap: fast targets (bhop chains
		// reach 360+ u/s) would otherwise be predicted slow and every shot
		// lands behind them - the recurring "lag-compensation deviation"
		// whiffs with 3.4 deg misses at 360+ u/s.
		const auto max_sim_speed = std::max( 320.0f, std::max( speed, derived_speed_2d ) * 1.1f );

		// Speed-adaptive window (auto mode): the prediction error from a
		// small direction drift grows linearly with the extrapolated
		// DISTANCE, not the tick count. Cap the physical distance of the
		// prediction - extreme-speed targets (500+ u/s) get a shorter
		// window so a 1 deg direction error cannot become a 10+ u lateral
		// miss; normal targets are unaffected (their 18-tick distance stays
		// under the cap).
		{
			const auto extrap_speed = std::max( speed, derived_speed_2d );
			constexpr float k_max_extrap_dist{ 130.0f };
			const auto dist_cap_ticks = static_cast< int >( std::floor( k_max_extrap_dist / ( extrap_speed * cstypes::tick_interval ) ) );
			ticks_to_extrapolate = std::min( ticks_to_extrapolate, std::max( 1, std::min( dist_cap_ticks, max_extrap ) ) );
		}

		// Prediction direction comes from the RECORD movement (origin
		// deltas), not the network velocity - the network field lags by
		// one packet and its direction can be stale on turning targets.
		float direction = 0.0f;
		bool have_move_dir = false;
		if ( it->second.size( ) > 1 )
		{
			const auto& prev = it->second[ 1 ];
			if ( prev.valid )
			{
				const auto delta = latest.origin - prev.origin;
				if ( delta.x != 0.0f || delta.y != 0.0f )
				{
					direction = std::atan2f( delta.y, delta.x ) * ( 180.0f / 3.14159265f );
					have_move_dir = true;
				}
			}
		}

		if ( !have_move_dir )
		{
			if ( velocity.x != 0.0f || velocity.y != 0.0f )
			{
				direction = std::atan2f( velocity.y, velocity.x ) * ( 180.0f / 3.14159265f );
			}
		}

		// Turning rate from THREE records: the angle between the two
		// consecutive movement segments is the actual yaw velocity. (The
		// old code measured "network velocity vs last movement segment",
		// which is a stale-field artifact, not a turn - turning targets
		// got either a bogus rate or a >35 deg skip.)
		float direction_change = 0.0f;

		if ( it->second.size( ) > 2 )
		{
			const auto& prev = it->second[ 1 ];
			const auto& older = it->second[ 2 ];
			if ( prev.valid && older.valid )
			{
				const auto seg1 = prev.origin - older.origin;
				const auto seg2 = latest.origin - prev.origin;
				const auto ok1 = seg1.x != 0.0f || seg1.y != 0.0f;
				const auto ok2 = seg2.x != 0.0f || seg2.y != 0.0f;

				if ( ok1 && ok2 )
				{
					const auto dir1 = std::atan2f( seg1.y, seg1.x ) * ( 180.0f / 3.14159265f );
					const auto dir2 = std::atan2f( seg2.y, seg2.x ) * ( 180.0f / 3.14159265f );

					auto angle_diff = dir2 - dir1;
					while ( angle_diff > 180.0f ) angle_diff -= 360.0f;
					while ( angle_diff < -180.0f ) angle_diff += 360.0f;

					// A hard re-direction is not predictable - skip the
					// extrapolation rather than guess a wrong path.
					if ( std::fabsf( angle_diff ) > 35.0f )
					{
						static auto last_dir_log{ 0 };
						const auto now_tick = memory::read<int>( addresses::globals::global_vars + 0x44 );
						if ( now_tick - last_dir_log > 64 )
						{
							last_dir_log = now_tick;
							logging::console::print( xs( "[extrap] {:x} | skip: direction change too large ({:.1f} deg)\n" ), pawn, angle_diff );
						}
						return std::nullopt;
					}

					const auto dt = latest.simulation_time - prev.simulation_time;
					if ( dt > 0.001f )
					{
						direction_change = ( angle_diff / dt ) * cstypes::tick_interval;
					}
				}
			}
		}

		if ( std::fabsf( direction_change ) > 6.0f )
		{
			direction_change = 0.0f;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return std::nullopt;
		}

		const auto collision = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pCollision"_hash ) );
		math::vector3 obb_mins{}, obb_maxs{};

		if ( collision )
		{
			obb_mins = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) );
			obb_maxs = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) );
		}
		else
		{
			obb_mins = { -16.0f, -16.0f, 0.0f };
			obb_maxs = { 16.0f, 16.0f, 72.0f };
		}

		const auto flags = memory::read<std::uint32_t>( pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );

		// Airborne targets are only extrapolated when the option is on.
		if ( !( flags & cstypes::entity_flags::on_ground ) && !settings::g_combat.m_lagcomp.extrapolation_air.value )
		{
			return std::nullopt;
		}

		extrapolation_data data{};
		auto used_cache = false;

		// Incremental path: between ticks the server tick only advances
		// by 1-2, and the movement input (speed / direction) barely
		// changes - the previous simulation state can be advanced by
		// that delta instead of recomputing the whole (up to 18-tick)
		// hull-trace loop from scratch. With a full server this turns
		// hundreds of engine traces per tick into a handful.
		{
			std::lock_guard lock( this->m_extrap_cache_mtx );

			auto it = this->m_extrap_cache.find( pawn );
			if ( it != this->m_extrap_cache.end( ) && it->second.valid )
			{
				const auto& cache = it->second;
				const auto delta = server_tick - cache.server_tick;

				// Only a small forward step may be reused; a larger gap
				// (record flushed / long stall) or a change in the
				// movement input falls back to the full recompute.
				if ( delta >= 1 && delta <= 2
					&& std::fabsf( speed - cache.speed ) < std::max( 20.0f, cache.speed * 0.25f )
					&& std::fabsf( direction - cache.direction ) < 15.0f )
				{
					data = cache.data;
					used_cache = true;

					auto fast_path = false;
					if ( data.flags & cstypes::entity_flags::on_ground )
					{
						const auto full_end = data.origin + data.velocity * ( cstypes::tick_interval * delta );
						const auto full_trace = systems::g_tracing.trace_hull(
							data.origin, full_end,
							data.obb_mins, data.obb_maxs,
							pawn, 0x1c3003, 4
						);

						if ( full_trace.fraction == 1.0f )
						{
							fast_path = true;

							for ( auto i = 0; i < delta; ++i )
							{
								data.direction += cache.direction_change;
								while ( data.direction > 180.0f ) data.direction -= 360.0f;
								while ( data.direction < -180.0f ) data.direction += 360.0f;

								const auto rad = data.direction * ( 3.14159265f / 180.0f );
								const auto current_speed = std::min( std::sqrtf( data.velocity.x * data.velocity.x + data.velocity.y * data.velocity.y ), max_sim_speed );
								data.velocity.x = std::cosf( rad ) * current_speed;
								data.velocity.y = std::sinf( rad ) * current_speed;
								data.velocity.z = 0.0f;

								data.origin += data.velocity * cstypes::tick_interval;
								data.sim_time += cstypes::tick_interval;
							}
						}
					}

					if ( !fast_path )
					{
						for ( auto i = 0; i < delta; ++i )
						{
							data.direction += cache.direction_change;
							while ( data.direction > 180.0f ) data.direction -= 360.0f;
							while ( data.direction < -180.0f ) data.direction += 360.0f;

							const auto rad = data.direction * ( 3.14159265f / 180.0f );
							const auto current_speed = std::min( std::sqrtf( data.velocity.x * data.velocity.x + data.velocity.y * data.velocity.y ), max_sim_speed );
							data.velocity.x = std::cosf( rad ) * current_speed;
							data.velocity.y = std::sinf( rad ) * current_speed;

							data.sim_time += cstypes::tick_interval;

							this->predict_movement( data, pawn );

							if ( lg.extrapolation_correct.value && data.velocity.length_2d( ) < 1.0f && data.velocity.z <= 0.0f )
							{
								break;
							}
						}
					}
				}
			}
		}

		if ( !used_cache )
		{
			data.origin = latest.origin;
			data.velocity = velocity;
			data.obb_mins = obb_mins;
			data.obb_maxs = obb_maxs;
			data.flags = flags;
			data.sim_time = latest.simulation_time;
			data.direction = direction;

			auto fast_path = false;

			// Fast path for grounded targets: gravity is irrelevant and the
			// plan-view motion only changes by the small direction delta, so a
			// single full-length hull trace proves the path is unobstructed and
			// the whole window can be advanced purely mathematically - no
			// per-tick trace (each tick would run up to 4 hull traces).
			if ( flags & cstypes::entity_flags::on_ground )
			{
				const auto full_end = data.origin + data.velocity * ( cstypes::tick_interval * ticks_to_extrapolate );
				const auto full_trace = systems::g_tracing.trace_hull(
					data.origin, full_end,
					data.obb_mins, data.obb_maxs,
					pawn, 0x1c3003, 4
				);

				if ( full_trace.fraction == 1.0f )
				{
					fast_path = true;

					for ( auto i = 0; i < ticks_to_extrapolate; ++i )
					{
						data.direction += direction_change;
						while ( data.direction > 180.0f ) data.direction -= 360.0f;
						while ( data.direction < -180.0f ) data.direction += 360.0f;

						const auto rad = data.direction * ( 3.14159265f / 180.0f );
						const auto current_speed = std::min( std::sqrtf( data.velocity.x * data.velocity.x + data.velocity.y * data.velocity.y ), max_sim_speed );
						data.velocity.x = std::cosf( rad ) * current_speed;
						data.velocity.y = std::sinf( rad ) * current_speed;
						data.velocity.z = 0.0f;

						data.origin += data.velocity * cstypes::tick_interval;
						data.sim_time += cstypes::tick_interval;
					}
				}
			}

			if ( !fast_path )
			{
				for ( auto i = 0; i < ticks_to_extrapolate; ++i )
				{
					data.direction += direction_change;
					while ( data.direction > 180.0f ) data.direction -= 360.0f;
					while ( data.direction < -180.0f ) data.direction += 360.0f;

					const auto rad = data.direction * ( 3.14159265f / 180.0f );
					const auto current_speed = std::min( std::sqrtf( data.velocity.x * data.velocity.x + data.velocity.y * data.velocity.y ), max_sim_speed );
					data.velocity.x = std::cosf( rad ) * current_speed;
					data.velocity.y = std::sinf( rad ) * current_speed;

					data.sim_time += cstypes::tick_interval;

					this->predict_movement( data, pawn );

					// Correction: when the predicted path is fully blocked (the target
					// stops against a wall or another entity), further ticks would
					// drag the pose into cover. Stop early so the extrapolated record
					// stays on the last reachable position.
					if ( lg.extrapolation_correct.value && data.velocity.length_2d( ) < 1.0f && data.velocity.z <= 0.0f )
					{
						break;
					}
				}
			}
		}

		const auto origin_delta = data.origin - latest.origin;

		// No displacement (stopped target) still returns a usable pose: the
		// record is stamped with the current tick so it stays eligible even
		// when the real record window has expired - otherwise a target whose
		// records fell outside the unlag budget would never be fired at.
		if ( origin_delta.length_sqr( ) < 0.01f )
		{
			static auto last_unchanged_log{ 0 };
			const auto now_tick = memory::read<int>( addresses::globals::global_vars + 0x44 );
			if ( now_tick - last_unchanged_log > 64 )
			{
				last_unchanged_log = now_tick;
				logging::console::print( xs( "[extrap] {:x} | skip: predicted origin unchanged after {} ticks\n" ), pawn, ticks_to_extrapolate );
			}

			record fallback = latest;
			fallback.tick = g_shared.ctx( ).current_tick;
			fallback.simulation_time = g_shared.ctx( ).current_time;
			fallback.extrapolated = true;

			// Keep the cache fresh even when nothing moved, so the next
			// tick's small server-tick delta can reuse the state.
			{
				std::lock_guard lock( this->m_extrap_cache_mtx );
				auto& entry = this->m_extrap_cache[ pawn ];
				entry.data = data;
				entry.server_tick = server_tick;
				entry.speed = speed;
				entry.direction = direction;
				entry.direction_change = direction_change;
				entry.valid = true;
			}

			return fallback;
		}

		record extrap_record = latest;
		extrap_record.origin = data.origin;
		extrap_record.simulation_time = data.sim_time;
		// The pose is predicted forward to "now": stamp the record with the
		// current tick so backtrack displays 0t and the attack timestamp
		// (and the spread seed) aligns with the server's current-pose
		// resolution. Deriving it from the lagging record list left the
		// record showing a stale rewind (e.g. bt 18t).
		extrap_record.tick = g_shared.ctx( ).current_tick;
		extrap_record.extrapolated = true;

		// The model must turn with the predicted path, or the hitbox capsules
		// keep the pre-turn orientation while the origin slides sideways.
		auto yaw_change = data.direction - direction;
		while ( yaw_change > 180.0f ) yaw_change -= 360.0f;
		while ( yaw_change < -180.0f ) yaw_change += 360.0f;

		const auto yaw_rad = yaw_change * ( 3.14159265f / 180.0f );
		const auto cos_y = std::cosf( yaw_rad );
		const auto sin_y = std::sinf( yaw_rad );

		for ( auto i = 0; i < extrap_record.bone_count && i < 128; ++i )
		{
			// Rotate around the latest origin first, then translate to the
			// predicted origin.
			const auto local_x = extrap_record.bones[ i ].position.x - latest.origin.x;
			const auto local_y = extrap_record.bones[ i ].position.y - latest.origin.y;

			extrap_record.bones[ i ].position.x = data.origin.x + local_x * cos_y - local_y * sin_y;
			extrap_record.bones[ i ].position.y = data.origin.y + local_x * sin_y + local_y * cos_y;
			extrap_record.bones[ i ].position.z = data.origin.z + ( extrap_record.bones[ i ].position.z - latest.origin.z );
		}

		extrap_record.rotation.y += yaw_change;

		const auto dist = std::sqrtf( origin_delta.x * origin_delta.x + origin_delta.y * origin_delta.y + origin_delta.z * origin_delta.z );

		// Throttle: successful extrapolations run every tick for every
		// candidate, which floods the log.
		{
			static auto last_extrap_log{ 0 };
			const auto now = memory::read<int>( addresses::globals::global_vars + 0x44 );
			if ( now - last_extrap_log > 64 )
			{
				last_extrap_log = now;
				logging::console::print(
					xs( "[extrap] {:x} | ok: {} ticks | delta {:.2f} u | origin ({:.1f}, {:.1f}, {:.1f})\n" ),
					pawn, ticks_to_extrapolate, dist,
					data.origin.x, data.origin.y, data.origin.z
				);
			}
		}

		// Cache the simulation state for the next tick's incremental step.
		{
			std::lock_guard lock( this->m_extrap_cache_mtx );
			auto& entry = this->m_extrap_cache[ pawn ];
			entry.data = data;
			entry.server_tick = server_tick;
			entry.speed = speed;
			entry.direction = direction;
			entry.direction_change = direction_change;
			entry.valid = true;
		}

		return extrap_record;
	}

} // namespace features::combat
