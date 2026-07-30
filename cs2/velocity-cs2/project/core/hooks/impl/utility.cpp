#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/hooking/hooking.hpp>
#include <utilities/logging/logging.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>
#include "../hooks.hpp"

namespace hooks {

	bool utility::initialize( )
	{
		if ( !hooking::manager::create( {
			{ &m_service_read, &service_read, xs( "service_read" ), PATTERN (patterns::service_read) },
			{ &m_log_internal, &log_internal, xs( "log_internal" ),PATTERN (patterns::log_internal) }
			} ) )
		{
			return false;
		}

		return true;
	}

	void utility::shutdown( )
	{
		m_service_read.reset( );
		m_log_internal.reset( );
	}

	std::uintptr_t __fastcall utility::service_read( std::uintptr_t a1 )
	{
		const auto flags_len = memory::read<std::uint32_t>( a1 - 212 );
		const auto len = flags_len & 0x3fffffff;

		std::string filename;
		if ( len > 0 && len < 512 )
		{
			char buf[ 512 ]{};

			if ( flags_len & 0x40000000 )
			{
				std::memcpy( buf, reinterpret_cast< void* >( a1 - 208 ), std::min( len, 512u - 1 ) );
			}
			else
			{
				const auto str_ptr = memory::read<std::uintptr_t>( a1 - 208 );
				if ( str_ptr )
				{
					std::memcpy( buf, reinterpret_cast< void* >( str_ptr ), std::min( len, 512u - 1 ) );
				}
			}

			filename = buf;
		}

		if ( filename.find( xs( "particles/embedded/" ) ) != std::string::npos )
		{
			std::uint32_t resource_id{ 0 };

			if ( filename.find( xs( "snow" ) ) != std::string::npos )
			{
				resource_id = rpack::particles::weather::snow;
			}
			else if ( filename.find( xs( "rain" ) ) != std::string::npos )
			{
				resource_id = rpack::particles::weather::rain;
			}
			else if ( filename.find( xs( "stars" ) ) != std::string::npos )
			{
				resource_id = rpack::particles::weather::stars;
			}
			else if ( filename.find( xs( "kill" ) ) != std::string::npos )
			{
				resource_id = rpack::particles::effects::killstars;
			}
			else if ( filename.find( xs( "tracer" ) ) != std::string::npos )
			{
				resource_id = rpack::particles::effects::tracer;
			}
			else if ( filename.find( xs( "sparks" ) ) != std::string::npos )
			{
				resource_id = rpack::particles::effects::sparks;
			}
			else if ( filename.find( xs( "fade" ) ) != std::string::npos )
			{
				resource_id = rpack::particles::effects::fade;
			}
			else if ( filename.find( xs( "halo" ) ) != std::string::npos )
			{
				resource_id = rpack::particles::effects::halo;
			}

			if ( resource_id != 0 )
			{
				const auto data = rpack::reader::data( resource_id );
				const auto size = rpack::reader::size( resource_id );

				if ( data && size )
				{
					const auto async_fs = memory::read<std::uintptr_t>( a1 + 24 );
					const auto buffer = memory::call_vfunc<std::uintptr_t>( async_fs, 22, size, filename.c_str( ) );

					if ( buffer )
					{
						std::memcpy( reinterpret_cast< void* >( buffer ), data, size );

						memory::write<std::uintptr_t>( a1 + 56, buffer );
						memory::write<std::uintptr_t>( a1 + 64, size );
						memory::write<std::uintptr_t>( a1 + 72, size );
						memory::call<void>(PATTERN (patterns::filesystem_close), a1 - 224, 0 );

						return 0;
					}
				}
			}
		}

		return m_service_read.call<std::uintptr_t>( a1 );
	}

	std::intptr_t __fastcall utility::log_internal( std::uintptr_t a1, std::uint32_t channel, std::int32_t severity, std::uintptr_t metadata, const char* message, std::intptr_t* args )
	{
if ( settings::g_misc.disable_game_logs && !logging::console::emitting )
		{
			return 0;
		}

		return m_log_internal.call<std::intptr_t>( a1, channel, severity, metadata, message, args );
	}

} // namespace hooks