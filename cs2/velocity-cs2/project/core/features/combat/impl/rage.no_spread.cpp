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

// No-spread module: the eye-candidate scan helper, the no-spread scan
// branch and the seed-correction pass, split out of rage.cpp. The
// double-tap linkage is preserved through the rage instance (claimed_tick
// feeds the seed bucket, set_no_spread_claim_tick stamps the entries).
namespace features::combat {

	std::vector<rage::scan_hit> rage::scan_from_eye_candidates( const shared::shoot_history::eye_candidates& eye_candidates, std::vector<candidate>& scan_set, const math::vector3& eye_offset, float inaccuracy, int max_traces, const aim_context& ctx, const systems::local::snapshot& local )
	{
		std::vector<scan_hit> hits_out;

		for ( auto i = 0; i < eye_candidates.count; ++i )
		{
			const auto eye = eye_candidates.entries[ i ].position + eye_offset;
			// Secondary eye candidates are only reached when the
			// primary found no direct hit - halve their budget so the
			// fallback pass cannot double the per-tick trace cost.
			const auto pass_budget = i == 0 ? max_traces : max_traces / 2;
			auto hits = this->scan_players( eye, inaccuracy, ctx, scan_set, local, pass_budget );
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
	}

	bool rage::run_no_spread( systems::input::usercmd* cmd, const aim_context& ctx, std::vector<candidate>& candidates, const shared::shoot_history::eye_candidates& eye_candidates, const systems::local::snapshot& local, bool allow_fire )
	{
		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		auto& shared_ctx = g_shared.ctx( );

		// build_context already sampled the post-command (predicted)
		// accuracy state - re-reading the live getter here costs a second
		// engine accuracy call per tick. The predicted value is what the
		// server sees when it consumes the shot, so reusing it keeps every
		// scan/seed verdict intact while removing that redundant call.
		shared_ctx.inaccuracy = ctx.predicted_inaccuracy;
		auto all_hits = this->scan_from_eye_candidates( eye_candidates, candidates, {}, shared_ctx.inaccuracy, k_max_scan_traces, ctx, local );

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

		this->m_double_tap.update_aim( best.hit.aim_angle - g_shared.get_aim_punch( local.pawn ), g_shared.ctx( ).current_tick );

		this->update_auto_scope( cmd, true );

		// Seed mode carries only a small, legal spread correction - fire
		// it from a stop like the regular path, otherwise the moving
		// spread floor can exceed the correction and the shot misses.
		if ( settings::g_combat.m_ragebot.no_spread_mode.value == settings::combat::ragebot::no_spread_mode::seed
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

	bool rage::apply_no_spread( math::vector3& aim_angle, const target& tgt, const math::vector3& shoot_eye, int stamp_tick, int tick_base )
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );

		// Seed mode is independent of the no-spread switch: it never
		// touches the view angles (zero compensation), it only verifies
		// the server-side spread seed lands on the hitbox naturally. That
		// is fully legal on servers that reject no-spread, so the mode
		// stays usable with the switch off - in that case a scattered
		// seed simply drops the shot (no compensation fallback, since
		// corrected angles are not allowed there).
		const auto seed_mode = settings::g_combat.m_ragebot.no_spread_mode.value == settings::combat::ragebot::no_spread_mode::seed;

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
					return false;
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
					if ( settings::g_combat.m_ragebot.no_spread.value )
					{
						const auto corrected = g_shared.find_spread_correction( aim_angle, claim_tick, seed );
						if ( corrected.x == 0.0f && corrected.y == 0.0f && corrected.z == 0.0f )
						{
							this->m_firing_this_tick = false;
							return false;
						}

						aim_angle = corrected;
					}
					else
					{
						this->m_firing_this_tick = false;
						return false;
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
					return false;
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
					corrected = g_shared.find_spread_correction( aim_angle, claim_tick, seed );
					if ( corrected.x == 0.0f && corrected.y == 0.0f && corrected.z == 0.0f )
					{
						this->m_firing_this_tick = false;
						return false;
					}
				}

				aim_angle = corrected;

				// Trajectory verification: the bucket match above only
				// guarantees the server derives the SAME spread seed from
				// the corrected angle - it does not guarantee the
				// corrected bullet geometrically crosses the hitbox. The
				// pitch-lift + roll compensation is an approximation, so
				// verify the actual bullet direction (corrected angle +
				// that seed's spread) against the RECORD pose capsule the
				// server rewinds to. A miss here means the shot would
				// whiff - drop it instead of firing a guaranteed miss.
				if ( tgt.hit.bone_index >= 0 && tgt.hit.bone_index < 28 )
				{
					const auto& bone = tgt.hit.record->bones[ tgt.hit.bone_index ];
					const auto& hb = tgt.hit.hitbox;
					const auto capsule_start = bone.rotation.rotate_vector( hb.mins ) + bone.position;
					const auto capsule_end = bone.rotation.rotate_vector( hb.maxs ) + bone.position;
					const auto radius = hb.radius > 0.0f ? hb.radius : 1.8f;

					math::vector3 fwd{}, lft{}, up{};
					math::helpers::angle_vectors_left( aim_angle, &fwd, &lft, &up );
					const auto dir = ( fwd + lft * spread.x + up * spread.y ).normalized( );

					auto fraction{ 1.0f };
					if ( !g_shared.ray_vs_capsule( shoot_eye, dir * shared_ctx.range, capsule_start, capsule_end, radius, fraction ) )
					{
						this->m_firing_this_tick = false;
						return false;
					}
				}
			}

		return true;
	}

} // namespace features::combat
