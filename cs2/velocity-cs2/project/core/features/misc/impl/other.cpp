#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
namespace features::misc {
	namespace {

		[[nodiscard]] std::string controller_name( std::uintptr_t controller )
		{
			if ( !controller )
			{
				return {};
			}

			const auto name_ptr = memory::safe_read<std::uintptr_t>(
				controller + SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_hash ) ).value_or( 0 );
			return memory::read_string( name_ptr, 127 );
		}

		void submit_name_change( const std::string& display_name )
		{
			if ( display_name.empty( ) )
			{
				return;
			}

			other::s_display_name = display_name;
			other::s_name_change_pending = true;
			memory::call<void>( PATTERN( patterns::engine_client_cmd ), addresses::globals::source2engine_to_client, 0, xs( "setinfo name x" ), 0x7ffef001 );
			other::s_name_change_pending = false;
		}

	} // namespace

	void other::on_round_start( )
	{
		this->do_autobuy( );
	}

	void other::on_player_death( std::uintptr_t event )
	{
		if ( !event )
		{
			return;
		}

		const auto attacker_key = cstypes::event_hash{ 0, "attacker" };
		const auto attacker = memory::call<std::uintptr_t>( PATTERN (patterns::game_event_get_controller), event, &attacker_key );
	}

	void other::on_frame_stage_notify( )
	{
		this->do_player_alpha_changing( );
		this->do_reveal_radar( );
		this->do_name_changing( );
		this->do_viewmodel_adjust( );
		this->do_auto_accept( );
	}

	void other::do_auto_accept( )
	{
		if ( !settings::g_misc.auto_accept.value )
		{
			this->m_auto_accept_done = false;
			this->m_auto_accept_ui_frames = 0;
			return;
		}

		// Pointer slot to the matchmaking session object (rip-relative).
		static const auto session_slot = PATTERN( patterns::auto_accept_session );
		if ( !session_slot )
		{
			return;
		}

		if ( !this->m_auto_accept_set_ready )
		{
			this->m_auto_accept_set_ready = PATTERN( patterns::set_player_ready );
			if ( !this->m_auto_accept_set_ready )
			{
				return;
			}
		}

		const auto session = memory::safe_read<std::uintptr_t>( session_slot ).value_or( 0 );
		if ( !session )
		{
			this->m_auto_accept_done = false;
			this->m_auto_accept_ui_frames = 0;
			return;
		}

		// Matchmaking session state. The current pattern matches the engine
		// gate `cmp dword [session+0xA8], 3` (accept screen), while older
		// builds used 1 - accept both so a depot change cannot silently
		// disable the feature.
		const auto state = memory::safe_read<std::uint32_t>( session + 0xA8 ).value_or( 0 );
		const auto completed = memory::safe_read<std::uint8_t>( session + 0xA4 ).value_or( 0 );

		// Accepted player count for the accept screen (best-effort: the
		// session layout has no public layout, these offsets were probed
		// from the accept dialog state; only shown when they look sane).
		const auto accepted_now = memory::safe_read<std::int32_t>( session + 0xAC ).value_or( 0 );
		const auto accepted_total = memory::safe_read<std::int32_t>( session + 0xB0 ).value_or( 0 );
		const auto count_sane = accepted_total > 0 && accepted_total <= 20 && accepted_now >= 0 && accepted_now <= accepted_total;

		if ( state == 0 )
		{
			this->m_auto_accept_done = false;
			this->m_auto_accept_ui_frames = 0;
			return;
		}

		// Throttled retry while the accept screen is up: the first call
		// can land on a transient frame (UI not settled / session still
		// transitioning), so keep trying every few frames until the screen
		// goes away. Repeated "accept" calls on the screen are idempotent.
		if ( ( state == 1 || state == 3 ) && completed == 1 )
		{
			this->m_auto_accept_attempts++;

			const auto first_call = !this->m_auto_accept_done;

			if ( first_call || ( this->m_auto_accept_attempts % 3 == 0 ) )
			{
				this->m_auto_accept_done = true;
				memory::call<char>( this->m_auto_accept_set_ready, nullptr, "accept" );

				if ( first_call && settings::g_misc.m_impacts.console_log.value )
				{
					if ( count_sane )
					{
						logging::console::print(
							xs( "[auto-accept] 已自动接受匹配（{} / {} 人已接受）" ),
							accepted_now,
							accepted_total
						);
					}
					else
					{
						logging::console::print( xs( "[auto-accept] 已自动接受匹配" ) );
					}
				}

				if ( this->m_auto_accept_ui_frames == 0 )
				{
					this->m_auto_accept_ui_frames = 10;
				}
			}

			if ( this->m_auto_accept_ui_frames > 0 )
			{
				--this->m_auto_accept_ui_frames;
				if ( this->m_auto_accept_ui_frames == 0 )
				{
					this->dispatch_auto_accept_ui( );
				}
			}
		}
	}

	bool other::dispatch_auto_accept_ui( ) const
	{
		// Same plumbing as scoreboard_weapons: resolve the CUIEngine context
		// (works from the main menu where no HUD panel exists) and run a tiny
		// Panorama snippet through vtable[77] to fire the assisted accept.
		const auto panorama = reinterpret_cast<features::misc::c_panorama_ui_engine*>( addresses::globals::panorama );
		if ( !panorama )
		{
			return false;
		}

		const auto cui_engine = panorama->get_ui_engine( );
		if ( !cui_engine )
		{
			return false;
		}

		const auto engine = reinterpret_cast<std::uintptr_t>( cui_engine );
		const auto max_entries = memory::safe_read<std::int32_t>( engine + 0x220 ).value_or( 0 );
		const auto table_base = memory::safe_read<std::uintptr_t>( engine + 0x228 ).value_or( 0 );
		if ( max_entries <= 0 || !table_base )
		{
			return false;
		}

		const auto client_base = addresses::modules::client;
		const auto client_size = memory::get_module_size( client_base );

		std::uintptr_t context{ 0 };
		for ( auto i = 0; i < (std::min)( max_entries, 500 ); ++i )
		{
			const auto entry = table_base + static_cast< std::uintptr_t >( i ) * 32;
			const auto marker = memory::safe_read<std::int32_t>( entry ).value_or( i );
			if ( marker == i )
			{
				continue;
			}

			const auto candidate = memory::safe_read<std::uintptr_t>( entry + 16 ).value_or( 0 );
			if ( !candidate )
			{
				continue;
			}

			const auto panel = memory::safe_read<std::uintptr_t>( candidate + 8 ).value_or( 0 );
			if ( !panel )
			{
				continue;
			}

			const auto vtable = memory::safe_read<std::uintptr_t>( panel ).value_or( 0 );
			if ( vtable >= client_base && vtable < client_base + client_size )
			{
				context = candidate;
				break;
			}
		}

		if ( !context )
		{
			return false;
		}

		const auto vtable = memory::safe_read<std::uintptr_t>( engine ).value_or( 0 );
		if ( !vtable )
		{
			return false;
		}

		const auto run_script = memory::safe_read<std::uintptr_t>( vtable + 77 * sizeof( std::uintptr_t ) ).value_or( 0 );
		if ( !run_script )
		{
			return false;
		}

		memory::call<std::int64_t>(
			run_script,
			engine,
			context,
			xs( "$.DispatchEvent('MatchAssistedAccept');" ),
			xs( "auto_accept" ),
			static_cast<std::int64_t>( 0 ) );

		return true;
	}

	void other::do_reveal_radar( ) const
	{
		if ( !settings::g_misc.reveal_radar.value )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) )
		{
			return;
		}

		const auto spotted_state_offset = SCHEMA( "C_CSPlayerPawn", "m_entitySpottedState"_hash );
		const auto spotted_offset = SCHEMA( "EntitySpottedState_t", "m_bSpotted"_hash );

		for ( const auto& player : systems::g_entities.get_by_type( systems::entities::type::player ) )
		{
			const auto controller = player.ptr;
			if ( !controller || !memory::read<bool>( controller + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto pawn_handle = memory::read<std::uint32_t>( controller + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );
			if ( !pawn || pawn == local.view_pawn( ) )
			{
				continue;
			}

			const auto team = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
			{
				continue;
			}

			memory::write<bool>( pawn + spotted_state_offset + spotted_offset, true );
		}
	}

	void other::do_autobuy( ) const
	{
		if ( !settings::g_misc.m_autobuy.enabled )
		{
			return;
		}

		std::string cmd {};

		switch ( settings::g_misc.m_autobuy.primary_weapon )
		{
		case 1: cmd += xs( "buy ak47; buy m4a1; " ); break;
		case 2: cmd += xs( "buy sg556; buy aug; " ); break;
		case 3: cmd += xs( "buy ssg08; " ); break;
		case 4: cmd += xs( "buy awp; " ); break;
		case 5: cmd += xs( "buy g3sg1; buy scar20; " ); break;
		}

		if ( settings::g_misc.m_autobuy.armor )
		{
			cmd += xs( "buy vesthelm; buy vest; " );
		}

		if ( settings::g_misc.m_autobuy.taser )
		{
			cmd += xs( "buy taser; " );
		}

		if ( settings::g_misc.m_autobuy.defuser )
		{
			cmd += xs( "buy defuser; " );
		}

		switch ( settings::g_misc.m_autobuy.secondary_weapon )
		{
		case 1: cmd += xs( "buy elite; " ); break;
		case 2: cmd += xs( "buy fiveseven; buy tec9; " ); break;
		case 3: cmd += xs( "buy deagle; " ); break;
		case 4: cmd += xs( "buy revolver; " ); break;
		}

		for ( auto i = 0; i < 5; ++i )
		{
			if ( !settings::g_misc.m_autobuy.grenades[ i ] )
			{
				continue;
			}

			switch ( i )
			{
			case 0: cmd += xs( "buy molotov; buy incgrenade; " ); break;
			case 1: cmd += xs( "buy hegrenade; " ); break;
			case 2: cmd += xs( "buy smokegrenade; " ); break;
			case 3: cmd += xs( "buy flashbang; " ); break;
			case 4: cmd += xs( "buy decoy; " ); break;
			}
		}

		if ( !cmd.empty( ) )
		{
			memory::call<void>(PATTERN (patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, cmd.c_str( ), 0x7ffef001 );
		}
	}

	void other::do_player_alpha_changing( )
	{
		const auto local = systems::g_local.get( );

		if ( !local.pawn || !local.is_alive )
		{
			if ( this->m_is_alpha_changed && local.pawn )
			{
				memory::call<void>( PATTERN (patterns::game_event_get_string), local.pawn, 255 );
			}

			this->m_is_alpha_changed = false;
			return;
		}

		if ( !settings::g_esp.m_local_alpha.enabled.value )
		{
			if ( this->m_is_alpha_changed )
			{
				this->m_is_alpha_changed = false;
				memory::call<void>( PATTERN (patterns::game_event_get_string), local.pawn, 255 );
			}

			return;
		}

		const auto is_scoped = memory::read<bool>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );
		const auto should_apply = !settings::g_esp.m_local_alpha.only_scoped.value || is_scoped;

		if ( should_apply )
		{
			this->m_is_alpha_changed = true;
			const auto alpha = static_cast< std::uint8_t >( settings::g_esp.m_local_alpha.opacity.value * 255.0f );
			memory::call<void>( PATTERN (patterns::game_event_get_string), local.pawn, alpha );
		}
		else
		{
			if ( this->m_is_alpha_changed )
			{
				this->m_is_alpha_changed = false;
				memory::call<void>( PATTERN (patterns::game_event_get_string), local.pawn, 255 );
			}
		}
	}

	void other::do_name_changing( )
	{
		const auto local = systems::g_local.get( );
		const auto& cfg = settings::g_misc.m_name_changer;
		const auto enabled = cfg.clantag.value || cfg.override_name.value;

		if ( !enabled )
		{
			if ( this->m_name_changer_active && local.controller && !this->m_original_name.empty( ) )
			{
				submit_name_change( this->m_original_name );
			}

			this->m_name_changer_active = false;
			this->m_name_changer_controller = 0;
			this->m_original_name.clear( );
			this->m_last_sent_name.clear( );
			other::s_display_name.clear( );
			return;
		}

		if ( !local.controller )
		{
			return;
		}

		if ( !this->m_name_changer_active )
		{
			this->m_original_name = controller_name( local.controller );
			if ( this->m_original_name.empty( ) )
			{
				this->m_original_name = xs( "Player" );
			}

			this->m_name_changer_active = true;
			this->m_name_changer_controller = local.controller;
			this->m_last_sent_name.clear( );
		}
		else if ( this->m_name_changer_controller != local.controller )
		{
			// Keep the captured real name across map loads, where the controller may be recreated.
			this->m_name_changer_controller = local.controller;
			this->m_last_sent_name.clear( );
		}

		const auto& configured_name = cfg.name.value;
		const auto& base_name = cfg.override_name.value && !configured_name.empty( )
			? configured_name
			: this->m_original_name;

		std::string display_name = base_name;
		if ( cfg.clantag.value )
		{
			constexpr std::string_view tag{ "velocity" };
			constexpr auto ticks_per_step{ 16 }; // 0.25 seconds at CS2's 64-tick interval.
			constexpr auto phase_count{ static_cast<int>( tag.size( ) * 2 ) };

			const auto global_vars = memory::safe_read<std::uintptr_t>( addresses::globals::global_vars ).value_or( 0 );
			const auto current_tick = global_vars
				? memory::safe_read<int>( global_vars + 0x44 ).value_or( 0 )
				: 0;
			auto phase = current_tick / ticks_per_step % phase_count;
			if ( phase < 0 )
			{
				phase += phase_count;
			}

			const auto reveal_index = phase <= static_cast<int>( tag.size( ) )
				? phase
				: phase_count - phase;
			const auto visible_tag = tag.substr( 0, static_cast<std::size_t>( reveal_index ) );
			if ( !visible_tag.empty( ) )
			{
				display_name.reserve( base_name.size( ) + visible_tag.size( ) + 3 );
				display_name = "[";
				display_name += visible_tag;
				display_name += "] ";
				display_name += base_name;
			}
		}
		if ( display_name == this->m_last_sent_name )
		{
			return;
		}

		submit_name_change( display_name );
		this->m_last_sent_name = std::move( display_name );
	}

	void other::do_kill_feed_preservation( )
	{
		const auto local = systems::g_local.get ();

		if (!local.pawn || !local.is_alive) {
			return;
		}

		const auto hud_element = memory::call<std::uintptr_t>(PATTERN (patterns::find_hud_element), xs( "CCSGO_HudDeathNotice" ) );
		if ( !hud_element )
		{
			return;
		}

		memory::write<float> (hud_element + 0x58, settings::g_misc.preserve_killfeed ? 1000.0f : 1.5f);

		float spawntime = memory::read<float> (local.pawn + SCHEMA ("C_CSPlayerPawnBase", "m_flLastSpawnTimeIndex"_hash));
		if ( m_last_spawntime != spawntime )
		{
			const auto clear_death_notices = PATTERN (patterns::hud_death_notice_clear);
			if ( clear_death_notices )
			{
				memory::call<void>( clear_death_notices, hud_element - 0x20 );
			}

			m_last_spawntime = spawntime;
		}
	}

	void other::do_viewmodel_adjust( )
	{
		const auto& cfg = settings::g_misc.m_viewmodel_adjust;
		if ( !cfg.enabled.value )
		{
			this->m_cached_vm_x = std::numeric_limits<float>::quiet_NaN( );
			this->m_cached_vm_y = std::numeric_limits<float>::quiet_NaN( );
			this->m_cached_vm_z = std::numeric_limits<float>::quiet_NaN( );
			this->m_cached_vm_fov = std::numeric_limits<float>::quiet_NaN( );
			return;
		}

		const auto set_float_cvar = [ ]( std::uint32_t hash, float value )
			{
			addresses::globals::cvar->find(hash)->m_value.fl = value;
			};

		if ( cfg.offset_x.value != this->m_cached_vm_x )
		{
			set_float_cvar( "viewmodel_offset_x"_hash, cfg.offset_x.value );
			this->m_cached_vm_x = cfg.offset_x.value;
		}

		if ( cfg.offset_y.value != this->m_cached_vm_y )
		{
			set_float_cvar( "viewmodel_offset_y"_hash, cfg.offset_y.value );
			this->m_cached_vm_y = cfg.offset_y.value;
		}

		if ( cfg.offset_z.value != this->m_cached_vm_z )
		{
			set_float_cvar( "viewmodel_offset_z"_hash, cfg.offset_z.value );
			this->m_cached_vm_z = cfg.offset_z.value;
		}

		if ( cfg.fov.value != this->m_cached_vm_fov )
		{
			set_float_cvar( "viewmodel_fov"_hash, cfg.fov.value );
			this->m_cached_vm_fov = cfg.fov.value;
		}
	}

} // namespace features::misc
