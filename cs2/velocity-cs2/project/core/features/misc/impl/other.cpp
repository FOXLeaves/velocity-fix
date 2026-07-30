#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
namespace features::misc {

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
		this->do_name_changing( );
		this->do_viewmodel_adjust( );
		this->do_femboy_praise( );
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

	void other::do_name_changing( ) const
	{
		if ( !settings::g_misc.m_name_changer.enabled.value )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.controller )
		{
			return;
		}

		/// @TODO: impl this properly, this is pure dogshit

		//static std::string_view last_base_name{};
		//static auto reveal_index{ 0ull };
		//static auto forward{ true };
		//static auto last_update_time{ 0.0f };

		//const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		//const auto current_time = memory::read<float>( global_vars + 0x30 );

		//if ( current_time < last_update_time )
		//{
		//	last_update_time = 0.0f;
		//	reveal_index = 0;
		//	forward = true;
		//}

		//if ( current_time - last_update_time < settings::g_misc.m_name_changer.speed.value )
		//{
		//	return;
		//}

		//const auto& cfg = settings::g_misc.m_name_changer;
		//const auto& base_name = cfg.text.value;
		//const auto len = base_name.size( );

		//if ( base_name != last_base_name )
		//{
		//	reveal_index = 0;
		//	forward = true;
		//	last_base_name = base_name;
		//}

		//const auto mode = cfg.mode.value;
		//const auto animation = cfg.animation.value;

		//std::string display;
		//display.reserve( len + 32 );

		//if ( animation == settings::misc::name_changer::animation_type::typewriter )
		//{
		//	if ( forward )
		//	{
		//		if ( ++reveal_index >= len )
		//		{
		//			reveal_index = len;
		//			forward = false;
		//		}
		//	}
		//	else
		//	{
		//		if ( reveal_index == 0 )
		//		{
		//			forward = true;
		//			reveal_index = 1;
		//		}
		//		else
		//		{
		//			reveal_index--;
		//		}
		//	}

		//	reveal_index = std::min( reveal_index, len );

		//	if ( mode == settings::misc::name_changer::mode_type::clantag )
		//	{
		//		display = "[";
		//		display.append( base_name, 0, reveal_index );
		//		display += "] ";

		//		const auto current_name = memory::read_string( local.controller + SCHEMA( "CBasePlayerController", "m_iszPlayerName"_hash ) );
		//		if ( const auto pos = current_name.find( "] " ); pos != std::string::npos )
		//		{
		//			display.append( current_name, pos + 2 );
		//		}
		//		else
		//		{
		//			display += current_name;
		//		}
		//	}
		//	else
		//	{
		//		display.append( base_name, 0, reveal_index );
		//	}
		//}
		//else
		//{
		//	if ( mode == settings::misc::name_changer::mode_type::clantag )
		//	{
		//		display = "[" + base_name + "] ";

		//		const auto current_name = memory::read_string( local.controller + SCHEMA( "CBasePlayerController", "m_iszPlayerName"_hash ) );
		//		if ( const auto pos = current_name.find( "] " ); pos != std::string::npos )
		//		{
		//			display.append( current_name, pos + 2 );
		//		}
		//		else
		//		{
		//			display += current_name;
		//		}
		//	}
		//	else
		//	{
		//		display = base_name;
		//	}
		//}

		//other::s_display_name = display;

		//static const auto name_cvar = addresses::globals::cvar->find ( "name"_hash );
		//if ( name_cvar )
		//{
		//	const auto flags = memory::read<std::int32_t>( name_cvar + 0x10 );
		//	if ( !( flags & 0x200 ) )
		//	{
		//		memory::write<std::int32_t>( name_cvar + 0x10, flags | 0x200 );
		//	}
		//}

		//memory::call<void>(PATTERN (patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, xs( "setinfo name x" ), 0x7ffef001 );

		//last_update_time = current_time;
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

		memory::write<float> (hud_element + 0x5C, settings::g_misc.preserve_killfeed ? 1000.0f : 1.5f);

		float spawntime = memory::read<float> (local.pawn + SCHEMA ("C_CSPlayerPawnBase", "m_flLastSpawnTimeIndex"_hash));
		if ( m_last_spawntime != spawntime )
		{
			memory::call<void>(PATTERN (patterns::hud_death_notice_clear), hud_element - 0x20 );
			m_last_spawntime = spawntime;
		}
	}

	void other::do_femboy_praise( ) const
	{
		if ( !settings::g_misc.get_praised_by_a_femboy_in_chat )
		{
			return;
		}

		const auto event = features::misc::g_impacts.poll_event( );
		if ( !event )
		{
			return;
		}

		const auto hud_element = memory::call<std::uintptr_t>(PATTERN (patterns::find_hud_element), xs( "CCSGO_HudVoiceStatus" ) );
		if ( !hud_element )
		{
			return;
		}

		const auto voice = hud_element - 32;

		static constexpr const char* kill_msgs[ ]
		{
			"omg tapped! ur so good <3",
			"heh nice kill cutie ;3",
			"they got absolutely destroyed, wish you would destroy me like that..",
			"aww nice shot, shoot inside me next >:3",
			"gg ez ur literally so good <33",
			"ur so hot omg",
			"IM GONNA CUM UR SO GOOD",
			"nyaa~ you just ended them so easily, be rough with me too pls <3",
			"omggg ur so strong when u kill like that.. i need u to pin me rn >///<",
			"ur kills are so clean, makes me wanna get on my knees for u ;3",
			"ur making my bussy wet omg.."
		};

		static constexpr const char* hit_msgs[ ]
		{
			"ooo nice hit~ do it again >:3",
			"hehe they felt that one, just like i feel u <3",
			"ur aim makes me blush wtf",
			"eep~ that hit was so good, hit me harder next time?~",
			"you tagged them so well.. tag me like that too pls <3",
			"hehe ur shots are making me squirm in my chair :3",
			"mmm that was crispy, just like u cutie~"
		};

		static constexpr const char* miss_msgs[ ]
		{
			"its ok bb everyone misses sometimes <3",
			"dw about it ur still amazing~",
			"the bullets are just shy, like u :3",
			"its fine love, ur still making my heart race either way <33",
			"skill issue... jk jk ur doing great <33",
			"missed shots only make ur next kills hotter, i believe in u!!",
			"that was the cheat's fault not urs obviously"
		};

		static std::mt19937 rng {std::random_device{}()};
		static size_t last_kill = SIZE_MAX;
		static size_t last_hit = SIZE_MAX;
		static size_t last_miss = SIZE_MAX;

		auto pick_no_repeat = [&] (const auto& msgs, size_t& last_idx) -> const char* {
			constexpr size_t n = std::extent_v<std::remove_reference_t<decltype(msgs)>>;
			std::uniform_int_distribution<size_t> dist (0, n - 1);
			size_t idx = dist (rng);
			if (n > 1) {
				while (idx == last_idx)
					idx = dist (rng);
			}
			last_idx = idx;
			return msgs [idx];
		};

		const char* msg{ nullptr };

		switch (event->type) {
			case impacts::event_type::kill:
				msg = pick_no_repeat (kill_msgs, last_kill);
				break;
			case impacts::event_type::hit:
				msg = pick_no_repeat (hit_msgs, last_hit);
				break;
			case impacts::event_type::miss:
				msg = pick_no_repeat (miss_msgs, last_miss);
				break;
			default:
				return;
		}

		static const auto label = [ ]( const char* text ) -> std::string
			{
				constexpr float sr = 0xFF, sg = 0xB6, sb = 0xC1;
				constexpr float er = 0xAD, eg = 0xD8, eb = 0xE6;

				const auto len = std::strlen( text );
				if ( len == 0 )
				{
					return {};
				}

				std::string result{};
				result.reserve( len * 40 );

				for ( auto i = 0ull; i < len; ++i )
				{
					const auto t = len > 1 ? static_cast< float >( i ) / static_cast< float >( len - 1 ) : 0.0f;
					const auto r = static_cast< std::uint8_t >( sr + ( er - sr ) * t );
					const auto g = static_cast< std::uint8_t >( sg + ( eg - sg ) * t );
					const auto b = static_cast< std::uint8_t >( sb + ( eb - sb ) * t );

					char tag[ 48 ];
					std::snprintf( tag, sizeof( tag ), "<font color='#%02X%02X%02X'>%c</font>", r, g, b, text[ i ] );

					result += tag;
				}

				return result;
			}( "velocity" );

		char buf[ 1024 ];
		std::snprintf( buf, sizeof( buf ), "%s <font color='#CCCCCC'>- %s</font>", label.c_str( ), msg );

		std::uint8_t flags[ 2 ]{ 1, 0 };

		memory::call<void>(PATTERN (patterns::set_voice_data), voice, buf, 0xFFFFFFFF, flags );
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