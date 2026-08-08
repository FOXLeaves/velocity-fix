#pragma once

#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <protection/game_addresses.hpp>

// Shared movement helpers: every movement feature (bhop, jumpbug,
// edgebug, ...) traces the player hull the same way - same mask, same
// gravity correction, same contact refinement. Keeping those in one place
// removes the duplicated inline copies and keeps the physics consistent.
namespace features::movement::utils {

	// Ladder / noclip / observer movement states disable movement tricks.
	[[nodiscard]] inline bool is_restricted_move_type( std::uintptr_t pawn )
	{
		if ( !pawn )
		{
			return true;
		}

		const auto move_type = memory::read<std::uint8_t>( pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		return move_type == cstypes::move_type::ladder
			|| move_type == cstypes::move_type::noclip
			|| move_type == cstypes::move_type::observer;
	}

	// Movement trace mask copied from the movement pawn; ORs the debris
	// bit when the pawn data is unavailable/blocked.
	[[nodiscard]] inline std::uint64_t build_movement_trace_mask( std::uintptr_t movement_services )
	{
		auto trace_mask{ 0ull };

		const auto pawn_ptr = memory::safe_read<std::uintptr_t>( movement_services + 56 ).value_or( 0 );
		if ( pawn_ptr )
		{
			trace_mask = memory::safe_read<std::uintptr_t>( pawn_ptr + 0xd48 ).value_or( 0 );
			if ( memory::safe_read<std::uint32_t>( pawn_ptr + 0x3f8 ).value_or( 0 ) & 0x10 )
			{
				trace_mask |= 0x20;
			}
		}
		else
		{
			trace_mask |= 0x20;
		}

		return trace_mask;
	}

	// Movement filter for the player's own hull (collision group 11).
	[[nodiscard]] inline systems::tracing::player_movement_filter movement_filter( std::uintptr_t pawn, std::uintptr_t movement_services )
	{
		return systems::g_tracing.make_player_movement_filter( pawn, build_movement_trace_mask( movement_services ), 11 );
	}

	// The player's hull extents.
	[[nodiscard]] inline systems::tracing::bbox_collision player_hull( std::uintptr_t pawn )
	{
		const auto mins = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash ) + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) );
		const auto maxs = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash ) + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) );
		return { mins, maxs };
	}

	// Full-tick gravity applied BEFORE the move, matching the engine's own
	// stepped integration (AirMove subtracts gravity first, then moves with
	// the resulting velocity). The displacement is then vz*t - g*t^2, not
	// the physically-continuous vz*t - 0.5*g*t^2 - tracing with the
	// midpoint correction drifted the contact fraction off the server's.
	[[nodiscard]] inline math::vector3 gravity_corrected_velocity( const math::vector3& velocity, std::uintptr_t local_pawn )
	{
		auto corrected = velocity;
		const auto sv_gravity = CONVAR( "sv_gravity" )->get<float>( );
		const auto gravity_scale = memory::read<float>( local_pawn + SCHEMA( "C_BaseEntity", "m_flGravityScale"_hash ) );
		corrected.z -= gravity_scale * sv_gravity * cstypes::tick_interval;
		return corrected;
	}

	// A single-tick hull trace of the movement step. The end point is
	// lowered by 2u so shallow contact registers.
	struct step_trace_input
	{
		math::vector3 origin{};
		math::vector3 velocity{};
		systems::tracing::bbox_collision bbox{};
		systems::tracing::player_movement_filter filter{};
		std::uintptr_t movement_services{};
	};

	[[nodiscard]] inline systems::tracing::result trace_movement_step( const step_trace_input& input )
	{
		const auto trace_end = input.origin + input.velocity * cstypes::tick_interval - math::vector3{ 0.0f, 0.0f, 2.0f };
		return systems::g_tracing.trace_player_bbox( input.origin, trace_end, input.bbox, input.filter, input.movement_services );
	}

	// Binary-refine the contact fraction of a step trace so the subtick
	// `when` stays accurate at speed (large per-tick steps coarsen the
	// initial trace).
	[[nodiscard]] inline float refine_contact_fraction( const systems::tracing::result& result, const step_trace_input& input, int passes )
	{
		const auto end = input.origin + input.velocity * cstypes::tick_interval - math::vector3{ 0.0f, 0.0f, 2.0f };
		auto lo = 0.0f;
		auto hi = result.fraction;

		for ( auto i = 0; i < passes; ++i )
		{
			const auto mid = ( lo + hi ) * 0.5f;
			const auto probe = input.origin + ( end - input.origin ) * mid;
			const auto r = systems::g_tracing.trace_player_bbox( input.origin, probe, input.bbox, input.filter, input.movement_services );
			if ( r.fraction < 1.0f )
			{
				hi = mid;
			}
			else
			{
				lo = mid;
			}
		}

		return hi;
	}

	// Standable ground normal threshold.
	[[nodiscard]] inline float standable_normal( )
	{
		return CONVAR( "sv_standable_normal" )->get<float>( );
	}

	// Landing prediction shared by bhop / jumpbug / edgebug: gravity-
	// corrected single-tick hull trace plus binary-refined contact
	// fraction. `holding_duck` restores the standing hull for the trace
	// so the contact fraction matches the unduck at contact.
	struct landing_result
	{
		bool hit{};
		float fraction{};
		float normal_z{};
		float fall_remaining{};
	};

	[[nodiscard]] inline landing_result predict_landing( std::uintptr_t pawn, const math::vector3& origin, const math::vector3& velocity, bool holding_duck )
	{
		landing_result out{};

		const auto movement_services = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return out;
		}

		const auto duck_amount = memory::read<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flDuckAmount"_hash ) );

		auto hull = player_hull( pawn );

		auto trace_origin = origin;
		if ( holding_duck && duck_amount > 0.0f )
		{
			constexpr float standing_height{ 72.0f };
			const auto duck_hull_diff = standing_height - hull.maxs.z;
			trace_origin.z -= duck_hull_diff * 0.5f;
			hull.maxs.z = standing_height;
		}

		const auto filter = movement_filter( pawn, movement_services );
		const auto sv_standable_normal = standable_normal( );

		const auto corrected = gravity_corrected_velocity( velocity, pawn );

		const step_trace_input step{ trace_origin, corrected, hull, filter, movement_services };
		const auto result = trace_movement_step( step );

		out.normal_z = result.normal.z;
		out.fall_remaining = trace_origin.z - ( trace_origin.z + corrected.z * cstypes::tick_interval - 2.0f );

		if ( result.fraction > 0.0f && result.fraction < 1.0f && result.normal.z >= sv_standable_normal )
		{
			out.hit = true;
			out.fraction = refine_contact_fraction( result, step, 4 );
		}

		return out;
	}

} // namespace features::movement::utils