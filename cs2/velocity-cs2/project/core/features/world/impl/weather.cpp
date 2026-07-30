#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>
#include "../world.hpp"

namespace features::world {

	void weather::on_frame_stage_notify( )
	{
		if ( !settings::g_world.m_weather.enabled.value )
		{
			if ( this->m_effect_index != 0 )
			{
				this->release_particles( );
			}

			this->m_last_particle_type = -1;
			return;
		}

		const auto game_rules = memory::read<std::uintptr_t>( addresses::globals::game_rules );
		if ( !game_rules )
		{
			this->release_particles( );
			return;
		}

		const auto round_start_time = memory::read<float>( game_rules + SCHEMA( "C_CSGameRules", "m_fRoundStartTime"_hash ) );
		if ( round_start_time != this->m_last_round_start_time )
		{
			this->release_particles( );
			this->m_last_round_start_time = round_start_time;
		}

		this->update_particles( );
	}

	void weather::release( )
	{
		this->release_particles( );
	}

	void weather::create_particle( )
	{
		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager )
		{
			return;
		}

		// FIX: "xs" returning a const char* will break here, its a dangling pointer. we use std::string's memory management to avoid this.
		std::string particle_path = "";

		switch ( settings::g_world.m_weather.type )
		{
		case settings::world::weather::weather_type::snow:
			particle_path = xs( "particles/embedded/snow.vpcf" );
			break;
		case settings::world::weather::weather_type::rain:
			particle_path = xs( "particles/embedded/rain.vpcf" );
			break;
		case settings::world::weather::weather_type::stars:
			particle_path = xs( "particles/embedded/stars.vpcf" );
			break;
		default:
			return;
		}

		struct buffer_string
		{
			std::uint32_t m_unknown1{};
			std::uint32_t m_unknown2{ 0xc00000c8 };

			union
			{
				std::uintptr_t m_str_ptr;
				std::uint8_t data[ 0xc8 ];
			};

			std::uintptr_t m_unknown3{};
			std::uintptr_t m_unknown4{};
		} buffer;

		memory::call<void>(PATTERN (patterns::init_particle_path_buffer_alt), &buffer, particle_path.c_str() );

		buffer.m_unknown4 = 'fcpv';

		memory::call<void>(PATTERN (patterns::resource_system_load), addresses::globals::resource_system, &buffer, "" );

		auto effect_index{ 0u };
		memory::call<int*>(PATTERN (patterns::particle_create_effect), particle_manager, &effect_index, particle_path.c_str (), 2, 0ll, 0ll, 0ll, 0 );

		this->m_effect_index = effect_index;
		this->m_last_particle_type = static_cast< int >( settings::g_world.m_weather.type.value );
	}

	void weather::update_particles( )
	{
		const auto current_type = static_cast< int >( settings::g_world.m_weather.type.value );

		if ( this->m_effect_index != 0 && this->m_last_particle_type != current_type )
		{
			this->release_particles( );
		}

		if ( this->m_effect_index == 0 )
		{
			this->create_particle( );

			if ( this->m_effect_index == 0 )
			{
				return;
			}
		}

		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		const auto view_pawn = systems::g_local.get( ).view_pawn( );

		if ( !view_pawn || !particle_manager )
		{
			return;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( view_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return;
		}

		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, this->m_effect_index, 0, &origin, 0 );

		const auto color = math::vector3{ static_cast< float >( settings::g_world.m_weather.color.value.r ), static_cast< float >( settings::g_world.m_weather.color.value.g ), static_cast< float >( settings::g_world.m_weather.color.value.b ) };
		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, this->m_effect_index, 1, &color, 0 );
	}

	void weather::release_particles( )
	{
		if ( this->m_effect_index == 0 )
		{
			return;
		}

		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( particle_manager )
		{
			memory::call<void>(PATTERN (patterns::particle_destroy_effect), particle_manager, this->m_effect_index, true, true );
			memory::call<void>(PATTERN (patterns::particle_stop_effect), particle_manager, this->m_effect_index );
		}

		this->m_effect_index = 0;
		this->m_last_particle_type = -1;
		this->m_particle_loaded = false;
	}

} // namespace features::world