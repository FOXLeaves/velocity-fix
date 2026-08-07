#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <utilities/random/random.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

// Ragebot target scanning and evaluation: per-player hitbox scans,
// hitchance evaluation and best-target selection, split out of rage.cpp.
namespace features::combat {
	std::vector<rage::scan_hit> rage::scan_players( const math::vector3& eye, float inaccuracy, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		diag::set_exception_phase( "rage: scan" );

		const auto candidate_count = static_cast< int >( candidates.size( ) );
		std::vector<std::vector<scan_hit>> per_candidate( candidates.size( ) );

		// Trace budget: every scan point costs an engine trace_bullet
		// (the hottest ragebot cost). With a full server the unthrottled
		// scan runs hundreds of them per tick and the frame time spikes.
		// The budget caps the total per scan pass - full scans for a few
		// targets, and a fixed upper bound for crowded servers.
		std::atomic<int> trace_budget{ k_max_scan_traces };

		const auto scan_range = [ & ]( int begin, int end )
			{
				for ( auto ci = begin; ci < end; ++ci )
				{
					auto& cand = candidates[ ci ];
					auto& candidate_hits = per_candidate[ ci ];
					candidate_hits.reserve( 24 );

					for ( auto ri = 0; ri < cand.record_count; ++ri )
					{
						if ( !cand.records[ ri ] || !cand.records[ ri ]->valid )
						{
							continue;
						}

						auto hits = this->scan_player( eye, inaccuracy, ctx, cand, cand.records[ ri ], local, trace_budget );
						const auto has_direct_hit = std::any_of( hits.begin( ), hits.end( ), [ ]( const scan_hit& hit )
							{
								return !hit.penetrated;
							} );

						for ( auto& h : hits )
						{
							candidate_hits.push_back( std::move( h ) );
						}

						// A viable shot on the newest record is both more reliable and
						// cheaper than evaluating historical poses for the same target.
						if ( has_direct_hit )
						{
							break;
						}

						// Budget exhausted: stop scanning further records and
						// candidates - the remaining budget is better spent
						// keeping the scan pass short.
						if ( trace_budget.load( std::memory_order_relaxed ) <= 0 )
						{
							return;
						}
					}
				}
			};

		// Parallel scan across candidates (the old build ran this through
		// the thread pool; the serial rewrite serialized hundreds of engine
		// traces per tick onto the create_move thread and dropped frames
		// with several enemies on screen).
		if ( candidate_count > 1 )
		{
			threadpool::parallel_for( 0, candidate_count, scan_range, 1 );
		}
		else
		{
			scan_range( 0, candidate_count );
		}

		std::vector<scan_hit> flat;
		auto total_hits{ std::size_t{} };
		for ( const auto& hits : per_candidate )
		{
			total_hits += hits.size( );
		}
		flat.reserve( total_hits );

		for ( auto& v : per_candidate )
		{
			for ( auto& h : v )
			{
				flat.push_back( std::move( h ) );
			}
		}

		return flat;
	}

	std::vector<rage::scan_hit> rage::scan_player( const math::vector3& eye, float inaccuracy, const aim_context& ctx, candidate& cand, shared::lagcomp::record* record, const systems::local::snapshot& local, std::atomic<int>& trace_budget ) const
	{
		const auto& skeleton = record->bones;
		const auto& hitbox_set = cand.hitboxes;
		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		const auto pen_ctx = g_shared.pen( ).prepare_target( cand.pawn, record );

		// Double tap: the claim-aligned pose is the one the server rewinds
		// the target to, and it may sit off the CURRENT view (the target
		// moved between the claimed tick and now). FOV-culling it against
		// the live view would leave only phantom "direct hits" on poses
		// the server never resolves - so the aligned pose is exempt from
		// the FOV gate entirely; the aim angle is carried by the entry
		// (silent), not by the view. 2-tick tolerance matches the score
		// gate (claim jitter ±1 around the sync).
		const auto dt_align_record = settings::g_combat.m_ragebot.m_double_tap.enabled.value
			&& std::abs( record->tick - const_cast<rage*>( this )->m_double_tap.claimed_tick( ) ) <= 2;
		const auto fov_limit = dt_align_record ? 180.0f : config.max_fov;

		if ( hitbox_set.count <= 0 )
		{
			return {};
		}

		// Scan order: head first, then limbs, then the torso - the torso
		// multipoints are the reliable fallback, so they go last.
		std::array<int, 20> scan_order{};
		auto scan_count{ 0 };

		for ( auto idx : { 0 } )
		{
			scan_order[ scan_count++ ] = idx;
		}

		for ( auto idx : { 13, 14, 15, 16, 17, 18 } )
		{
			scan_order[ scan_count++ ] = idx;
		}

		for ( auto idx : { 7, 8, 9, 10 } )
		{
			scan_order[ scan_count++ ] = idx;
		}

		for ( auto idx : { 11, 12 } )
		{
			scan_order[ scan_count++ ] = idx;
		}

		for ( auto idx : { 4, 5, 6, 3, 2 } )
		{
			scan_order[ scan_count++ ] = idx;
		}

		std::vector<scan_hit> results;
		results.reserve( static_cast< std::size_t >( scan_count ) * 2 );

		// Index lookup table for the hitbox set (linear search per hitbox
		// was O(count^2) on every scan).
		std::array<const systems::hitboxes::entry*, 20> hb_by_index{};
		for ( const auto& entry : hitbox_set )
		{
			if ( entry.index >= 0 && entry.index < static_cast< int >( hb_by_index.size( ) ) )
			{
				hb_by_index[ entry.index ] = &entry;
			}
		}

		// Per-hitbox two-phase scan: the center point is traced first, and
		// when it is sufficient (direct hit, damage floor met) the whole
		// multipoint set for that hitbox is skipped - no multipoint
		// generation, no extra trace_bullet calls. This is the dominant
		// ragebot cost (one engine trace per scan point), so direct-hit
		// scenarios drop to a fraction of the old trace count.
		for ( auto idx = 0; idx < scan_count; ++idx )
		{
			const auto hitbox_index = scan_order[ idx ];
			const systems::hitboxes::entry* hb = hitbox_index >= 0 && hitbox_index < static_cast< int >( hb_by_index.size( ) )
				? hb_by_index[ hitbox_index ]
				: nullptr;

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

			// Center FOV pre-filter: a multipoint is never closer to the
			// view than its own center, so a center outside the FOV skips
			// the whole hitbox - no multipoint generation, no traces.
			const auto center_aim = math::helpers::calculate_angle( eye, center );
			if ( math::helpers::angle_distance( ctx.view_angles, center_aim ) > fov_limit )
			{
				continue;
			}

			const auto process_point = [ & ]( const math::vector3& position, bool is_center, bool& out_sufficient ) -> bool
				{
					out_sufficient = false;

					const auto aim = math::helpers::calculate_angle( eye, position );
					const auto fov = math::helpers::angle_distance( ctx.view_angles, aim );

					if ( fov > fov_limit )
					{
						return false;
					}

					shared::penetration::result pen{};
					// Consume one trace from the shared budget before the
					// engine call - the hottest per-point cost.
					if ( trace_budget.fetch_sub( 1, std::memory_order_relaxed ) <= 0 )
					{
						return false;
					}

					if ( !g_shared.pen( ).run( eye, position, pen_ctx, local.pawn, local.team, pen ) )
					{
						return false;
					}

					// Head hits that reach the hitbox through penetration frequently
					// land on a different hitgroup (neck/generic) once the wall drags
					// the trajectory off the capsule. Re-derive the damage from the
					// actual hitgroup instead of trusting the head figure.
					if ( hitbox_index == 0 && pen.hitgroup != systems::g_hitboxes.hitgroup_from_hitbox( hitbox_index ) )
					{
						const auto& weapon_data = g_shared.pen( ).get_weapon_data( );
						if ( weapon_data.headshot_multiplier > 1.0f )
						{
							pen.damage = pen.damage / weapon_data.headshot_multiplier;
						}
						else
						{
							return false;
						}
					}

					if ( pen.damage < cand.min_damage )
					{
						return false;
					}

					// A direct (non-penetrated) hit with lethal damage is as
					// good as this hitbox gets - the caller can stop tracing
					// further multipoints on it.
					out_sufficient = !pen.penetrated || pen.damage >= static_cast< float >( cand.health );

					scan_hit h{};
					h.position = position;
					h.aim_angle = aim;
					h.damage = pen.damage;
					h.fov = fov;
					h.hitbox_index = hitbox_index;
					h.hitgroup = pen.hitgroup;
					h.bone_index = hb->bone;
					h.hitbox = *hb;
					h.is_center = is_center;
					h.penetrated = pen.penetrated;
					h.pawn = cand.pawn;
					h.health = cand.health;
					h.target_velocity = cand.velocity;
					h.record = record;

					results.push_back( std::move( h ) );
					return true;
				};

			if ( config.debug_multipoints.value )
			{
				std::lock_guard lock( m_debug_mtx );
				m_debug_points.push_back( { center, hitbox_index, true } );
			}

			// Multipoints first: they cover far more of the hitbox than the
			// center alone, so a shootable point is found sooner - faster
			// lock-on and more hits on moving/edge cases. A direct hit with
			// lethal damage ends the hitbox scan early - further points
			// cannot improve on it, and every skipped trace_bullet call is
			// a big FPS win (the engine trace is the hottest ragebot cost).
			//
			// Stationary targets skip the edge multipoints entirely: the
			// client-side ray vs the server's capsule resolution carry a
			// small implementation difference, and an edge point that hugs
			// the capsule surface can sit just outside on the server side -
			// the geometric CENTER is the point that is guaranteed inside
			// no matter the resolution delta, so it wins for a standing
			// target. Moving targets keep the multipoints (they absorb the
			// pose/velocity residual).
			if ( config.pointscale > 0.0f && cand.velocity.length_2d( ) >= 5.0f )
			{
				const auto mps = this->generate_multipoints( *hb, center, bone.rotation, config.pointscale, eye, inaccuracy );

				for ( const auto& mp : mps )
				{
					if ( ( center - mp ).length_sqr( ) < 0.01f )
					{
						continue;
					}

					if ( config.debug_multipoints.value )
					{
						std::lock_guard lock( m_debug_mtx );
						m_debug_points.push_back( { mp, hitbox_index, false } );
					}

					bool sufficient = false;
					if ( process_point( mp, false, sufficient ) && sufficient )
					{
						break;
					}
				}
			}

			// Center point: geometric fallback, always scanned so the
			// best point wins on score in select_best.
			bool center_sufficient = false;
			process_point( center, true, center_sufficient );
		}

		return results;
	}

	rage::target rage::select_best( const aim_context& aim_ctx, const std::vector<scan_hit>& hits, float eval_inaccuracy ) const
	{		diag::set_exception_phase( "rage: select_best" );

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

		// Group by record pointer with a hash map instead of a linear scan;
		// scans commonly produce 100+ hits across a dozen records.
		std::unordered_map<shared::lagcomp::record*, std::vector<int>> grouped;
		grouped.reserve( 16 );

		for ( auto i = 0; i < static_cast< int >( hits.size( ) ); ++i )
		{
			auto& indices = grouped[ hits[ i ].record ];
			if ( indices.empty( ) )
			{
				indices.reserve( 16 );
			}

			indices.push_back( i );
		}

		std::vector<record_group> groups;
		groups.reserve( grouped.size( ) );
		for ( auto& [ record, indices ] : grouped )
		{
			groups.push_back( { record, std::move( indices ) } );
		}

		constexpr auto top_k_per_record{ 8 };

		auto cheap_score = [ & ]( const scan_hit& h ) -> float
			{
				const auto lethal_bonus = h.damage * 0.9f >= static_cast< float >( h.health ) ? 100000.0f : 0.0f;
				const auto direct_bonus = h.penetrated ? 0.0f : 5000.0f;
				const auto center_bonus = h.is_center ? 2000.0f : 0.0f;

				return lethal_bonus + direct_bonus + center_bonus + h.damage * 20.0f +
					static_cast< float >( hitgroup_priority( h.hitbox_index ) ) * 10.0f - h.fov;
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
		};

		std::vector<evaluated_hit> evaluated;
		evaluated.reserve( hits.size( ) );

		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		const auto needed_hc = config.hitchance_override.value
			? static_cast< float >( config.hitchance_override_value ) / 100.0f
			: static_cast< float >( config.hitchance ) / 100.0f;

		for ( auto& group : groups )
		{
			if ( !group.record || !group.record->valid )
			{
				continue;
			}

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

			const auto& bone = group.record->bones[ h.bone_index ];
			float hc{ 1.0f };
			if ( !config.no_spread.value )
			{
				// Adaptive sampling with threshold focus: 256 samples keep
				// the common case fast, and when the estimate lands near
				// the configured threshold it re-checks with 512 so the
				// pass/fail decision does not flutter tick-to-tick from
				// sampling noise (that flutter read as hesitant firing).
				const auto dist = ( h.source_eye.position - h.position ).length( );
				auto samples = dist < 900.0f ? 256 : dist < 2200.0f ? 256 : 512;
				hc = g_shared.calculate_hitchance( h.source_eye.position, h.aim_angle, h.hitbox, bone, eval_inaccuracy, aim_ctx.spread, samples );
				if ( std::fabsf( hc - needed_hc ) < 0.05f && samples < 512 )
				{
					hc = g_shared.calculate_hitchance( h.source_eye.position, h.aim_angle, h.hitbox, bone, eval_inaccuracy, aim_ctx.spread, 512 );
				}
			}
			const auto hp = static_cast< float >( h.health );
			const auto can_kill = h.damage >= hp;
				// The head is the lethal hitbox: its small capsule caps the
				// reachable sampled hitchance far below the body's, so a
				// strict threshold made ragebot wait forever on visible
				// heads. When the head is the only lethal point (full-hp
				// target + 100/force-lethal damage floor) the tolerance is
				// widened to 25% - a visible head that can kill fires
				// instead of being stared at.
				const auto is_head = systems::g_hitboxes.hitgroup_from_hitbox( h.hitbox_index ) == 1;
				const auto head_tolerance = is_head ? ( can_kill ? 0.25f : 0.10f ) : 0.0f;
				const auto passes_hitchance = config.no_spread.value || hc >= needed_hc - 0.01f - head_tolerance;
				auto score = passes_hitchance ? 1000000.0f : 0.0f;

				if ( can_kill )
				{
					score += 100000.0f + hc * 10000.0f;
				}
				else
				{
					score += h.damage * hc * 100.0f + h.damage * 5.0f;
				}

				score += h.penetrated ? 0.0f : 250.0f;
				// Center points are the most stable aim reference during
				// double tap (the rewound pose carries a residual offset,
				// so a center point stays inside while an edge multipoint
				// may not) - boost them under DT.
				score += h.is_center ? ( settings::g_combat.m_ragebot.m_double_tap.enabled.value ? 200.0f : 50.0f ) : 0.0f;
				score += static_cast< float >( hitgroup_priority( h.hitbox_index ) ) * 2.0f;
				score -= h.fov * 0.1f;

				// Pose priority, velocity-aware: the extrapolated prediction
				// is the closest match to the pose the server resolves
				// against for a moving target, so it gets a strong edge
				// there - the shot fires on the predicted current pose
				// instead of waiting for the lagged live body. For
				// stationary targets (no extrapolated record exists) the
				// live pose is authoritative. Rewind poses keep their age
				// penalty as a last resort.
				//
				// Plain (non-DT) ragebot resolves the shot against the
				// LOCAL current tick (fire_gun stamps the local
				// interpolated tick), so the pose MUST be the current one:
				// extrapolated (predicted current) or the newest live
				// record. A rewound pose's "direct hit / lethal" is a
				// phantom - the server never rewinds to it - so the
				// freshness term dominates every other score component
				// (lethal +100k included) and stale poses are penalized
				// hard instead of winning on fake damage.
				//
				// Double tap overrides the priority: every attack claims
				// the weapon-ready tick on the input history, and the
				// server rewinds the TARGET to that claimed tick. The
				// pose matching the claim is the only one the server will
				// resolve the shot against - anything else (live/extrap)
				// makes the rewound body drift off the aimed point and
				// both shots of the pair miss. The alignment bonus must
				// dominate every other score term (lethal +100k, direct
				// +5k): a "direct hit" on a non-aligned pose is a phantom
				// - the server never rewinds to it - so it must never win.
				if ( settings::g_combat.m_ragebot.m_double_tap.enabled.value && h.record )
				{
					const auto claim_tick = const_cast<rage*>( this )->m_double_tap.claimed_tick( );
					const auto align = std::abs( h.record->tick - claim_tick );
					// Tolerance of 2 ticks: the record grid is one tick and
					// the claimed tick jitters ±1 around the sync, so a
					// stricter gate would swap the aligned record tick to
					// tick and the aim would jump between poses. Within the
					// tolerance the alignment dominates everything.
					score += align <= 2 ? 200000.0f : -static_cast< float >( align ) * 5000.0f;
				}
				else if ( h.record && h.record->extrapolated )
				{
					// Predicted current pose - the closest match to the
					// current-tick resolution.
					score += 150000.0f;
				}
				else if ( h.record )
				{
					const auto age = g_shared.ctx( ).current_tick - h.record->tick;
					if ( age <= 1 )
					{
						// Newest live record ≈ current pose.
						score += 150000.0f;
					}
					else
					{
						// Rewound pose: the server resolves the current
						// tick, this pose is a phantom - never let a fake
						// "direct hit" on it win.
						score -= static_cast< float >( age ) * 5000.0f;
					}
				}

				evaluated.push_back( evaluated_hit{ idx, hc, score } );
			}
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

			auto is_better = !best.valid || e.score > best.score;
			if ( best.valid && std::fabsf( e.score - best.score ) < 0.01f )
			{
				if ( h.record->tick != best.hit.record->tick )
				{
					is_better = h.record->tick > best.hit.record->tick;
				}
				else if ( h.is_center != best.hit.is_center )
				{
					is_better = h.is_center;
				}
				else
				{
					is_better = h.fov < best.hit.fov;
				}
			}

			// Target-switch debounce: when several targets are viable and
			// their scores are close, keep aiming at the pawn selected on
			// the previous tick. Re-aiming at a different target every tick
			// re-aligns pose/claim each time and tanks the hit rate during
			// a multi-target fight; a clearly better score still wins.
			if ( best.valid && e.score > best.score - 800.0f && h.pawn == this->m_last_select_pawn )
			{
				is_better = true;
			}

			if ( is_better )
			{
				best.hit = h;
				best.hitchance = e.hitchance;
				best.score = e.score;
				best.valid = true;
			}
		}

		this->m_last_select_pawn = best.valid ? best.hit.pawn : 0;

		// Double tap empty-shot guard: the PREDICTED claim (second-shot
		// window, sync not yet arrived) sits ahead of the record grid - if
		// no recorded pose is within 2 ticks of it, the server would rewind
		// the target to a pose we never aimed at and the shot is guaranteed
		// to whiff, so wait for the pose instead. Normal/stale claims (the
		// weapon has been ready for a long time, the claim is an old tick)
		// must never be gated - the server legitimately rewinds to that old
		// pose and the record list simply does not span that far.
		if ( settings::g_combat.m_ragebot.m_double_tap.enabled.value && best.valid
			&& const_cast<rage*>( this )->m_double_tap.claim_is_predicted( ) )
		{
			const auto claim_tick = const_cast<rage*>( this )->m_double_tap.claimed_tick( );
			const auto align = std::abs( best.hit.record->tick - claim_tick );
			if ( align > 2 )
			{
				return target{};
			}
		}

		return best;
	}

	float rage::evaluate_hitchance( const scan_hit& hit, const aim_context& ctx, float inaccuracy ) const
	{
		if ( !hit.record || !hit.record->valid || hit.bone_index < 0 || hit.bone_index >= 28 )
		{
			return 0.0f;
		}

		return g_shared.calculate_hitchance( hit.source_eye.position, hit.aim_angle, hit.hitbox, hit.record->bones[ hit.bone_index ], inaccuracy, ctx.spread );
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

	std::vector<math::vector3> rage::generate_multipoints( const systems::hitboxes::entry& hitbox, const math::vector3& center, const math::quaternion& bone_rot, float pointscale, const math::vector3& shoot_pos, float inaccuracy ) const
	{
		std::vector<math::vector3> out;

		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );

		auto scale = std::clamp( pointscale / 100.0f, 0.0f, 1.0f );
		const auto auto_mode = scale <= 0.01f;

		// Cone-based automatic scale. Used when the point-scale slider is at
		// 0 (auto multipoint); the extra point sets follow the final scale.
		float auto_scale{ 0.5f };
		if ( hitbox.radius > 0.001f )
		{
			const auto cone = std::max( inaccuracy + g_shared.ctx( ).spread, 0.0f );
			const auto cone_radius = std::tanf( cone ) * ( center - shoot_pos ).length( );
			auto_scale = std::clamp( 0.9f - cone_radius / hitbox.radius, 0.0f, 1.0f );
		}

		if ( auto_mode )
		{
			// Slider at 0 = automatic multipoint scale.
			scale = auto_scale;
			if ( scale <= 0.01f )
			{
				return out;
			}
		}
		else if ( config.dynamic_pointscale.value && hitbox.radius > 0.001f )
		{
			scale = std::min( scale, auto_scale );

			if ( scale <= 0.01f )
			{
				return out;
			}
		}

		// Aim-point inset: the recorded/rewound pose always carries a
		// small residual offset from what the server resolves, so aim
		// points are pulled inward to stay safely inside the hitbox
		// capsule - a surface-hugging point can sit just outside on the
		// server side and the shot tickles the edge. The head capsule is
		// small, so it gets an extra margin. Double tap (rewound pose)
		// insets deeper than the plain ragebot (live pose).
		{
			const auto dt_active = settings::g_combat.m_ragebot.m_double_tap.enabled.value;
			const auto limit = dt_active ? ( hitbox.index == 0 ? 0.7f : 0.75f ) : 0.8f;
			scale = std::min( scale, limit );
		}

		// Build a view-relative frame so points rotate correctly above and below us.
		const auto hb_mid   = ( hitbox.mins + hitbox.maxs ) * 0.5f;
		const auto capsule_a = center + bone_rot.rotate_vector( hitbox.mins - hb_mid );
		const auto capsule_b = center + bone_rot.rotate_vector( hitbox.maxs - hb_mid );
		const auto shoot_dir = ( center - shoot_pos ).normalized( );
		const auto ang       = math::helpers::vector_to_angle( shoot_dir );

		math::vector3 left{}, up{};
		math::helpers::angle_vectors_left( ang, nullptr, &left, &up );

		// angle_vectors_left returns left, so negate it for right.
		const auto right = math::vector3{ -left.x, -left.y, -left.z };

		// Trace from outside through the center to find the real capsule surface.
		// This is accurate around rounded end caps, where radius offsets are not.
		const auto surface_point = [ & ]( const math::vector3& direction ) -> math::vector3
		{
			const auto dir = direction.normalized( );

			if ( hitbox.radius > 0.001f )
			{
				const auto reach = ( capsule_b - capsule_a ).length( ) + hitbox.radius * 2.0f + 1.0f;
				const auto origin = center + dir * reach;
				const auto delta = dir * ( reach * -2.0f );
				auto fraction{ 1.0f };

				if ( g_shared.ray_vs_capsule( origin, delta, capsule_a, capsule_b, hitbox.radius, fraction ) )
				{
					return origin + delta * fraction;
				}
			}
			else
			{
				// Intersect box hitboxes in bone space using their directional support.
				auto inverse = bone_rot;
				inverse.x = -inverse.x;
				inverse.y = -inverse.y;
				inverse.z = -inverse.z;
				const auto local_dir = inverse.rotate_vector( dir );
				const auto extents = ( hitbox.maxs - hitbox.mins ) * 0.5f;
				auto distance = 8192.0f;

				if ( std::fabs( local_dir.x ) > 1.0e-6f ) distance = std::min( distance, std::fabs( extents.x / local_dir.x ) );
				if ( std::fabs( local_dir.y ) > 1.0e-6f ) distance = std::min( distance, std::fabs( extents.y / local_dir.y ) );
				if ( std::fabs( local_dir.z ) > 1.0e-6f ) distance = std::min( distance, std::fabs( extents.z / local_dir.z ) );

				if ( distance < 8192.0f )
				{
					return center + dir * distance;
				}
			}

			return center;
		};

		const auto scaled_surface = [ & ]( const math::vector3& direction )
		{
			const auto surface = surface_point( direction );
			return center + ( surface - center ) * scale;
		};

		// Extra-point sets carry their own scale (0 disables the set).
		const auto scaled_surface_at = [ & ]( const math::vector3& direction, float s )
		{
			const auto surface = surface_point( direction );
			return center + ( surface - center ) * s;
		};

		switch ( hitbox.index )
		{
		case 0: // head
		{
			out.reserve( 4 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );
			out.push_back( scaled_surface( up ) );
			out.push_back( scaled_surface( -up ) );

			// Extra set (toggle + scale slider): the four diagonal corners
			// plus the face/back surface points for a denser head sample.
			// Scale 0 = auto (follows the point scale).
			if ( config.extra_head_points.value )
			{
				const auto extra = config.extra_head_scale.value <= 0
					? scale
					: std::clamp( static_cast< float >( config.extra_head_scale.value ) / 100.0f, 0.0f, 1.0f );
				out.push_back( scaled_surface_at( ( right + up ).normalized( ), extra ) );
				out.push_back( scaled_surface_at( ( right - up ).normalized( ), extra ) );
				out.push_back( scaled_surface_at( ( -right + up ).normalized( ), extra ) );
				out.push_back( scaled_surface_at( ( -right - up ).normalized( ), extra ) );
				out.push_back( scaled_surface_at( shoot_dir, extra ) );
				out.push_back( scaled_surface_at( -shoot_dir, extra ) );
			}
			break;
		}

		case 2: case 3: // stomach / pelvis
		{
			out.reserve( 2 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );

			if ( config.extra_body_points.value )
			{
				const auto extra = config.extra_body_scale.value <= 0
					? scale
					: std::clamp( static_cast< float >( config.extra_body_scale.value ) / 100.0f, 0.0f, 1.0f );
				out.push_back( scaled_surface_at( up, extra ) );
				out.push_back( scaled_surface_at( -up, extra ) );
			}
			break;
		}

		case 4: case 5: case 6: // chest
		{
			out.reserve( 3 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );
			if ( hitbox.index == 6 )
			{
				out.push_back( scaled_surface( up ) );
			}

			if ( config.extra_body_points.value )
			{
				const auto extra = config.extra_body_scale.value <= 0
					? scale
					: std::clamp( static_cast< float >( config.extra_body_scale.value ) / 100.0f, 0.0f, 1.0f );
				out.push_back( scaled_surface_at( up, extra ) );
				out.push_back( scaled_surface_at( -up, extra ) );
				if ( hitbox.index == 6 )
				{
					out.push_back( scaled_surface_at( ( right + up ).normalized( ), extra ) );
					out.push_back( scaled_surface_at( ( -right + up ).normalized( ), extra ) );
				}
				out.push_back( scaled_surface_at( shoot_dir, extra ) );
				out.push_back( scaled_surface_at( -shoot_dir, extra ) );
			}
			break;
		}

		case 7: case 8: case 9: case 10: case 11: case 12: // legs / feet
		{
			out.reserve( 2 );
			out.push_back( capsule_a );
			out.push_back( capsule_b );
			break;
		}

		case 13: case 14: case 15: case 16: case 17: case 18: // arms
		{
			out.reserve( 1 );
			out.push_back( capsule_b );
			break;
		}

		default:
		{
			out.reserve( 2 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );
			break;
		}
		}

		return out;
	}

} // namespace features::combat
