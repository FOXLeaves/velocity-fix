#include <pch/pch.hpp>

#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>

#include <core/settings.hpp>
#include <core/systems/systems.hpp>

#include "../misc.hpp"

namespace features::misc {

	namespace {

		constexpr std::uintptr_t k_min_ptr = 0x10000000ull;
		constexpr std::uintptr_t k_max_ptr = 0x7FFFFFFFFFFFull;

		[[nodiscard]] inline bool valid_ptr( std::uintptr_t p ) noexcept
		{
			return p >= k_min_ptr && p <= k_max_ptr;
		}

	} // namespace

	void dlight::on_present( )
	{
		auto& cfg = settings::g_misc.m_dlight;

		if ( !cfg.enabled.value )
		{
			this->m_connected_frames = 0;
			return;
		}

		/*const auto mgr_slot = addresses::globals::dynamic_light_manager_slot;
		const auto fn_addr = addresses::functions::get_dynamic_light_idx;

		if ( !mgr_slot || !fn_addr )
		{
			return;
		}

		const auto get_idx = reinterpret_cast<std::uint8_t*( __fastcall* )( void*, std::uint32_t, std::int32_t )>( fn_addr );

		const auto local = systems::g_local.get( );
		if ( !local.controller || !local.pawn || !local.is_alive )
		{
			this->m_connected_frames = 0;
			return;
		}

		this->m_connected_frames++;

		constexpr int k_stabilize_frames = 300;
		if ( this->m_connected_frames < k_stabilize_frames )
		{
			return;
		}

		const auto manager = *reinterpret_cast< void* const* >( mgr_slot );
		const auto mgr_u = reinterpret_cast< std::uintptr_t >( manager );

		if ( !valid_ptr( mgr_u ) )
		{
			return;
		}

		const auto game_scene = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );

		if ( !game_scene || !valid_ptr( game_scene ) )
		{
			return;
		}

		const auto pos = memory::read<math::vector3>( game_scene + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		if ( !global_vars )
		{
			return;
		}

		const auto curtime = memory::read<float>( global_vars + 0x30 );

		auto* const entry = get_idx( manager, 1u, 0 );

		const auto ent_u = reinterpret_cast< std::uintptr_t >( entry );
		if ( !entry || !valid_ptr( ent_u ) )
		{
			return;
		}

		const auto& c = cfg.color.value;

		const auto r = static_cast<std::uint32_t>( c.r );
		const auto g = static_cast<std::uint32_t>( c.g );
		const auto b = static_cast<std::uint32_t>( c.b );

		const std::uint32_t packed = r | ( g << 8u ) | ( b << 16u ) | ( 255u << 24u );

		const float radius = cfg.radius.value;
		const float z_off = cfg.z_offset.value;

		*reinterpret_cast< float* >( entry + 0x04 ) = pos.x;
		*reinterpret_cast< float* >( entry + 0x08 ) = pos.y;
		*reinterpret_cast< float* >( entry + 0x0C ) = pos.z + z_off;
		*reinterpret_cast< float* >( entry + 0x10 ) = radius;
		*reinterpret_cast< std::uint32_t* >( entry + 0x14 ) = packed;
		*reinterpret_cast< float* >( entry + 0x18 ) = curtime + 0.15f;
		*reinterpret_cast< float* >( entry + 0x1C ) = 42.0f;*/

		// redo this
	}

} // namespace features::misc
