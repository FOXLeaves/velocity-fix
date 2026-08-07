#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/game_path.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::changer {

	void custom_models::refresh( )
	{
		this->m_models.clear( );

		const auto root = game_path::csgo_directory( );
		if ( !root )
		{
			return;
		}

		// Content roots mounted by the engine, with the base each one is
		// relative to for the in-game resource path.
		std::vector<std::pair<std::filesystem::path, std::filesystem::path>> roots;
		roots.emplace_back( *root / L"characters", *root );
		roots.emplace_back( *root / L"models", *root );
		roots.emplace_back( *root / L"agents", *root );
		roots.emplace_back( *root / L"materials", *root );
		roots.emplace_back( *root / L"phase2" / L"characters", *root / L"phase2" );

		std::vector<std::string> found;

		for ( const auto& [scan_root, base] : roots )
		{
			std::error_code error{};
			if ( !std::filesystem::is_directory( scan_root, error ) )
			{
				continue;
			}

			for ( const auto& entry : std::filesystem::recursive_directory_iterator( scan_root, error ) )
			{
				if ( error )
				{
					error.clear( );
					continue;
				}

				if ( !entry.is_regular_file( error ) )
				{
					continue;
				}

				const auto ext = entry.path( ).extension( );
				if ( ext != L".vmdl" && ext != L".vmdl_c" )
				{
					continue;
				}

				const auto lower_path = [ ]( const std::wstring& value )
				{
					auto out = value;
					std::transform( out.begin( ), out.end( ), out.begin( ), [ ]( wchar_t c )
					{
						return static_cast< wchar_t >( towlower( static_cast< wint_t >( c ) ) );
					} );
					return out;
				};

				const auto stem = lower_path( entry.path( ).stem( ).wstring( ) );

				// Skip first-person hand models (viewmodel/arm variants) and
				// prefab assemblies (not directly usable as a player model).
				const auto is_hand = stem.ends_with( L"_arms" ) || stem.ends_with( L"_arm" )
					|| stem.ends_with( L"modelarm" ) || stem == L"arm" || stem == L"arms";

				if ( is_hand || stem.ends_with( L"_prefab" ) )
				{
					continue;
				}

				bool in_hand_dir{ false };
				for ( const auto& part : entry.path( ).parent_path( ) )
				{
					const auto lower = lower_path( part.wstring( ) );
					if ( lower == L"arms" || lower == L"hand" || lower == L"hands" )
					{
						in_hand_dir = true;
						break;
					}
				}

				if ( in_hand_dir )
				{
					continue;
				}

				std::string relative;
				for ( const auto& part : entry.path( ).lexically_relative( base ) )
				{
					if ( !relative.empty( ) )
					{
						relative += '/';
					}
					relative += part.string( );
				}

				if ( relative.empty( ) || relative.find( "/weapons/" ) != std::string::npos )
				{
					continue;
				}

				// Normalize to the .vmdl form (compiled caches load from the same path).
				if ( relative.ends_with( ".vmdl_c" ) )
				{
					relative = relative.substr( 0, relative.size( ) - 7 ) + ".vmdl";
				}

				found.push_back( std::move( relative ) );
			}
		}

		std::sort( found.begin( ), found.end( ) );
		found.erase( std::unique( found.begin( ), found.end( ) ), found.end( ) );
		this->m_models = std::move( found );
	}

	int custom_models::find( const std::string& path ) const
	{
		if ( path.empty( ) )
		{
			return -1;
		}

		for ( auto i = 0; i < static_cast< int >( this->m_models.size( ) ); ++i )
		{
			if ( this->m_models[ i ] == path )
			{
				return i;
			}
		}

		return -1;
	}

	void agents::on_frame_stage_notify( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		const auto team = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		const auto team_matches = ( this->m_tracked_team == team );

		const auto& cfg = settings::g_changer.agents;

		const auto resolve_target = [ &cfg ]( int t ) -> std::string
		{
			const auto custom_enabled = ( t == 3 ) ? cfg.ct_custom_enabled.value : ( t == 2 ) ? cfg.t_custom_enabled.value : false;
			const auto custom_path = ( t == 3 ) ? cfg.ct_custom_model : ( t == 2 ) ? cfg.t_custom_model : std::string{ };

			if ( custom_enabled && !custom_path.empty( ) )
			{
				return custom_path;
			}

			const auto selected_def_index = ( t == 3 ) ? cfg.ct_def : ( t == 2 ) ? cfg.t_def : static_cast< std::int16_t >( 0 );
			const auto* selected = ( selected_def_index != 0 ) ? g_econ_item_system.find_def( selected_def_index ) : nullptr;
			if ( selected && !selected->model_player.empty( ) )
			{
				return selected->model_player;
			}

			return {};
		};

		const auto target_path = resolve_target( team );

		if ( this->m_tracked_pawn != local.pawn )
		{
			this->m_original_model.clear( );
			this->m_applied_path.clear( );
			this->m_overridden = false;
			this->m_applied_handle = 0;
			this->m_applied_def = 0;
			this->m_tracked_team = 0;
			this->m_tracked_pawn = local.pawn;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return;
		}

		const auto model_state = game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash );

		if ( target_path.empty( ) )
		{
			if ( this->m_overridden && !this->m_original_model.empty( ) )
			{
				memory::call<void>(PATTERN (patterns::set_player_model), local.pawn, this->m_original_model.c_str( ) );

				this->cycle_weapon_owners( local.pawn );

				this->m_applied_handle = 0;
				this->m_applied_path.clear( );
				this->m_applied_def = 0;
				this->m_tracked_team = 0;
				this->m_overridden = false;
			}
			return;
		}

		if ( !this->m_overridden && this->m_original_model.empty( ) )
		{
			const auto model_name_ptr = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_ModelName"_hash ) );
			if ( model_name_ptr )
			{
				this->m_original_model = memory::read_string( model_name_ptr );
			}
		}

		if ( this->m_overridden && !team_matches )
		{
			this->m_original_model.clear( );
			this->m_overridden = false;

			const auto model_name_ptr = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_ModelName"_hash ) );
			if ( model_name_ptr )
			{
				this->m_original_model = memory::read_string( model_name_ptr );
			}
		}

		// New target -> log and reset the applied state so the model gets set again.
		if ( this->m_applied_path != target_path )
		{

			this->m_applied_path = target_path;
			this->m_applied_handle = 0;
		}

		const auto model_name_ptr = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_ModelName"_hash ) );
		std::string current_name;
		if ( model_name_ptr )
		{
			current_name = memory::read_string( model_name_ptr );
		}

		const auto normalize = [ ]( std::string value )
		{
			std::transform( value.begin( ), value.end( ), value.begin( ), [ ]( char c )
			{
				return static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
			} );
			return value;
		};

		const auto current_norm = normalize( current_name );
		const auto target_norm = normalize( target_path );
		const auto applied = current_norm == target_norm || current_norm.ends_with( target_norm ) || target_norm.ends_with( current_norm );

		// Keep precaching every frame until the model is actually applied.
		if ( !applied )
		{
			this->precache_model( target_path );
		}

		if ( this->m_overridden && applied && team_matches )
		{
			return;
		}

		memory::call<void>(PATTERN (patterns::set_player_model), local.pawn, target_path.c_str( ) );

		const auto collision = local.pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash );
		memory::write<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ), math::vector3( -16.0f, -16.0f, 0.0f ) );
		memory::write<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ), math::vector3( 16.0f, 16.0f, 72.0f ) );

		this->cycle_weapon_owners( local.pawn );

		this->m_applied_handle = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );
		this->m_applied_def = -1;
		this->m_tracked_team = team;
		this->m_overridden = true;
	}

	void agents::precache_model( const std::string& path )
	{
		const auto init_path_buffer = PATTERN( patterns::init_particle_path_buffer );
		const auto precache_resource = PATTERN( patterns::resource_system_precache );
		if ( !init_path_buffer || !precache_resource || !addresses::globals::resource_system )
		{
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

		memory::call<void>( init_path_buffer, &buffer, path.c_str( ) );
		buffer.m_unknown4 = 'ldmv';
		memory::call<void>( precache_resource, addresses::globals::resource_system, &buffer, "" );
	}

	void agents::cycle_weapon_owners( std::uintptr_t pawn )
	{
		const auto weapon_services = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
		if ( !weapon_services )
		{
			return;
		}

		const auto weapons_base = weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hMyWeapons"_hash );
		const auto weapons_size = memory::read<int>( weapons_base );
		const auto weapons_data = memory::read<std::uintptr_t>( weapons_base + 0x8 );

		if ( !weapons_data || weapons_size <= 0 )
		{
			return;
		}

		for ( auto i = 0; i < weapons_size; ++i )
		{
			const auto handle = memory::read<std::uint32_t>( weapons_data + i * sizeof( std::uint32_t ) );
			const auto weapon = systems::g_entities.lookup( handle );

			if ( !weapon )
			{
				continue;
			}

			const auto owner_off = SCHEMA( "C_BaseEntity", "m_hOwnerEntity"_hash );
			const auto saved_owner = memory::read<std::uint32_t>( weapon + owner_off );

			memory::write<std::uint32_t>( weapon + owner_off, 0xffffffff );
			memory::write<std::uint32_t>( weapon + owner_off, saved_owner );
		}
	}

} // namespace features::changer