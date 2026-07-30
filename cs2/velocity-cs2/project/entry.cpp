#include <pch/pch.hpp>

#include <utilities/logging/logging.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/security/security.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <utilities/bootstrap/bootstrap.hpp>
#include <utilities/steam/steam.hpp>

#include <core/hooks/hooks.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <core/rendering/rendering.hpp>
#include <protection/protection.hpp>

namespace {

#if defined( DEV )
	void init_log( const char* msg )
	{
		OutputDebugStringA( "[velocity] " );
		OutputDebugStringA( msg );
		OutputDebugStringA( "\n" );
	}

	#define INIT_FAIL( msg ) \
		do { \
			init_log( msg ); \
			return 0; \
		} while ( 0 )

	#define INIT_WARN( msg ) init_log( msg )
#else
	#define INIT_FAIL( msg ) \
		do { \
			MessageBoxA( nullptr, xs( msg ), xs( "..." ), MB_ICONERROR ); \
			return 0; \
		} while ( 0 )

	#define INIT_WARN( msg ) INIT_FAIL( msg )
#endif

	DWORD WINAPI init_thread( LPVOID param )
	{
		const auto module_handle = static_cast<HMODULE>( param );

#if !defined( DEV )
		// bool protection_result = false; g_protection.attach( &protection_result );
		// if ( !protection_result )
		// {
		// 	g_protection.crash( );
		// 	return 0;
		// }
#endif

		CoInitializeEx( nullptr, COINIT_MULTITHREADED );

		config::initialize( );
		settings::finalize_binds( );

		bootstrap::on_dll_attach( );

		security::regions::add_module( module_handle );

		{
			if ( !logging::console::initialize( ) )
			{
#if defined( DEV )
				INIT_WARN( "failed to initialize console logging." );
#else
				INIT_FAIL( "failed to initialize console logging." );
#endif
			}

			if ( !PROTECTION_CHECK( ) || !logging::popup::initialize( ) )
			{
#if defined( DEV )
				INIT_WARN( "failed to initialize popup logging." );
#else
				INIT_FAIL( "failed to initialize popup logging." );
#endif
			}
		}

		{
			if ( !PROTECTION_CHECK( ) || !security::integrity::initialize( ) )
			{
				INIT_FAIL( "failed to initialize integrity checks." );
			}

			if ( !PROTECTION_CHECK( ) || !threadpool::initialize( ) )
			{
				INIT_FAIL( "failed to initialize thread pool." );
			}

			if ( !PROTECTION_CHECK( ) || !steam::http::initialize( ) )
			{
				INIT_FAIL( "failed to initialize steam http." );
			}

			if ( !PROTECTION_CHECK( ) || !steam::friends::initialize( ) )
			{
				INIT_FAIL( "failed to initialize steam friends." );
			}

			if ( !PROTECTION_CHECK( ) || !steam::utils::initialize( ) )
			{
				INIT_FAIL( "failed to initialize steam utils." );
			}
		}

		{
			if ( !PROTECTION_CHECK( ) || !addresses::modules::initialize( ) )
			{
				INIT_FAIL( "failed to initialize module addresses." );
			}

			if ( !PROTECTION_CHECK( ) || !addresses::globals::initialize( ) )
			{
				INIT_FAIL( "failed to initialize global addresses." );
			}

			if ( !PROTECTION_CHECK( ) || !addresses::functions::initialize( ) )
			{
				INIT_FAIL( "failed to initialize function addresses." );
			}
		}

		{
			if ( !PROTECTION_CHECK( ) || !systems::materials::initialize( ) )
			{
				INIT_FAIL( "failed to initialize materials system." );
			}

			if ( !PROTECTION_CHECK( ) || !systems::events::initialize( ) )
			{
				INIT_FAIL( "failed to initialize event system." );
			}

			if ( !PROTECTION_CHECK( ) || !systems::g_icons.initialize( ) )
			{
				INIT_FAIL( "failed to initialize vpk parse system." );
			}

			if ( !PROTECTION_CHECK( ) || !systems::g_model_preview.initialize( ) )
			{
				INIT_FAIL( "failed to initialize model preview system." );
			}
		}

		{
			if ( !PROTECTION_CHECK( ) || !features::changer::g_econ_item_system.initialize( ) )
			{
				INIT_FAIL( "failed to initialize econ item system." );
			}
		}

		{
			if ( !hooks::vac::initialize( ) )
			{
				INIT_FAIL( "failed to initialize vac hooks." );
			}

			if ( !PROTECTION_CHECK( ) || !hooks::utility::initialize( ) )
			{
				INIT_FAIL( "failed to initialize utility hooks." );
			}

			if ( !PROTECTION_CHECK( ) || !hooks::cheat::initialize( ) )
			{
				INIT_FAIL( "failed to initialize cheat hooks." );
			}
		}

		{
			if ( !PROTECTION_CHECK( ) || !addresses::globals::cvar->unlock_all( ) )
			{
				INIT_FAIL( "failed to unlock hidden cvars." );
			}
		}

		features::world::g_scene.discover_skyboxes( );
		return 1;
	}

} // namespace

extern "C" int __stdcall entry( HMODULE module_handle, DWORD reason, LPVOID reserved )
{
	if ( reason == DLL_PROCESS_ATTACH )
	{
		_CRT_INIT( module_handle, reason, reserved );
		DisableThreadLibraryCalls( module_handle );

		const auto thread = CreateThread( nullptr, 0, init_thread, module_handle, 0, nullptr );
		if ( !thread )
		{
			return 0;
		}

		CloseHandle( thread );
		return 1;
	}
#if defined( DEV )
	else if ( reason == DLL_PROCESS_DETACH )
	{
		_CRT_INIT( module_handle, reason, reserved );
		features::esp::player::g_chams.bt( ).shutdown( );
		features::esp::player::g_chams.os( ).shutdown( );

		features::world::g_weather.release( );
		rendering::g_menu.shutdown( );

		systems::events::shutdown( );
		hooks::utility::shutdown( );
		hooks::cheat::shutdown( );
		CoUninitialize( );
	}
#endif

	return 1;
}
