#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>

#include "../misc.hpp"
#include <protection/game_addresses.hpp>

namespace features::misc {

	void camera::on_override_view( std::uintptr_t view_setup )
	{
		this->do_aspect_ratio_change( view_setup );

		const auto local = systems::g_local.get( );
		if ( !local.is_alive || systems::g_local.is_in_cinematic( ) || local.team < 2 || !local.pawn )
		{
			return;
		}

		this->do_thirdperson( view_setup, local.pawn );
		this->do_fov_change( view_setup, local.pawn );
	}

	void camera::update_fov_sensitivity( std::uintptr_t player_pawn ) const
	{
		if ( !settings::g_misc.m_camera.change_fov.value )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( player_pawn != local.pawn )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_camera;
		const auto is_scoped = memory::read<bool>( player_pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );
		const auto target_fov = ( is_scoped && cfg.scoped_fov_override.value ) ? cfg.scoped_fov.value : cfg.fov.value;

		if ( is_scoped == this->m_cached_scoped && target_fov == this->m_cached_target_fov && this->m_cached_fov_sensitivity >= 0.0f )
		{
			const auto current_adjust = memory::read<float>( player_pawn + SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_hash ) );
			if ( std::fabsf( current_adjust - this->m_cached_fov_sensitivity ) < 0.0001f )
			{
				return;
			}
		}

		this->m_cached_scoped = is_scoped;
		this->m_cached_target_fov = target_fov;

		const auto ratio = CONVAR ("zoom_sensitivity_ratio")->get<float>( );
		const auto desired = ratio * ( target_fov / 90.0f );

		this->m_cached_fov_sensitivity = desired;
		memory::write<float>( player_pawn + SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_hash ), desired );
	}

	void camera::do_thirdperson( std::uintptr_t view_setup, std::uintptr_t local_pawn ) const
	{
		const auto& cfg = settings::g_misc.m_camera;
		if ( !cfg.thirdperson.value )
		{
			return;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto view_offset = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
		const auto eye_position = origin + view_offset;
		const auto view_angles = systems::g_input.get_view_angles( );

		math::vector3 forward{};
		{
			math::helpers::angle_vectors_left( view_angles, &forward );
		}

		auto camera_position = eye_position - forward * cfg.thirdperson_distance;
		const auto hull = math::vector3{ -cfg.thirdperson_hull_size, -cfg.thirdperson_hull_size, -cfg.thirdperson_hull_size };
		const auto result = systems::g_tracing.trace_hull( eye_position, camera_position, hull, hull, local_pawn );

		if ( result.fraction < 1.0f )
		{
			const auto world = systems::g_entities.get_by_index( 0 );
			if ( result.hit_entity == world )
			{
				camera_position = eye_position + ( camera_position - eye_position ) * result.fraction;
			}
		}

		memory::write<math::vector3>( view_setup + 0x4a0, camera_position );
	}

	void camera::do_fov_change( std::uintptr_t view_setup, std::uintptr_t local_pawn ) const
	{
		const auto& cfg = settings::g_misc.m_camera;
		if ( !cfg.change_fov.value )
		{
			return;
		}

		const auto is_scoped = memory::read<bool>( local_pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );
		const auto target_fov = ( is_scoped && cfg.scoped_fov_override.value ) ? cfg.scoped_fov.value : cfg.fov.value;

		if ( !cfg.change_aspect_ratio.value )
		{
			memory::write<float>( view_setup + 0x498, target_fov );
		}

		this->update_fov_sensitivity( local_pawn );
	}

	void camera::do_aspect_ratio_change( std::uintptr_t view_setup )
	{
		const auto& cfg = settings::g_misc.m_camera;

		if ( cfg.change_aspect_ratio.value )
		{
			if ( this->m_original_aspect_ratio == 0.0f )
			{
				this->m_original_aspect_ratio = memory::read<float>( view_setup + 0x4d8 );
			}

			const auto current_aspect_ratio = memory::read<float>( view_setup + 0x4d8 );
			const auto current_fov = cfg.change_fov.value ? cfg.fov : memory::read<float>( view_setup + 0x498 );

			memory::write<float>( view_setup + 0x4d8, cfg.aspect_ratio );
			memory::write<std::uint8_t>( view_setup + 0x555, memory::read<std::uint8_t>( view_setup + 0x555 ) | 0x2 );

			if ( current_aspect_ratio > 0.0f )
			{
				const auto scale = cfg.aspect_ratio / current_aspect_ratio;
				const auto new_fov = current_fov * scale;
				memory::write<float>( view_setup + 0x498, new_fov );
			}
		}
		else
		{
			if ( this->m_original_aspect_ratio != 0.0f )
			{
				memory::write<float>( view_setup + 0x4d8, this->m_original_aspect_ratio );
				memory::write<std::uint8_t>( view_setup + 0x555, memory::read<std::uint8_t>( view_setup + 0x555 ) & ~0x2 );
			}

			this->m_original_aspect_ratio = 0.0f;
		}
	}

} // namespace features::misc