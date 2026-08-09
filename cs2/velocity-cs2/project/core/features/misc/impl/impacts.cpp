#include <pch/pch.hpp>
#include <ShlObj.h>
#include <filesystem>

#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/rendering/rendering.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::misc {

	namespace detail {

		constexpr std::uint32_t invalid_particle_effect{ static_cast<std::uint32_t>( -1 ) };
		constexpr std::uint8_t k_periwinkle_start_r{ 130 };
		constexpr std::uint8_t k_periwinkle_start_g{ 160 };
		constexpr std::uint8_t k_periwinkle_start_b{ 240 };
		constexpr std::uint8_t k_periwinkle_end_r{ 200 };
		constexpr std::uint8_t k_periwinkle_end_g{ 220 };
		constexpr std::uint8_t k_periwinkle_end_b{ 255 };

		[[nodiscard]] std::string chat_white( std::string_view text )
		{
			return std::format( "<font color='#FFFFFF'>{}</font>", text );
		}

		[[nodiscard]] std::string chat_dim( std::string_view text )
		{
			return std::format( "<font color='#CCCCCC'>{}</font>", text );
		}

		// Whole-message solid colors: kill = green, hit = yellow, miss = red;
		// the brand prefix rendered by chat_print keeps its periwinkle
		// gradient.
		[[nodiscard]] std::string chat_hit( std::string_view text )
		{
			return std::format( "<font color='#55FF55'>{}</font>", text );
		}

		[[nodiscard]] std::string chat_hit_yellow( std::string_view text )
		{
			return std::format( "<font color='#FFD700'>{}</font>", text );
		}

		[[nodiscard]] std::string chat_miss( std::string_view text )
		{
			return std::format( "<font color='#FF5555'>{}</font>", text );
		}

		[[nodiscard]] std::string escape_chat_text( std::string_view text )
		{
			std::string escaped{};
			escaped.reserve( text.size( ) );

			for ( const auto ch : text )
			{
				const auto byte = static_cast< unsigned char >( ch );
				if ( byte < 0x20 || byte == 0x7f )
				{
					escaped += ' ';
					continue;
				}

				switch ( ch )
				{
				case '&': escaped += "&amp;"; break;
				case '<': escaped += "&lt;"; break;
				case '>': escaped += "&gt;"; break;
				case '\"': escaped += "&quot;"; break;
				default: escaped += ch; break;
				}
			}

			return escaped;
		}

		[[nodiscard]] std::string sanitize_console_field( std::string_view text )
		{
			std::string sanitized{};
			sanitized.reserve( text.size( ) );

			for ( const auto ch : text )
			{
				const auto byte = static_cast< unsigned char >( ch );
				sanitized += byte < 0x20 || byte == 0x7f ? ' ' : ( ch == '"' ? '\'' : ch );
			}

			return sanitized;
		}

		[[nodiscard]] std::string format_metric( float value, std::string_view suffix = {} )
		{
			if ( !std::isfinite( value ) || value < 0.0f )
			{
				return "n/a";
			}

			return std::format( "{:.3f}{}", value, suffix );
		}

		[[nodiscard]] std::string format_vector( const math::vector3& value, bool valid = true )
		{
			if ( !valid || !std::isfinite( value.x ) || !std::isfinite( value.y ) || !std::isfinite( value.z ) )
			{
				return "n/a";
			}

			return std::format( "({:.2f},{:.2f},{:.2f})", value.x, value.y, value.z );
		}

		// kill (health <= 0) = green, otherwise yellow.
		[[nodiscard]] std::string chat_hit_colored( std::string_view text, int health )
		{
			return health <= 0 ? chat_hit( text ) : chat_hit_yellow( text );
		}

		[[nodiscard]] std::string format_hit_chat_message( const std::string& name, int damage, const std::string& hitgroup, int health, const std::string& reason = {} )
		{
			const auto damage_str = std::to_string( damage );

			if ( !reason.empty( ) )
			{
				return chat_hit_colored( std::format( "命中 {} 造成 {} 伤害（{}），{}（剩余 {}）", name, damage_str, hitgroup, reason, health ), health );
			}

			return chat_hit_colored( std::format( "命中 {} 造成 {} 伤害（{}）（剩余 {}）", name, damage_str, hitgroup, health ), health );
		}

		[[nodiscard]] std::string format_knife_chat_message( const std::string& name, int damage, int health )
		{
			return chat_hit_colored( std::format( "刀杀 {} 造成 {} 伤害（剩余 {}）", name, damage, health ), health );
		}

		[[nodiscard]] std::string format_taser_chat_message( const std::string& name )
		{
			return chat_hit_yellow( std::format( "电击了 {}", name ) );
		}

		[[nodiscard]] std::string make_gradient_label( const char* text, std::uint8_t sr, std::uint8_t sg, std::uint8_t sb, std::uint8_t er, std::uint8_t eg, std::uint8_t eb )
		{
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
		}

		bool chat_print( const char* label_text, std::uint8_t sr, std::uint8_t sg, std::uint8_t sb, std::uint8_t er, std::uint8_t eg, std::uint8_t eb, const char* msg )
		{
			const auto local = systems::g_local.get( );
			// Miss/hit confirmations resolve 0.35-1s after the shot, so the
			// local player can be dead by the time the log fires. The chat
			// is still visible in death cam - keep sending instead of
			// silently dropping the log.
			if ( !local.controller || systems::g_local.is_in_cinematic( ) )
			{
				return false;
			}

			const auto hud_element = memory::call<std::uintptr_t>( PATTERN (patterns::find_hud_element), xs( "CCSGO_HudVoiceStatus" ) );
			if ( !hud_element )
			{
				return false;
			}

			const auto voice = hud_element - 32;
			const auto label = make_gradient_label( label_text, sr, sg, sb, er, eg, eb );

			char buf[ 1024 ];
			std::snprintf( buf, sizeof( buf ), "%s <font color='#CCCCCC'>- </font>%s", label.c_str( ), msg );

			std::uint8_t flags[ 2 ]{ 1, 0 };

			const auto set_voice_data = PATTERN( patterns::set_voice_data );
			if ( !set_voice_data )
			{
				return false;
			}

			memory::call<void>(set_voice_data, voice, buf, 0xFFFFFFFF, flags );
			return true;
		}

		bool chat_print_velocity( const char* msg )
		{
			return chat_print( "[velocity plus]", k_periwinkle_start_r, k_periwinkle_start_g, k_periwinkle_start_b, k_periwinkle_end_r, k_periwinkle_end_g, k_periwinkle_end_b, msg );
		}

	} // namespace detail

	void impacts::on_render_early( xdraw::draw_list& draw_list )
	{
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );

		this->render_hit_effect( draw_list, current_time );
		this->render_bullet_impact_overlays( draw_list, current_time );
	}

	void impacts::on_frame_stage_notify( )
	{
		{
			// Resolve shot state and mutate shared render data on the game
			// thread. Native console/chat calls are drained after unlocking.
			std::unique_lock lock( this->m_mtx );
			this->check_misses( );

			if ( this->m_buffered_impact_time > 0.0f
				&& systems::g_local.get( ).is_valid( ) && systems::g_view.has_camera( ) )
			{
				this->flush_buffered_impacts( );
				this->m_buffered_impacts.clear( );
				this->m_buffered_impact_time = -1.0f;
			}
		}

		this->flush_miss_outputs( );
	}

	void impacts::on_level_change( )
	{
		std::unique_lock lock( this->m_mtx );

		this->m_hitmarkers.clear( );
		this->m_logs.clear( );
		this->m_pending_hits.clear( );
		this->m_pending_shots.clear( );
		this->m_pending_miss_outputs.clear( );
		this->m_bullet_impacts.clear( );
		this->m_buffered_impacts.clear( );
		this->m_buffered_impact_time = -1.0f;
		this->m_hit_effect_time = 0.0f;
		this->m_active_fire_sequence = 0;
		this->m_next_shot_sequence = 1;
		this->m_next_impact_event_sequence = 1;
		++this->m_session_epoch;
	}

	void impacts::on_round_start( )
	{
		// Deliver already-finalized results before the new round resets the
		// correlation state. Uncommitted/in-flight intents are intentionally
		// cancelled at the boundary.
		this->flush_miss_outputs( );
		std::unique_lock lock( this->m_mtx );
		this->m_pending_hits.clear( );
		this->m_pending_shots.clear( );
		this->m_active_fire_sequence = 0;
		this->m_next_shot_sequence = 1;
		this->m_next_impact_event_sequence = 1;
		++this->m_session_epoch;
	}

	void impacts::on_render( xdraw::draw_list& draw_list )
	{
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );

		this->render_hit_markers( draw_list, current_time );
		this->render_logs( draw_list, current_time );
	}

	void impacts::on_report_hit( std::uintptr_t msg )
	{
		const auto& cfg = settings::g_misc.m_impacts;

		if ( !cfg.hit_marker.value && !cfg.hit_sound.value && !cfg.hit_effect.value )
		{
			return;
		}

		const auto position = memory::read<math::vector3>( msg + 0x18 );
		if ( !std::isfinite( position.x ) || !std::isfinite( position.y ) || !std::isfinite( position.z ) || position.length_sqr( ) < 1.0f )
		{
			return;
		}

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );

		std::unique_lock lock( this->m_mtx );

		this->m_pending_hits.push_back( { position, current_time } );

		if ( this->m_pending_hits.size( ) > 10 )
		{
			this->m_pending_hits.erase( this->m_pending_hits.begin( ) );
		}
	}

	void impacts::on_player_hurt( std::uintptr_t event )
	{
		if ( !event )
		{
			return;
		}

		const auto data = this->parse_event( event );
		if ( !data.victim_pawn )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_impacts;
		const auto is_kill = data.health <= 0;

		if ( cfg.hit_marker )
		{
			const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
			const auto current_time = memory::read<float>( global_vars + 0x30 );
			auto position = math::vector3{};
			auto has_position{ false };

			// Report-hit contains the most accurate contact point, but it is not
			// guaranteed to arrive before player_hurt. Use it when available and
			// fall back to the confirmed shot impact or the victim's position.
			const auto game_scene_node = memory::read<std::uintptr_t>( data.victim_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			if ( game_scene_node )
			{
				const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
				const auto view_offset = memory::read<math::vector3>( data.victim_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );

				position = origin + view_offset * 0.75f;
				has_position = true;
			}

			std::unique_lock lock( this->m_mtx );

			auto best_idx{ SIZE_MAX };
			auto best_diff{ 0.5f };

			for ( auto i = 0ull; i < this->m_pending_hits.size( ); ++i )
			{
				const auto diff = std::abs( current_time - this->m_pending_hits[ i ].time );
				if ( diff < best_diff )
				{
					best_diff = diff;
					best_idx = i;
				}
			}

			if ( best_idx != SIZE_MAX )
			{
				position = this->m_pending_hits[ best_idx ].position;
				has_position = true;
				this->m_pending_hits.erase( this->m_pending_hits.begin( ) + best_idx );
			}
			else
			{
				for ( auto it = this->m_pending_shots.rbegin( ); it != this->m_pending_shots.rend( ); ++it )
				{
					if ( it->victim_pawn == data.victim_pawn && it->impact_confirmed && current_time - it->impact_time <= 1.0f )
					{
						position = it->impact_position;
						has_position = true;
						break;
					}
				}
			}

			if ( has_position )
			{
				this->m_hitmarkers.push_back( { position, current_time, data.damage } );

				if ( this->m_hitmarkers.size( ) > 10 )
				{
					this->m_hitmarkers.erase( this->m_hitmarkers.begin( ) );
				}
			}

			std::erase_if( this->m_pending_hits, [ & ]( const auto& entry ) { return current_time - entry.time > 0.5f; } );
		}

		if ( is_kill && cfg.death_sound.value )
		{
			this->play_sound( cfg.death_sound_type, cfg.death_sound_volume, cfg.custom_death_sound.value );
		}

		if ( cfg.hit_sound.value )
		{
			this->play_sound( cfg.hit_sound_type, cfg.hit_sound_volume, cfg.custom_hit_sound.value );
		}

		if ( is_kill && cfg.death_effect.value )
		{
			this->play_death_effect( data.victim_pawn );
		}

		if ( cfg.hit_effect.value )
		{
			this->play_hit_effect( data.victim_pawn );
		}

		if ( cfg.hit_log.value )
		{
			this->add_hit_log( data );
		}
	}

	void impacts::on_weapon_fire( std::uintptr_t event )
	{
		if ( !event )
		{
			return;
		}

		const auto userid_key = cstypes::event_hash{ 0, "userid" };
		const auto controller = memory::call<std::uintptr_t>( PATTERN( patterns::game_event_get_controller ), event, &userid_key );
		if ( controller != systems::g_local.get( ).controller )
		{
			return;
		}

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );
		const auto event_weapon_type = features::combat::g_shared.ctx( ).weapon_type;
		const auto event_weapon_id = features::combat::g_shared.ctx( ).item_def_idx;
		auto expected_fire_age{ 0.0f };
		auto max_fire_age{ 0.75f };
		if ( const auto net_channel = memory::call<std::uintptr_t>( PATTERN( patterns::get_net_channel ), 0, 0 ) )
		{
			const auto outgoing = memory::call_vfunc<float>( net_channel, 10, 0 );
			const auto incoming = memory::call_vfunc<float>( net_channel, 10, 1 );
			if ( std::isfinite( outgoing ) && outgoing >= 0.0f && outgoing < 1.0f
				&& std::isfinite( incoming ) && incoming >= 0.0f && incoming < 1.0f )
			{
				expected_fire_age = outgoing + incoming;
				max_fire_age = std::clamp( expected_fire_age + 0.35f, 0.5f, 2.0f );
			}
		}

		std::unique_lock lock( this->m_mtx );
		shot_record* matched{};
		auto best_age_error{ FLT_MAX };

		for ( auto& shot : this->m_pending_shots )
		{
			if ( shot.resolved || shot.fire_confirmed || !shot.committed )
			{
				continue;
			}

			const auto committed_at = shot.committed_time > 0.0f ? shot.committed_time : shot.time;
			const auto age = current_time - committed_at;
			if ( age >= -0.05f && age <= max_fire_age )
			{
				const auto age_error = std::fabsf( age - expected_fire_age );
				if ( age_error < best_age_error )
				{
					best_age_error = age_error;
					matched = &shot;
				}
			}
		}

		// Server fire events are ordered. They may only confirm an attack
		// expression that Ragebot actually committed into a user command.
		// The active client weapon is deliberately not used: this event is
		// delayed and can arrive after a weapon switch. Match the intent whose
		// age is closest to the measured round trip; this lets a missing A fire
		// leave a gap instead of shifting B's evidence onto A.

		// Every local fire event terminates the previous shotgun pellet
		// bucket, even when the new fire does not belong to Ragebot.
		this->m_active_fire_sequence = 0;
		if ( !matched )
		{
			// A held manual/legit weapon can fire without a new command edge.
			// Materialize an anonymous non-Rage token at the event boundary so
			// its impact/hurt cannot fall through into an older Rage record.
			this->m_pending_shots.push_back(
				{
					.sequence = this->m_next_shot_sequence++,
					.session_epoch = this->m_session_epoch,
					.command_number = -1,
					.target_name = "non-rage-event",
					.time = current_time,
					.committed_time = current_time,
					.fire_time = current_time,
					.committed = true,
					.ragebot = false,
					.fire_confirmed = true,
					.weapon_type = event_weapon_type,
					.weapon_id = static_cast< std::uint16_t >( event_weapon_id ),
				} );
			matched = &this->m_pending_shots.back( );
		}

		if ( matched )
		{
			matched->fire_confirmed = true;
			matched->fire_time = current_time;
			matched->miss_pending = false;
			this->m_active_fire_sequence = matched->sequence;
		}
	}

	void impacts::on_bullet_impact( std::uintptr_t event )
	{
		if ( !event )
		{
			return;
		}

		const auto userid_key = cstypes::event_hash{ 0, "userid" };
		const auto controller = memory::call<std::uintptr_t>(PATTERN (patterns::game_event_get_controller), event, &userid_key );

		if ( controller != systems::g_local.get( ).controller )
		{
			return;
		}

		const auto x = memory::call<float>(PATTERN (patterns::game_event_get_float), event, "x", 0.0f );
		const auto y = memory::call<float>(PATTERN (patterns::game_event_get_float), event, "y", 0.0f );
		const auto z = memory::call<float>(PATTERN (patterns::game_event_get_float), event, "z", 0.0f );

		{
			std::unique_lock lock( this->m_mtx );
			const auto pos = math::vector3{ x, y, z };
			const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
			const auto current_time = memory::read<float>( global_vars + 0x30 );
			shot_record* matched_shot{};
			const auto in_window = [ & ]( const shot_record& shot )
			{
				const auto age = current_time - shot.time;
				return age >= -0.05f && age <= 2.5f;
			};

			// weapon_fire is the strongest event boundary available. Bind the
			// next impact to that exact token; shotgun tokens stay active for all
			// pellets until the next local weapon_fire changes the sequence.
			for ( auto& shot : this->m_pending_shots )
			{
				if ( shot.sequence == this->m_active_fire_sequence
					&& ( !shot.resolved || shot.hurt_confirmed )
					&& ( !shot.impact_confirmed || shot.weapon_type == cstypes::weapon_type::shotgun )
					&& in_window( shot ) )
				{
					matched_shot = &shot;
					break;
				}
			}

			// A hit can be reported before its impact on a reordered event
			// stream. Consume that late impact with the retained hit tombstone,
			// but never ahead of a newer explicit weapon_fire boundary.
			if ( !matched_shot )
			{
				for ( auto& shot : this->m_pending_shots )
				{
					if ( shot.hurt_confirmed && !shot.impact_confirmed && in_window( shot ) )
					{
						matched_shot = &shot;
						break;
					}
				}
			}

			// Fire and impact evidence are both ordered. Geometry is analyzed
			// after binding and must never reorder two rapid Ragebot shots.
			if ( !matched_shot )
			{
				for ( auto& shot : this->m_pending_shots )
				{
					if ( !shot.resolved && shot.fire_confirmed && !shot.impact_confirmed && in_window( shot ) )
					{
						matched_shot = &shot;
						break;
					}
				}
			}

			if ( !matched_shot )
			{
				for ( auto& shot : this->m_pending_shots )
				{
					if ( !shot.resolved && shot.committed && !shot.impact_confirmed && in_window( shot ) )
					{
						matched_shot = &shot;
						break;
					}
				}
			}

			if ( matched_shot )
			{
				matched_shot->committed = true;
				if ( !matched_shot->fire_confirmed ) matched_shot->fire_time = current_time;
				matched_shot->fire_confirmed = true;
				matched_shot->miss_pending = false;
				++matched_shot->impact_count;
				matched_shot->impact_event_sequence = this->m_next_impact_event_sequence++;
				const auto target_origin = matched_shot->aim_position.length_sqr( ) > 1.0f
					? matched_shot->aim_position
					: matched_shot->skeleton[ 0 ].position;
				const auto dist_sq = ( pos - target_origin ).length_sqr( );
				matched_shot->impact_time = current_time;

				if ( !matched_shot->impact_confirmed || dist_sq < matched_shot->best_impact_dist_sq )
				{
					matched_shot->impact_position = pos;
					matched_shot->best_impact_dist_sq = dist_sq;
					matched_shot->impact_confirmed = true;
				}
			}

			this->check_misses( );

			const auto& cfg = settings::g_misc.m_impacts;
			if ( cfg.bullet_impact_effect.value || cfg.bullet_tracers.value )
			{
				const auto type = cfg.bullet_impact_effect_type.value;
				const auto show_overlay = type == settings::misc::impacts::bullet_impact_type::overlay || type == settings::misc::impacts::bullet_impact_type::both;

				if ( cfg.bullet_tracers.value || show_overlay )
				{
					const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
					const auto current_time = memory::read<float>( global_vars + 0x30 );

					if ( current_time != this->m_buffered_impact_time )
					{
						this->flush_buffered_impacts( );

						this->m_buffered_impact_time = current_time;
						this->m_buffered_impacts.clear( );

						const auto local_pawn = systems::g_local.get( ).pawn;
						if ( local_pawn )
						{
							const auto game_scene_node = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
							if ( game_scene_node )
							{
								const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
								const auto view_offset = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
								this->m_buffered_eye_position = origin + view_offset;
							}
							else
							{
								this->m_buffered_impact_time = -1.0f;
							}
						}
						else
						{
							this->m_buffered_impact_time = -1.0f;
						}
					}

					if ( this->m_buffered_impact_time == current_time )
					{
						this->m_buffered_impacts.push_back( math::vector3{ x, y, z } );
					}
				}
			}
		}

		const auto& cfg = settings::g_misc.m_impacts;
		if ( cfg.bullet_impact_effect.value )
		{
			const auto type = cfg.bullet_impact_effect_type.value;
			const auto show_sparks = type == settings::misc::impacts::bullet_impact_type::sparks || type == settings::misc::impacts::bullet_impact_type::both;

			if ( show_sparks )
			{
				this->play_bullet_impact_effect( math::vector3{ x, y, z } );
			}
		}
	}

	void impacts::on_base_fire_guns_get_inaccuracy( std::uintptr_t weapon, float inaccuracy )
	{
		const auto owner_handle = memory::read<std::uint32_t>( weapon + SCHEMA( "C_BaseEntity", "m_hOwnerEntity"_hash ) );
		const auto owner = systems::g_entities.lookup( owner_handle );

		if ( !owner || owner != systems::g_local.get( ).pawn )
		{
			return;
		}

		const auto weapon_id = memory::read<std::uint16_t>( weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_hash )
			+ SCHEMA( "C_AttributeContainer", "m_Item"_hash ) + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );
		std::unique_lock lock( this->m_mtx );

		const auto bind_inaccuracy = [ & ]( bool require_weapon )
		{
			for ( auto it = this->m_pending_shots.rbegin( ); it != this->m_pending_shots.rend( ); ++it )
			{
				auto& shot = *it;
				const auto committed_at = shot.committed_time > 0.0f ? shot.committed_time : shot.time;
				const auto age = current_time - committed_at;
				if ( !shot.resolved && shot.committed && !shot.inaccuracy_confirmed
					&& age >= -0.05f && age <= 0.75f && ( !require_weapon || shot.weapon_id == weapon_id ) )
				{
					shot.server_inaccuracy = inaccuracy;
					shot.inaccuracy_confirmed = true;
					return true;
				}
			}
			return false;
		};

		if ( weapon_id ) bind_inaccuracy( true );
		else bind_inaccuracy( false );
	}

	void impacts::on_get_interpolated_shoot_position( std::uintptr_t weapon_services, float* out )
	{
		const auto local_pawn = systems::g_local.get( ).pawn;
		if ( !local_pawn )
		{
			return;
		}

		const auto local_weapon_services = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
		if ( !local_weapon_services || weapon_services != local_weapon_services )
		{
			return;
		}

		const auto shoot_position = math::vector3{ out[ 0 ], out[ 1 ], out[ 2 ] };
		const auto weapon_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
		const auto weapon = weapon_handle ? systems::g_entities.lookup( weapon_handle ) : 0;
		const auto weapon_id = weapon ? memory::read<std::uint16_t>( weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_hash )
			+ SCHEMA( "C_AttributeContainer", "m_Item"_hash ) + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) ) : 0;
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );

		std::unique_lock lock( this->m_mtx );

		const auto bind_shoot_position = [ & ]( bool require_weapon )
		{
			for ( auto it = this->m_pending_shots.rbegin( ); it != this->m_pending_shots.rend( ); ++it )
			{
				auto& shot = *it;
				const auto committed_at = shot.committed_time > 0.0f ? shot.committed_time : shot.time;
				const auto age = current_time - committed_at;
				if ( !shot.resolved && shot.committed && !shot.server_shoot_position_confirmed
					&& age >= -0.05f && age <= 0.75f && ( !require_weapon || shot.weapon_id == weapon_id ) )
				{
					shot.server_shoot_position = shoot_position;
					shot.server_shoot_position_confirmed = true;
					return true;
				}
			}
			return false;
		};

		if ( weapon_id ) bind_shoot_position( true );
		else bind_shoot_position( false );
	}

	void impacts::on_shot_committed( std::intptr_t command_number )
	{
		if ( command_number < 0 )
		{
			return;
		}

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );
		std::unique_lock lock( this->m_mtx );

		for ( auto it = this->m_pending_shots.rbegin( ); it != this->m_pending_shots.rend( ); ++it )
		{
			if ( it->command_number == command_number && !it->resolved )
			{
				if ( !it->committed )
				{
					it->committed = true;
					it->committed_time = current_time;
				}
				return;
			}
		}
	}

	void impacts::on_command_finalized( std::intptr_t command_number, bool primary_attack_edge, bool primary_attack_ready )
	{
		if ( command_number < 0 || ( !primary_attack_edge && !primary_attack_ready ) )
		{
			return;
		}

		const auto& weapon = features::combat::g_shared.ctx( );
		if ( !weapon.valid || weapon.weapon_type < cstypes::weapon_type::pistol
			|| weapon.weapon_type > cstypes::weapon_type::lmg
			|| ( weapon.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver
				&& features::combat::g_rage.is_cocking_revolver( ) ) )
		{
			return;
		}

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );
		std::unique_lock lock( this->m_mtx );

		// A Ragebot shot already owns this command. Only create a sentinel for
		// final attack input that no Rage record claimed (manual/legit fire).
		for ( const auto& shot : this->m_pending_shots )
		{
			if ( shot.session_epoch == this->m_session_epoch && shot.command_number == command_number )
			{
				return;
			}
		}

		// A real subtick edge always represents a distinct fire intent (including
		// manual double-tap). Only throttle inferred held-fire placeholders.
		if ( !primary_attack_edge )
		{
			const auto cycle_time = weapon.weapon_vdata
				? memory::read<float>( weapon.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flCycleTime"_hash ) ) : 0.1f;
			const auto token_gap = std::clamp( std::isfinite( cycle_time ) ? cycle_time * 0.5f : 0.05f, 0.02f, 0.25f );

			for ( auto it = this->m_pending_shots.rbegin( ); it != this->m_pending_shots.rend( ); ++it )
			{
				if ( !it->ragebot && it->weapon_id == weapon.item_def_idx && current_time - it->time < token_gap )
				{
					return;
				}
			}
		}

		this->m_pending_shots.push_back(
			{
				.sequence = this->m_next_shot_sequence++,
				.session_epoch = this->m_session_epoch,
				.command_number = command_number,
				.target_name = "non-rage",
				.time = current_time,
				.committed_time = current_time,
				.committed = true,
				.ragebot = false,
				.weapon_type = weapon.weapon_type,
				.weapon_id = static_cast< std::uint16_t >( weapon.item_def_idx ),
			} );

		constexpr auto max_pending_shots{ 128ull };
		while ( this->m_pending_shots.size( ) > max_pending_shots )
		{
			const auto& oldest = this->m_pending_shots.front( );
			if ( oldest.ragebot && oldest.committed && !oldest.resolved )
			{
				this->add_miss_log( oldest,
					{ .reason = miss_reason::tracker_overflow, .confidence = miss_confidence::low } );
			}
			this->m_pending_shots.erase( this->m_pending_shots.begin( ) );
		}
	}

	void impacts::on_boom( const shot_parameters& params )
	{
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );
		const auto target_name = this->get_player_name_from_pawn( params.victim_pawn );
		const auto controller_handle = memory::read<std::uint32_t>( params.victim_pawn + SCHEMA( "C_BasePlayerPawn", "m_hController"_hash ) );
		const auto target_velocity = memory::read<math::vector3>( params.victim_pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
		const auto& weapon = features::combat::g_shared.ctx( );

		std::unique_lock lock( this->m_mtx );
		shot_record record
			{
				.sequence = this->m_next_shot_sequence,
				.session_epoch = this->m_session_epoch,
				.command_number = params.command_number,
				.victim_pawn = params.victim_pawn,
				.victim_controller_handle = controller_handle,
				.target_name = target_name,
				.hitgroup = params.hitgroup,
				.target_health = params.target_health,
				.damage = params.damage,
				.hitchance = params.hitchance,
				.predicted_inaccuracy = params.inaccuracy,
				.predicted_spread = params.spread,
				.server_inaccuracy = 0.0f,
				.aim_angle = params.aim_angle,
				.aim_position = params.aim_position,
				.shoot_position = params.shoot_position,
				.tick = params.tick,
				.stamp_tick = params.stamp_tick,
				.time = current_time,
				.skeleton = params.skeleton,
				.target_velocity = target_velocity,
				.forced = params.forced,
				.extrapolated = params.extrapolated,
				.penetrated = params.penetrated,
				.seed_mode = params.seed_mode,
				.dt = params.dt,
				.weapon_type = weapon.weapon_type,
				.weapon_id = static_cast< std::uint16_t >( weapon.item_def_idx ),
			};

		if ( params.command_number >= 0 )
		{
			for ( auto& pending : this->m_pending_shots )
			{
				if ( pending.session_epoch == this->m_session_epoch && pending.command_number == params.command_number )
				{
					if ( !pending.ragebot )
					{
						record.sequence = pending.sequence;
						pending = std::move( record );
						return;
					}

					// CreateMove can replay one command. Refresh an uncommitted
					// intent instead of creating a second logical shot.
					if ( !pending.committed && !pending.fire_confirmed && !pending.impact_confirmed )
					{
						record.sequence = pending.sequence;
						pending = std::move( record );
					}
					return;
				}
			}
		}

		++this->m_next_shot_sequence;
		this->m_pending_shots.push_back( std::move( record ) );

		constexpr auto max_pending_shots{ 128ull };
		while ( this->m_pending_shots.size( ) > max_pending_shots )
		{
			if ( this->m_pending_shots.front( ).ragebot && this->m_pending_shots.front( ).committed
				&& !this->m_pending_shots.front( ).resolved )
			{
				this->add_miss_log( this->m_pending_shots.front( ),
					{ .reason = miss_reason::tracker_overflow, .confidence = miss_confidence::low } );
			}
			this->m_pending_shots.erase( this->m_pending_shots.begin( ) );
		}
	}

	const char* impacts::miss_reason_code( miss_reason reason )
	{
		switch ( reason )
		{
		case miss_reason::server_rejected: return "server_rejected";
		case miss_reason::impact_missing: return "impact_missing";
		case miss_reason::shoot_origin_mismatch: return "shoot_origin_mismatch";
		case miss_reason::prediction_mismatch: return "prediction_mismatch";
		case miss_reason::trajectory_mismatch: return "trajectory_mismatch";
		case miss_reason::occlusion: return "occlusion";
		case miss_reason::penetration_failed: return "penetration_failed";
		case miss_reason::spread: return "spread";
		case miss_reason::lag_compensation: return "lagcomp";
		case miss_reason::extrapolation_mismatch: return "extrapolation_mismatch";
		case miss_reason::record_mismatch: return "record_mismatch";
		case miss_reason::tracker_overflow: return "tracker_overflow";
		default: return "unknown";
		}
	}

	const char* impacts::miss_reason_label( miss_reason reason )
	{
		switch ( reason )
		{
		case miss_reason::server_rejected: return "服务器未确认";
		case miss_reason::impact_missing: return "无弹着点";
		case miss_reason::shoot_origin_mismatch: return "射击位置偏差";
		case miss_reason::prediction_mismatch: return "精度预测偏差";
		case miss_reason::trajectory_mismatch: return "弹着轨迹不匹配";
		case miss_reason::occlusion: return "障碍物";
		case miss_reason::penetration_failed: return "穿透失败";
		case miss_reason::spread: return "散布";
		case miss_reason::lag_compensation: return "延迟补偿";
		case miss_reason::extrapolation_mismatch: return "外推偏差";
		case miss_reason::record_mismatch: return "记录偏差";
		case miss_reason::tracker_overflow: return "跟踪队列溢出";
		default: return "未知";
		}
	}

	const char* impacts::miss_confidence_name( miss_confidence confidence )
	{
		switch ( confidence )
		{
		case miss_confidence::high: return "high";
		case miss_confidence::medium: return "medium";
		default: return "low";
		}
	}

	impacts::miss_analysis impacts::analyze_shot( const shot_record& shot ) const
	{
		miss_analysis result{};

		if ( !shot.fire_confirmed && !shot.impact_confirmed )
		{
			result.reason = miss_reason::server_rejected;
			result.confidence = miss_confidence::medium;
			return result;
		}

		if ( !shot.impact_confirmed )
		{
			result.reason = miss_reason::impact_missing;
			result.confidence = miss_confidence::medium;
			return result;
		}

		const auto shoot_position = shot.server_shoot_position_confirmed ? shot.server_shoot_position : shot.shoot_position;
		if ( shot.server_shoot_position_confirmed )
		{
			result.shoot_origin_delta = ( shot.server_shoot_position - shot.shoot_position ).length( );
		}

		math::vector3 ideal_forward{};
		math::helpers::angle_vectors_left( shot.aim_angle, &ideal_forward );
		ideal_forward = ideal_forward.normalized( );

		const auto to_impact = shot.impact_position - shoot_position;
		result.impact_distance = to_impact.length( );
		const auto target_position = shot.aim_position.length_sqr( ) > 1.0f ? shot.aim_position : shot.skeleton[ 0 ].position;
		result.target_distance = ( target_position - shoot_position ).length( );

		if ( result.impact_distance <= 0.1f )
		{
			result.reason = miss_reason::unknown;
			return result;
		}

		const auto impact_direction = to_impact * ( 1.0f / result.impact_distance );
		const auto angular_deviation = std::acosf( std::clamp( ideal_forward.dot( impact_direction ), -1.0f, 1.0f ) );
		result.angular_deviation_deg = math::helpers::rad_to_deg( angular_deviation );

		const auto inaccuracy = shot.inaccuracy_confirmed ? shot.server_inaccuracy : shot.predicted_inaccuracy;
		const auto spread_cone = std::atanf( std::max( inaccuracy, 0.0f ) + std::max( shot.predicted_spread, 0.0f ) );
		result.spread_cone_deg = math::helpers::rad_to_deg( spread_cone );

		if ( shot.victim_pawn && systems::g_entities.exists( shot.victim_pawn ) )
		{
			const auto normalize_metric = [ ]( float value )
			{
				return std::isfinite( value ) && value < FLT_MAX * 0.5f ? std::max( value, 0.0f ) : -1.0f;
			};

			result.impact_to_hitbox = normalize_metric( this->distance_to_nearest_hitbox( shot ) );
			result.impact_ray_to_hitbox = normalize_metric( this->ray_distance_to_nearest_hitbox( shot, impact_direction ) );
			result.ideal_ray_to_hitbox = normalize_metric( this->ray_distance_to_nearest_hitbox( shot, ideal_forward ) );
		}

		if ( result.shoot_origin_delta > 1.0f )
		{
			result.reason = miss_reason::shoot_origin_mismatch;
			result.confidence = miss_confidence::high;
			return result;
		}

		const auto inaccuracy_tolerance = std::max( 0.0005f, std::fabsf( shot.predicted_inaccuracy ) * 0.35f );
		if ( shot.inaccuracy_confirmed && std::fabsf( shot.server_inaccuracy - shot.predicted_inaccuracy ) > inaccuracy_tolerance )
		{
			result.reason = miss_reason::prediction_mismatch;
			result.confidence = miss_confidence::high;
			return result;
		}

		const auto trajectory_tolerance = std::max( spread_cone * 3.0f, math::helpers::deg_to_rad( 2.0f ) );
		if ( angular_deviation > trajectory_tolerance )
		{
			result.reason = miss_reason::trajectory_mismatch;
			result.confidence = miss_confidence::high;
			return result;
		}

		if ( result.ideal_ray_to_hitbox >= 0.0f && result.ideal_ray_to_hitbox <= 1.0f
			&& result.target_distance > 0.0f && result.impact_distance + 8.0f < result.target_distance )
		{
			result.reason = shot.penetrated ? miss_reason::penetration_failed : miss_reason::occlusion;
			result.confidence = miss_confidence::high;
			return result;
		}

		if ( result.impact_ray_to_hitbox > 1.0f )
		{
			result.reason = miss_reason::spread;
			result.confidence = miss_confidence::high;
			return result;
		}

		if ( shot.extrapolated )
		{
			result.reason = miss_reason::extrapolation_mismatch;
			result.confidence = miss_confidence::medium;
			return result;
		}

		if ( std::max( shot.stamp_tick - shot.tick, 0 ) > 0 || shot.target_velocity.length_2d( ) >= 5.0f )
		{
			result.reason = miss_reason::lag_compensation;
			result.confidence = miss_confidence::medium;
			return result;
		}

		result.reason = result.impact_ray_to_hitbox >= 0.0f ? miss_reason::record_mismatch : miss_reason::unknown;
		result.confidence = miss_confidence::low;
		return result;
	}

	impacts::hit_data impacts::parse_event( std::uintptr_t event )
	{
		const auto attacker_key = cstypes::event_hash{ 0, "attacker" };
		const auto userid_key = cstypes::event_hash{ 0, "userid" };

		const auto attacker = memory::call<std::uintptr_t>( PATTERN (patterns::game_event_get_controller), event, &attacker_key );
		const auto victim = memory::call<std::uintptr_t>( PATTERN (patterns::game_event_get_controller), event, &userid_key );

		const auto local = systems::g_local.get( );

		if ( attacker != local.controller || victim == local.controller )
		{
			return {};
		}

		const auto victim_pawn = memory::call<std::uintptr_t>(PATTERN (patterns::game_event_get_pawn), event, &userid_key );
		if ( !victim_pawn )
		{
			return {};
		}

		const auto victim_team = memory::read<int>( victim_pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		if ( !local.is_this_other_team( victim_team ) )
		{
			return {};
		}

		const auto damage = memory::call<int>( PATTERN (patterns::game_event_get_int), event, "dmg_health", false );
		const auto hitgroup = memory::call<int>( PATTERN (patterns::game_event_get_int), event, "hitgroup", false );
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );

		auto expected_hitgroup{ 0 };
		auto expected_damage{ 0.0f };
		auto was_aimbot{ false };
		auto weapon_type{ 0u };
		auto backtrack_ticks{ 0 };
		math::vector3 aim_position{};
		std::string mismatch_reason{};
		math::vector3 impact_pos{};

		{
			std::unique_lock lock( this->m_mtx );

			if ( !this->m_pending_hits.empty( ) )
			{
				impact_pos = this->m_pending_hits.back( ).position;
			}

			auto matched_shot = this->m_pending_shots.end( );
			auto latest_impact_sequence{ 0ull };

			// A player_hurt normally follows the bullet_impact that caused it.
			// Correlate by event chronology only: expected hitgroup, expected
			// damage and geometry are outcomes to diagnose, never identity keys.
			for ( auto it = this->m_pending_shots.begin( ); hitgroup > 0 && it != this->m_pending_shots.end( ); ++it )
			{
				if ( it->resolved || !it->impact_confirmed
					|| ( it->ragebot && it->victim_pawn != victim_pawn ) )
				{
					continue;
				}

				if ( it->impact_event_sequence > latest_impact_sequence )
				{
					latest_impact_sequence = it->impact_event_sequence;
					matched_shot = it;
				}
			}

			// A newer explicit weapon_fire is a hard correlation boundary. An
			// older impact (including a non-Rage sentinel) must not cross it and
			// consume a hurt that can belong to the active shot. A non-Rage
			// active sentinel deliberately has no target and consumes the hurt
			// without confirming any Rage shot.
			if ( hitgroup > 0 )
			{
				for ( auto it = this->m_pending_shots.begin( ); it != this->m_pending_shots.end( ); ++it )
				{
					if ( it->sequence == this->m_active_fire_sequence && !it->resolved && it->fire_confirmed
						&& ( !it->ragebot || it->victim_pawn == victim_pawn ) )
					{
						const auto no_impact_candidate = matched_shot == this->m_pending_shots.end( );
						const auto crosses_newer_fire = !no_impact_candidate && matched_shot != it
							&& it->sequence > matched_shot->sequence
							&& it->fire_time >= matched_shot->impact_time;
						if ( no_impact_candidate || crosses_newer_fire ) matched_shot = it;
						break;
					}
				}
			}

			// Some event streams deliver player_hurt before bullet_impact. In
			// that case use the oldest server-confirmed fire for this victim;
			// the retained hit tombstone will absorb its late impact.
			if ( hitgroup > 0 && matched_shot == this->m_pending_shots.end( ) )
			{
				for ( auto it = this->m_pending_shots.begin( ); it != this->m_pending_shots.end( ); ++it )
				{
					if ( it->ragebot && it->victim_pawn == victim_pawn && !it->resolved && it->fire_confirmed )
					{
						matched_shot = it;
						break;
					}
				}
			}

			if ( hitgroup > 0 && matched_shot == this->m_pending_shots.end( ) )
			{
				for ( auto it = this->m_pending_shots.begin( ); it != this->m_pending_shots.end( ); ++it )
				{
					if ( it->ragebot && it->victim_pawn == victim_pawn && !it->resolved && it->committed )
					{
						matched_shot = it;
						break;
					}
				}
			}

			if ( matched_shot != this->m_pending_shots.end( ) )
			{
				weapon_type = matched_shot->weapon_type;
				if ( matched_shot->impact_confirmed )
				{
					impact_pos = matched_shot->impact_position;
				}
				matched_shot->committed = true;
				if ( !matched_shot->fire_confirmed ) matched_shot->fire_time = current_time;
				matched_shot->fire_confirmed = true;
				matched_shot->hurt_confirmed = true;
				matched_shot->resolved = true;
				matched_shot->resolved_time = current_time;

				if ( matched_shot->ragebot )
				{
					expected_hitgroup = matched_shot->hitgroup;
					was_aimbot = true;
					expected_damage = matched_shot->damage;
					backtrack_ticks = std::max( matched_shot->stamp_tick - matched_shot->tick, 0 );
					aim_position = matched_shot->aim_position;

					// Seed-mode hit confirmation feeds the seed-sync verdict.
					if ( matched_shot->seed_mode )
					{
						features::combat::g_shared.note_seed_shot( true );
					}

					if ( expected_hitgroup > 0 && hitgroup != expected_hitgroup )
					{
						mismatch_reason = this->miss_reason_label( this->analyze_shot( *matched_shot ).reason );
					}
				}
			}
		}

		if ( !was_aimbot && weapon_type == 0 )
		{
			if ( local.pawn )
			{
				const auto weapon_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
				if ( weapon_services )
				{
					const auto weapon_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
					const auto weapon = weapon_handle ? systems::g_entities.lookup( weapon_handle ) : 0;

					if ( weapon )
					{
						const auto weapon_vdata = memory::read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 );
						if ( weapon_vdata )
						{
							weapon_type = memory::read<std::uint32_t>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_WeaponType"_hash ) );
						}
					}
				}
			}
		}


		return
		{
			.victim = victim,
			.victim_pawn = victim_pawn,
			.damage = damage,
			.health = memory::call<int>( PATTERN (patterns::game_event_get_int), event, "health", false ),
			.hitgroup = hitgroup,
			.expected_hitgroup = expected_hitgroup,
			.backtrack_ticks = backtrack_ticks,
			.aim_position = aim_position,
			.impact_position = impact_pos,
			.was_aimbot = was_aimbot,
			.mismatch_reason = std::move( mismatch_reason ),
			.weapon_type = weapon_type,
			.expected_damage = expected_damage
		};
	}

	std::string impacts::get_player_name( std::uintptr_t controller )
	{
		const auto name_ptr = memory::read<std::uintptr_t>( controller + SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_hash ) );
		if ( !name_ptr )
		{
			return "unknown";
		}

		return memory::read_string( name_ptr, 64 );
	}

	std::string impacts::get_player_name_from_pawn( std::uintptr_t pawn )
	{
		const auto controller_handle = memory::read<std::uint32_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_hController"_hash ) );
		if ( !controller_handle )
		{
			return "unknown";
		}

		const auto controller = systems::g_entities.lookup( controller_handle );
		if ( !controller )
		{
			return "unknown";
		}

		return this->get_player_name( controller );
	}

	float impacts::distance_to_nearest_hitbox( const shot_record& shot ) const
	{
		if ( shot.impact_position.length_sqr( ) < 1.0f )
		{
			return -1.0f;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( shot.victim_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return -1.0f;
		}

		const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
		if ( hitbox_set.count <= 0 )
		{
			return -1.0f;
		}

		auto best_dist{ FLT_MAX };

		for ( const auto& entry : hitbox_set )
		{
			if ( entry.bone < 0 || entry.bone >= static_cast< int >( shot.skeleton.size( ) ) )
			{
				continue;
			}

			const auto& bone = shot.skeleton[ entry.bone ];
			if ( bone.position.length_sqr( ) < 1.0f )
			{
				continue;
			}

			auto dist{ FLT_MAX };
			if ( entry.radius > 0.001f )
			{
				const auto capsule_start = bone.rotation.rotate_vector( entry.mins ) + bone.position;
				const auto capsule_end = bone.rotation.rotate_vector( entry.maxs ) + bone.position;
				const auto segment = capsule_end - capsule_start;
				const auto segment_length_sq = segment.length_sqr( );
				const auto t = segment_length_sq > 0.001f
					? std::clamp( ( shot.impact_position - capsule_start ).dot( segment ) / segment_length_sq, 0.0f, 1.0f )
					: 0.0f;
				dist = ( shot.impact_position - ( capsule_start + segment * t ) ).length( ) - entry.radius;
			}
			else
			{
				auto inverse = bone.rotation;
				inverse.x = -inverse.x;
				inverse.y = -inverse.y;
				inverse.z = -inverse.z;
				const auto local = inverse.rotate_vector( shot.impact_position - bone.position );
				const math::vector3 closest
				{
					std::clamp( local.x, entry.mins.x, entry.maxs.x ),
					std::clamp( local.y, entry.mins.y, entry.maxs.y ),
					std::clamp( local.z, entry.mins.z, entry.maxs.z )
				};
				dist = ( local - closest ).length( );
			}

			if ( dist < best_dist )
			{
				best_dist = dist;
			}
		}

		return best_dist;
	}

	float impacts::ray_distance_to_nearest_hitbox( const shot_record& shot, const math::vector3& direction ) const
	{
		const auto game_scene_node = memory::read<std::uintptr_t>( shot.victim_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return FLT_MAX;
		}

		const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
		if ( hitbox_set.count <= 0 )
		{
			return FLT_MAX;
		}

		auto best_dist{ FLT_MAX };
		const auto shoot_position = shot.server_shoot_position_confirmed ? shot.server_shoot_position : shot.shoot_position;

		for ( const auto& entry : hitbox_set )
		{
			if ( entry.bone < 0 || entry.bone >= static_cast< int >( shot.skeleton.size( ) ) )
			{
				continue;
			}

			const auto& bone = shot.skeleton[ entry.bone ];
			if ( bone.position.length_sqr( ) < 1.0f )
			{
				continue;
			}

			auto dist{ FLT_MAX };
			if ( entry.radius > 0.001f )
			{
				const auto capsule_start = bone.rotation.rotate_vector( entry.mins ) + bone.position;
				const auto capsule_end = bone.rotation.rotate_vector( entry.maxs ) + bone.position;
				const auto segment = capsule_end - capsule_start;
				const auto to_start = capsule_start - shoot_position;
				const auto perpendicular_segment = segment - direction * segment.dot( direction );
				const auto perpendicular_start = to_start - direction * to_start.dot( direction );
				const auto perpendicular_length_sq = perpendicular_segment.length_sqr( );
				auto segment_t = perpendicular_length_sq > 1.0e-6f
					? std::clamp( -perpendicular_start.dot( perpendicular_segment ) / perpendicular_length_sq, 0.0f, 1.0f )
					: 0.0f;
				auto segment_point = capsule_start + segment * segment_t;
				auto ray_t = ( segment_point - shoot_position ).dot( direction );

				if ( ray_t < 0.0f )
				{
					const auto segment_length_sq = segment.length_sqr( );
					segment_t = segment_length_sq > 1.0e-6f
						? std::clamp( ( shoot_position - capsule_start ).dot( segment ) / segment_length_sq, 0.0f, 1.0f )
						: 0.0f;
					segment_point = capsule_start + segment * segment_t;
					ray_t = 0.0f;
				}

				const auto ray_point = shoot_position + direction * ray_t;
				dist = ( segment_point - ray_point ).length( ) - entry.radius;
			}
			else
			{
				auto inverse = bone.rotation;
				inverse.x = -inverse.x;
				inverse.y = -inverse.y;
				inverse.z = -inverse.z;
				const auto local_origin = inverse.rotate_vector( shoot_position - bone.position );
				const auto local_direction = inverse.rotate_vector( direction );
				auto entry_t{ 0.0f };
				auto exit_t{ FLT_MAX };

				const auto intersect_axis = [ & ]( float origin, float delta, float minimum, float maximum )
				{
					if ( std::fabs( delta ) < 1.0e-8f )
					{
						return origin >= minimum && origin <= maximum;
					}

					auto first = ( minimum - origin ) / delta;
					auto second = ( maximum - origin ) / delta;
					if ( first > second ) std::swap( first, second );
					entry_t = std::max( entry_t, first );
					exit_t = std::min( exit_t, second );
					return entry_t <= exit_t;
				};

				if ( intersect_axis( local_origin.x, local_direction.x, entry.mins.x, entry.maxs.x ) &&
					intersect_axis( local_origin.y, local_direction.y, entry.mins.y, entry.maxs.y ) &&
					intersect_axis( local_origin.z, local_direction.z, entry.mins.z, entry.maxs.z ) )
				{
					dist = 0.0f;
				}
				else
				{
					const auto center = ( entry.mins + entry.maxs ) * 0.5f;
					const auto extents = ( entry.maxs - entry.mins ) * 0.5f;
					const auto to_center = center - local_origin;
					const auto projection = std::max( to_center.dot( local_direction ), 0.0f );
					dist = std::max( ( to_center - local_direction * projection ).length( ) - extents.length( ), 0.0f );
				}
			}

			if ( dist < best_dist )
			{
				best_dist = dist;
			}
		}

		return best_dist;
	}

	void impacts::add_hit_log( const hit_data& data )
	{
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );
		const auto& cfg = settings::g_misc.m_impacts;

		log entry{};
		entry.name = this->get_player_name( data.victim );
		entry.damage = data.damage;
		entry.health = data.health;
		entry.time = current_time;
		entry.offset.set_stiffness( 180.0f );
		entry.offset.set_damping( 16.0f );
		entry.offset.snap( -150.0f );
		entry.offset.set_target( 0.0f );
		entry.alpha.fade_in( 0.15f );
		entry.snapped = false;
		entry.duration = cfg.hit_log_duration;
		entry.hitgroup = systems::g_hitboxes.hitgroup_to_name( data.hitgroup );
		entry.weapon_type = data.weapon_type;

		if ( data.was_aimbot && !data.mismatch_reason.empty( ) )
		{
			const auto expected_name = systems::g_hitboxes.hitgroup_to_name( data.expected_hitgroup );
			entry.reason = std::format( "预期 {}（{}，回溯 {}t）", expected_name, data.mismatch_reason, data.backtrack_ticks );
		}
		else if ( data.was_aimbot && data.health > 0 && data.expected_damage > 0.0f && static_cast< float >( data.damage ) < data.expected_damage )
		{
			entry.reason = std::format( "预期 {} 伤害，实际 {}（回溯 {}t）", data.expected_damage, data.damage, data.backtrack_ticks );
		}

		// When the actual hit landed off the point the aimbot aimed at,
		// note the offset (same hitgroup but a grazing hit, or spread drift).
		if ( data.was_aimbot && data.aim_position.length_sqr( ) > 1.0f && data.impact_position.length_sqr( ) > 1.0f )
		{
			const auto offset = ( data.impact_position - data.aim_position ).length( );
			if ( offset > 5.0f )
			{
				const auto offset_note = std::format( "弹着偏移 {:.0f}u（瞄准点偏差）", offset );
				entry.reason = entry.reason.empty( ) ? offset_note : entry.reason + "，" + offset_note;
			}
		}

		if ( cfg.console_log.value || cfg.chat_log.value )
		{
			std::string plain_msg{};
			std::string chat_msg{};

			if ( data.weapon_type == cstypes::weapon_type::taser )
			{
				plain_msg = std::format( "电击了 {}", entry.name );
				chat_msg = detail::format_taser_chat_message( entry.name );
			}
			else if ( data.weapon_type == cstypes::weapon_type::knife )
			{
				plain_msg = std::format( "刀杀 {} 造成 {} 伤害（剩余 {}）", entry.name, entry.damage, entry.health );
				chat_msg = detail::format_knife_chat_message( entry.name, entry.damage, entry.health );
			}
			else if ( !entry.reason.empty( ) )
			{
				plain_msg = std::format( "命中 {} 造成 {} 伤害（{}），{}（剩余 {}）", entry.name, entry.damage, entry.hitgroup, entry.reason, entry.health );
				chat_msg = detail::format_hit_chat_message( entry.name, entry.damage, entry.hitgroup, entry.health, entry.reason );
			}
			else
			{
				plain_msg = std::format( "命中 {} 造成 {} 伤害（{}）（剩余 {}）", entry.name, entry.damage, entry.hitgroup, entry.health );
				chat_msg = detail::format_hit_chat_message( entry.name, entry.damage, entry.hitgroup, entry.health );
			}

			if ( cfg.console_log.value )
			{
				// Console severity colors: kill = 4, hit = 2 (yellow),
				// miss = 3 (red). Green is only available in the chat log
				// (engine console severity has no green level).
				logging::console::print_severity( entry.health <= 0 ? 4 : 2, xs( "{}" ), plain_msg );
			}

			if ( cfg.chat_log.value )
			{
				detail::chat_print_velocity( chat_msg.c_str( ) );
			}
		}

		if ( cfg.hit_log.value )
		{
			std::unique_lock lock( this->m_mtx );
			this->m_logs.insert( this->m_logs.begin( ), std::move( entry ) );

			if ( this->m_logs.size( ) > 5 )
			{
				this->m_logs.pop_back( );
			}
		}

	}

	void impacts::add_miss_log( const shot_record& shot, const miss_analysis& analysis )
	{
		if ( !shot.ragebot )
		if ( !shot.ragebot )
		{
			return;
		}

		// Shots the server never confirmed (no fire event) are not useful
		// misses - they are usually an attack-expression rejection (e.g.
		// double-tap claim issues), not a trajectory problem, and every
		// one of them spamming a log only masks the real misses.
		if ( analysis.reason == miss_reason::server_rejected )
		{
			return;
		}

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );
		const auto& cfg = settings::g_misc.m_impacts;

		const auto name = shot.target_name.empty( ) ? std::string{ "unknown" } : shot.target_name;
		const auto console_name = detail::sanitize_console_field( name );
		const auto chat_name = detail::escape_chat_text( name );
		const auto group = systems::g_hitboxes.hitgroup_to_name( shot.hitgroup );
		const auto reason_code = this->miss_reason_code( analysis.reason );
		const auto reason_label = this->miss_reason_label( analysis.reason );

		if ( cfg.console_log.value || cfg.chat_log.value )
		{
			pending_miss_output output{};
			if ( cfg.console_log.value )
			{
				std::string flags{};
				const auto append_flag = [ & ]( std::string_view flag, bool enabled )
				{
					if ( !enabled ) return;
					if ( !flags.empty( ) ) flags += ',';
					flags += flag;
				};
				append_flag( "dt", shot.dt );
				append_flag( "forced", shot.forced );
				append_flag( "extrapolated", shot.extrapolated );
				append_flag( "penetrated", shot.penetrated );
				append_flag( "seed", shot.seed_mode );
				append_flag( "target_dead", shot.target_dead_at_resolution );
				if ( flags.empty( ) ) flags = "none";

				const auto fire_inaccuracy = shot.inaccuracy_confirmed
					? std::format( "{:.6f}", shot.server_inaccuracy ) : std::string{ "n/a" };
				const auto committed_at = shot.committed_time > 0.0f ? shot.committed_time : shot.time;
				const auto age_ms = std::max( current_time - committed_at, 0.0f ) * 1000.0f;
				const auto shot_id = shot.command_number >= 0
					? std::format( "S{}-C{}-0", shot.session_epoch, shot.command_number )
					: std::format( "S{}-Q{}", shot.session_epoch, shot.sequence );

				output.console_message = std::format(
					"[velocity plus][ragebot miss] id={} seq={} target=\"{}\" target_pawn=0x{:X} controller_handle=0x{:X} "
					"weapon={} weapon_type={} reason={} label=\"{}\" confidence={} cmd={} record_tick={} stamp_tick={} bt={} "
					"expected_hg={} expected_dmg={:.1f} target_hp={} hc={:.1f}% flags={} pred_inacc={:.6f} fire_inacc={} spread={:.6f} "
					"shoot={} server_shoot={} origin_delta={} aim_angle={} aim={} impact_count={} impact_seq={} impact={} angular={} cone={} "
					"impact_dist={} target_dist={} impact_hb={} ray_hb={} ideal_ray_hb={} target_velocity={} target_speed={:.2f} "
					"evidence=committed:{},fire:{},inaccuracy:{},shootpos:{},impact:{},hurt:{} age_ms={:.0f}",
					shot_id, shot.sequence, console_name, shot.victim_pawn, shot.victim_controller_handle,
					shot.weapon_id, shot.weapon_type, reason_code, reason_label, this->miss_confidence_name( analysis.confidence ),
					shot.command_number, shot.tick, shot.stamp_tick, std::max( shot.stamp_tick - shot.tick, 0 ), group,
					shot.damage, shot.target_health, shot.hitchance * 100.0f, flags, shot.predicted_inaccuracy, fire_inaccuracy,
					shot.predicted_spread, detail::format_vector( shot.shoot_position ),
					detail::format_vector( shot.server_shoot_position, shot.server_shoot_position_confirmed ),
					detail::format_metric( analysis.shoot_origin_delta, "u" ), detail::format_vector( shot.aim_angle ),
					detail::format_vector( shot.aim_position ), shot.impact_count, shot.impact_event_sequence,
					detail::format_vector( shot.impact_position, shot.impact_confirmed ),
					detail::format_metric( analysis.angular_deviation_deg, "deg" ), detail::format_metric( analysis.spread_cone_deg, "deg" ),
					detail::format_metric( analysis.impact_distance, "u" ), detail::format_metric( analysis.target_distance, "u" ),
					detail::format_metric( analysis.impact_to_hitbox, "u" ), detail::format_metric( analysis.impact_ray_to_hitbox, "u" ),
					detail::format_metric( analysis.ideal_ray_to_hitbox, "u" ), detail::format_vector( shot.target_velocity ),
					shot.target_velocity.length_2d( ), static_cast< int >( shot.committed ), static_cast< int >( shot.fire_confirmed ),
					static_cast< int >( shot.inaccuracy_confirmed ), static_cast< int >( shot.server_shoot_position_confirmed ),
					static_cast< int >( shot.impact_confirmed ), static_cast< int >( shot.hurt_confirmed ), age_ms );
			}

			if ( cfg.chat_log.value )
			{
				const auto short_text = analysis.reason == miss_reason::server_rejected
					? std::format( "空枪 {}（{}）", chat_name, reason_label )
					: std::format( "未命中 {} [{}]：{}{}", chat_name, detail::escape_chat_text( group ), reason_label,
						shot.forced ? "（强制）" : "" );
				output.chat_message = detail::chat_miss( short_text );
				output.chat_next_attempt_at = std::chrono::steady_clock::now( );
				output.chat_expires_at = output.chat_next_attempt_at + std::chrono::seconds( 2 );
			}

			this->m_pending_miss_outputs.push_back( std::move( output ) );
		}

		if ( !cfg.miss_log.value )
		{
			return;
		}

		log entry{};
		entry.name = name;
		entry.reason = reason_label;
		entry.hitgroup = shot.forced ? std::format( "({}，强制)", group ) : std::format( "({})", group );

		entry.damage = 0;
		entry.health = -1;
		entry.time = current_time;
		entry.offset.set_stiffness( 180.0f );
		entry.offset.set_damping( 16.0f );
		entry.offset.snap( -150.0f );
		entry.offset.set_target( 0.0f );
		entry.alpha.fade_in( 0.15f );
		entry.snapped = false;
		entry.is_miss = true;
		entry.duration = settings::g_misc.m_impacts.miss_log_duration;
		entry.weapon_type = shot.weapon_type;

		this->m_logs.insert( this->m_logs.begin( ), std::move( entry ) );

		if ( this->m_logs.size( ) > 5 )
		{
			this->m_logs.pop_back( );
		}

	}

	void impacts::flush_miss_outputs( )
	{
		std::vector<pending_miss_output> outputs{};
		{
			std::unique_lock lock( this->m_mtx );
			outputs.swap( this->m_pending_miss_outputs );
		}

		const auto now = std::chrono::steady_clock::now( );

		// Miss output is budgeted by the number of bullets actually in
		// flight: one shot may only ever produce its FASTEST miss log (the
		// earliest finalization), two shots two logs, and so on. Any
		// excess logs from the same volley are dropped instead of queued -
		// this kills the storm while keeping one log per real miss.
		// Only CONFIRMED (committed) bullets that are still unresolved
		// count toward the quota - uncommitted scan intents must not
		// inflate the budget (that is what let the storm through before).
		const auto bullet_quota = static_cast< int >( std::count_if( this->m_pending_shots.begin( ), this->m_pending_shots.end( ),
			[ ]( const shot_record& s ) { return s.ragebot && s.committed && !s.resolved; } ) );

		std::vector<pending_miss_output> retries{};
		auto console_printed{ 0 };
		for ( auto& output : outputs )
		{
			if ( !output.console_message.empty( ) )
			{
				// Earliest-finalized logs come first in the queue, so the
				// first bullet_quota entries are the fastest logs for the
				// shots in flight. Beyond the quota the entry is dropped
				// (unless it still carries a chat payload below).
				if ( console_printed < bullet_quota )
				{
					logging::console::print_severity( 3, xs( "{}" ), output.console_message );
					++console_printed;
				}
				else
				{
					output.console_message.clear( );
				}
			}

			if ( !output.chat_message.empty( ) )
			{
				if ( now < output.chat_next_attempt_at )
				{
					retries.push_back( std::move( output ) );
				}
				else if ( !detail::chat_print_velocity( output.chat_message.c_str( ) ) && now < output.chat_expires_at )
				{
					output.console_message.clear( );
					output.chat_next_attempt_at = now + std::chrono::milliseconds( 100 );
					retries.push_back( std::move( output ) );
				}
			}
		}

		if ( !retries.empty( ) )
		{
			std::unique_lock lock( this->m_mtx );
			for ( auto& retry : retries )
			{
				this->m_pending_miss_outputs.push_back( std::move( retry ) );
			}
		}
	}

	void impacts::check_misses( )
	{
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );
		auto latency{ 0.0f };
		if ( const auto net_channel = memory::call<std::uintptr_t>( PATTERN( patterns::get_net_channel ), 0, 0 ) )
		{
			const auto measured = memory::call_vfunc<float>( net_channel, 10, 0 );
			if ( std::isfinite( measured ) && measured > 0.0f && measured < 1.0f )
			{
				latency = measured;
			}
		}

		const auto hurt_grace_period = std::clamp( 0.35f + latency * 1.5f, 0.35f, 1.5f );
		const auto absolute_timeout = std::clamp( 1.0f + latency * 2.0f, 1.0f, 3.0f );
		constexpr auto uncommitted_timeout{ 0.5f };
		constexpr auto hit_tombstone_time{ 1.0f };
		constexpr auto miss_finalization_delay{ 0.5f };
		const auto& cfg = settings::g_misc.m_impacts;
		const auto wants_output = cfg.miss_log.value || cfg.console_log.value || cfg.chat_log.value;
		const auto erase_shot = [ & ]( auto it )
		{
			if ( it->sequence == this->m_active_fire_sequence )
			{
				this->m_active_fire_sequence = 0;
			}
			return this->m_pending_shots.erase( it );
		};

		for ( auto it = this->m_pending_shots.begin( ); it != this->m_pending_shots.end( ); )
		{
			if ( it->resolved )
			{
				if ( current_time - it->resolved_time <= hit_tombstone_time )
				{
					++it;
					continue;
				}

				it = erase_shot( it );
				continue;
			}

			// Manual/legit sentinels only occupy the causal event timeline.
			// They are never analyzed or emitted as Ragebot misses.
			if ( !it->ragebot )
			{
				const auto evidence_time = it->impact_confirmed ? it->impact_time
					: ( it->fire_confirmed && it->fire_time > 0.0f ? it->fire_time : it->committed_time );
				const auto evidence_timeout = it->impact_confirmed ? hurt_grace_period + miss_finalization_delay : absolute_timeout;
				if ( current_time - evidence_time > evidence_timeout )
				{
					it = erase_shot( it );
					continue;
				}

				++it;
				continue;
			}

			// Do not print a miss at the first timeout edge. Keep the shot
			// matchable for one final bounded window so a reordered/late hurt
			// cannot produce contradictory "miss + hit" output for one shot.
			if ( it->miss_pending )
			{
				if ( current_time - it->resolved_time <= miss_finalization_delay )
				{
					++it;
					continue;
				}

				if ( it->seed_mode && it->impact_confirmed )
				{
					features::combat::g_shared.note_seed_shot( false );
				}

				const auto analysis = this->analyze_shot( *it );
				const auto victim_gone = it->victim_pawn && ( !systems::g_entities.exists( it->victim_pawn )
					|| memory::read<int>( it->victim_pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) ) <= 0 );
				it->target_dead_at_resolution = victim_gone;

				if ( wants_output )
				{
					this->add_miss_log( *it, analysis );
				}

				it = erase_shot( it );
				continue;
			}

			const auto elapsed = current_time - it->time;
			if ( !it->committed && !it->fire_confirmed && !it->impact_confirmed )
			{
				if ( elapsed > uncommitted_timeout )
				{
					it = erase_shot( it );
					continue;
				}

				++it;
				continue;
			}

			const auto committed_at = it->committed_time > 0.0f ? it->committed_time : it->time;
			const auto committed_elapsed = current_time - committed_at;
			const auto ready_to_resolve = it->impact_confirmed
				? current_time - it->impact_time > hurt_grace_period
				: committed_elapsed > absolute_timeout;

			if ( !ready_to_resolve )
			{
				++it;
				continue;
			}

			it->miss_pending = true;
			it->resolved_time = current_time;
			++it;

		}
	}

	void impacts::render_hit_markers( xdraw::draw_list& draw_list, float time )
	{
		const auto& cfg = settings::g_misc.m_impacts;

		std::unique_lock lock( this->m_mtx );

		for ( auto it = this->m_hitmarkers.begin( ); it != this->m_hitmarkers.end( ); )
		{
			const auto duration = cfg.hit_marker_duration;
			const auto elapsed = time - it->time;

			if ( elapsed > duration )
			{
				it = this->m_hitmarkers.erase( it );
				continue;
			}

			const auto screen = systems::g_view.project( it->position );
			const auto progress = elapsed / duration;

			const auto ease_out = 1.0f - ( progress * progress );
			const auto alpha = static_cast< std::uint8_t >( ease_out * 255.0f );
			const auto color = xdraw::color( cfg.hit_marker_color.value.r, cfg.hit_marker_color.value.g, cfg.hit_marker_color.value.b, alpha );

			const auto x = screen.x, y = screen.y;

			const auto show_classic = cfg.hit_marker_type == settings::misc::impacts::marker_type::classic || cfg.hit_marker_type == settings::misc::impacts::marker_type::both;
			const auto show_damage = cfg.hit_marker_type == settings::misc::impacts::marker_type::damage || cfg.hit_marker_type == settings::misc::impacts::marker_type::both;

			auto size{ 0.0f };
			auto gap{ 0.0f };

			if ( show_classic )
			{
				const auto expand_progress = std::min( elapsed * 20.0f, 1.0f );
				const auto ease_expand = 1.0f - std::pow( 1.0f - expand_progress, 4.0f );

				const auto base_size{ 10.0f };
				const auto base_gap{ 3.0f };
				const auto expand_amount = 6.0f * ( 1.0f - ease_expand );

				size = base_size + expand_amount;
				gap = base_gap + expand_amount * 0.3f;
			}

			constexpr auto thickness{ 1.25f };

			auto draw_arm = [ & ]( xdraw::draw_list& target, float x1, float y1, float x2, float y2, xdraw::color col, float thick )
				{
					const auto dx = x2 - x1;
					const auto dy = y2 - y1;
					const auto len = std::sqrt( dx * dx + dy * dy );

					if ( len < 0.001f )
					{
						return;
					}

					const auto col_clear = xdraw::color{ col.r, col.g, col.b, 0 };
					const auto mx = ( x1 + x2 ) * 0.5f;
					const auto my = ( y1 + y2 ) * 0.5f;

					const float pts[ ]{ x1, y1, mx, my, x2, y2 };
					const xdraw::color cols[ ]{ col_clear, col, col };

					target.polyline_gradient( pts, cols, false, thick );
				};

			auto damage_text = std::string{};
			auto draw_x{ 0.0f };
			auto draw_y{ 0.0f };

			if ( show_damage )
			{
				const auto base_offset = show_classic ? 20.0f : 0.0f;

				damage_text = std::to_string( it->damage );
				const auto [text_w, text_h] = xdraw::measure_text( damage_text );

				const auto text_x = x - text_w * 0.5f;
				const auto text_y = y - base_offset - text_h * 0.5f;

				const auto shake_progress = std::max( 0.0f, 1.0f - elapsed * 8.0f );
				const auto shake_x = std::sin( elapsed * 50.0f ) * shake_progress * 3.0f;
				const auto shake_y = std::cos( elapsed * 45.0f ) * shake_progress * 2.0f;

				draw_x = text_x + shake_x;
				draw_y = text_y + shake_y;
			}

			if ( cfg.hit_marker_glow && alpha > 0 )
			{
				auto& glow = xdraw::get_glow( );
				const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( alpha ) * cfg.hit_marker_glow_strength );
				const auto glow_col = xdraw::color{ cfg.hit_marker_color.value.r, cfg.hit_marker_color.value.g, cfg.hit_marker_color.value.b, glow_a };

				if ( show_classic )
				{
					for ( auto t = 0; t < 3; ++t )
					{
						const auto glow_thick = thickness + 1.0f + static_cast< float >( t ) * 2.0f;

						draw_arm( glow, x - size, y - size, x - gap, y - gap, glow_col, glow_thick );
						draw_arm( glow, x + size, y - size, x + gap, y - gap, glow_col, glow_thick );
						draw_arm( glow, x - size, y + size, x - gap, y + gap, glow_col, glow_thick );
						draw_arm( glow, x + size, y + size, x + gap, y + gap, glow_col, glow_thick );
					}
				}

				if ( show_damage )
				{
					glow.text( draw_x, draw_y, damage_text, glow_col );
				}
			}

			if ( show_classic )
			{
				draw_arm( draw_list, x - size, y - size, x - gap, y - gap, color, thickness );
				draw_arm( draw_list, x + size, y - size, x + gap, y - gap, color, thickness );
				draw_arm( draw_list, x - size, y + size, x - gap, y + gap, color, thickness );
				draw_arm( draw_list, x + size, y + size, x + gap, y + gap, color, thickness );
			}

			if ( show_damage )
			{
				draw_list.text( draw_x, draw_y, damage_text, color );
			}

			++it;
		}
	}

	void impacts::render_logs( xdraw::draw_list& draw_list, float time )
	{
		std::unique_lock lock( this->m_mtx );

		const auto& s = xui::ctx( ).style;

		constexpr auto fade_ratio{ 0.8f };
		constexpr auto entry_spacing{ 3.0f };
		constexpr auto base_x{ 15.0f };
		constexpr auto base_y{ 15.0f };

		constexpr auto h{ 24.0f };
		constexpr auto r{ 8.0f };
		constexpr auto inner_r{ 6.0f };
		constexpr auto inner_pad{ 2.0f };
		constexpr auto text_pad_x{ 8.0f };
		constexpr auto text_nudge{ 0.5f };
		constexpr auto icon_size{ 20.0f };
		constexpr auto icon_inner_pad{ 4.0f };

		static const auto miss_accent = xdraw::color{ 255, 100, 100, 255 };
		static const auto miss_dim = xdraw::color{ 255, 100, 100, 82 };

		static auto hit_icon_w = 0, hit_icon_h = 0;
		static const auto hit_icon = xdraw::load_svg( R"(<svg width="12" height="12" viewBox="0 0 12 12" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M1.5 6C1.5 6.59095 1.6164 7.17611 1.84254 7.72208C2.06869 8.26804 2.40016 8.76412 2.81802 9.18198C3.23588 9.59984 3.73196 9.93131 4.27792 10.1575C4.82389 10.3836 5.40905 10.5 6 10.5C6.59095 10.5 7.17611 10.3836 7.72208 10.1575C8.26804 9.93131 8.76412 9.59984 9.18198 9.18198C9.59984 8.76412 9.93131 8.26804 10.1575 7.72208C10.3836 7.17611 10.5 6.59095 10.5 6C10.5 4.80653 10.0259 3.66193 9.18198 2.81802C8.33807 1.97411 7.19347 1.5 6 1.5C4.80653 1.5 3.66193 1.97411 2.81802 2.81802C1.97411 3.66193 1.5 4.80653 1.5 6Z" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/><path d="M4.5 7H7.5C7.5 7.39782 7.34196 7.77936 7.06066 8.06066C6.77936 8.34196 6.39782 8.5 6 8.5C5.60218 8.5 5.22064 8.34196 4.93934 8.06066C4.65804 7.77936 4.5 7.39782 4.5 7Z" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/><path d="M4.5 4L7.5 5.5" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/><path d="M4.5 5.5L7.5 4" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/></svg>)", 1.0f, &hit_icon_w, &hit_icon_h );

		static auto miss_icon_w = 0, miss_icon_h = 0;
		static const auto miss_icon = xdraw::load_svg( R"(<svg width="12" height="12" viewBox="0 0 12 12" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M1.5 6C1.5 6.59095 1.6164 7.17611 1.84254 7.72208C2.06869 8.26804 2.40016 8.76412 2.81802 9.18198C3.23588 9.59984 3.73196 9.93131 4.27792 10.1575C4.82389 10.3836 5.40905 10.5 6 10.5C6.59095 10.5 7.17611 10.3836 7.72208 10.1575C8.26804 9.93131 8.76412 9.59984 9.18198 9.18198C9.59984 8.76412 9.93131 8.26804 10.1575 7.72208C10.3836 7.17611 10.5 6.59095 10.5 6C10.5 5.40905 10.3836 4.82389 10.1575 4.27792C9.93131 3.73196 9.59984 3.23588 9.18198 2.81802C8.76412 2.40016 8.26804 2.06869 7.72208 1.84254C7.17611 1.6164 6.59095 1.5 6 1.5C5.40905 1.5 4.82389 1.6164 4.27792 1.84254C3.73196 2.06869 3.23588 2.40016 2.81802 2.81802C2.40016 3.23588 2.06869 3.73196 1.84254 4.27792C1.6164 4.82389 1.5 5.40905 1.5 6Z" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/><path d="M7.25 8.02525C7.08706 7.85896 6.89258 7.72684 6.67794 7.63665C6.4633 7.54646 6.23282 7.5 6 7.5C5.76718 7.5 5.5367 7.54646 5.32206 7.63665C5.10742 7.72684 4.91294 7.85896 4.75 8.02525" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/><path d="M5 4.625C4.75 5.125 3.75 5.125 3.5 4.625" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/><path d="M8.5 4.625C8.25 5.125 7.25 5.125 7 4.625" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/></svg>)", 1.0f, &miss_icon_w, &miss_icon_h );

		const auto inner_h = h - inner_pad * 2.0f;
		auto y_offset{ 0.0f };

		for ( auto it = this->m_logs.begin( ); it != this->m_logs.end( ); )
		{
			const auto elapsed = time - it->time;
			const auto duration = it->duration;
			const auto fade_start = duration * fade_ratio;

			if ( elapsed > duration && it->alpha.finished( ) )
			{
				it = this->m_logs.erase( it );
				continue;
			}

			if ( elapsed > fade_start && it->alpha.alpha( ) > 0.5f )
			{
				it->alpha.fade_out( 0.5f );
			}

			it->offset.update( );
			it->alpha.update( );

			if ( !it->snapped && it->offset.settled( ) )
			{
				it->offset.snap( 0.0f );
				it->snapped = true;
			}

			const auto alpha = it->alpha.alpha( );
			const auto slide_x = it->snapped ? 0.0f : it->offset.value( );

			if ( alpha > 0.01f )
			{
				const auto scale_alpha = [ & ]( xdraw::color c ) -> xdraw::color { return c.alpha( static_cast< std::uint8_t >( ( c.a / 255.0f ) * alpha * 255.0f ) ); };
				const auto& icon_color = it->is_miss ? miss_accent : s.accent;
				const auto& accent_base = it->is_miss ? miss_accent : s.accent;
				const auto& dim_base = it->is_miss ? miss_dim : s.text_dim;
				const auto accent_col = scale_alpha( accent_base );
				const auto dim_col = scale_alpha( dim_base );

				struct text_span
				{
					std::string text{};
					bool accent{};
					float w{};
					float h{};
				};

				std::vector<text_span> spans{};

				if ( it->is_miss )
				{
					if ( it->weapon_type == cstypes::weapon_type::knife || it->weapon_type == cstypes::weapon_type::taser )
					{
						const auto weapon_name = it->weapon_type == cstypes::weapon_type::knife ? "knife" : "zeus";
						const auto [a_w, a_h] = xdraw::measure_text( "missed " );
						const auto [b_w, b_h] = xdraw::measure_text( weapon_name );
						const auto [c_w, c_h] = xdraw::measure_text( " on " );
						const auto [d_w, d_h] = xdraw::measure_text( it->name );
						const auto [e_w, e_h] = xdraw::measure_text( " due to " );
						const auto [f_w, f_h] = xdraw::measure_text( "latency" );

						spans.push_back( { "missed ", false, a_w, a_h } );
						spans.push_back( { weapon_name, true, b_w, b_h } );
						spans.push_back( { " on ", false, c_w, c_h } );
						spans.push_back( { it->name, true, d_w, d_h } );
						spans.push_back( { " due to ", false, e_w, e_h } );
						spans.push_back( { "latency", true, f_w, f_h } );
					}
					else
					{
						const auto [a_w, a_h] = xdraw::measure_text( "missed " );
						const auto [b_w, b_h] = xdraw::measure_text( "shot" );
						const auto [c_w, c_h] = xdraw::measure_text( " due to " );
						const auto [d_w, d_h] = xdraw::measure_text( it->reason );

						spans.push_back( { "missed ", false, a_w, a_h } );
						spans.push_back( { "shot", true, b_w, b_h } );
						spans.push_back( { " due to ", false, c_w, c_h } );
						spans.push_back( { it->reason, true, d_w, d_h } );

						if ( !it->hitgroup.empty( ) )
						{
							const auto [e_w, e_h] = xdraw::measure_text( " " );
							const auto [f_w, f_h] = xdraw::measure_text( it->hitgroup );

							spans.push_back( { " ", false, e_w, e_h } );
							spans.push_back( { it->hitgroup, false, f_w, f_h } );
						}
					}
				}
				else
				{
					if ( it->weapon_type == cstypes::weapon_type::knife )
					{
						const auto damage_text = std::to_string( it->damage );

						const auto [a_w, a_h] = xdraw::measure_text( "knifed " );
						const auto [b_w, b_h] = xdraw::measure_text( it->name );
						const auto [c_w, c_h] = xdraw::measure_text( " for " );
						const auto [d_w, d_h] = xdraw::measure_text( damage_text );

						spans.push_back( { "knifed ", false, a_w, a_h } );
						spans.push_back( { it->name, true, b_w, b_h } );
						spans.push_back( { " for ", false, c_w, c_h } );
						spans.push_back( { damage_text, true, d_w, d_h } );

						const auto remaining_text = std::format( " ({} remaining)", it->health );
						const auto [e_w, e_h] = xdraw::measure_text( remaining_text );
						spans.push_back( { remaining_text, false, e_w, e_h } );
					}
					else if ( it->weapon_type == cstypes::weapon_type::taser )
					{
						const auto [a_w, a_h] = xdraw::measure_text( "zapped the fuck out of " );
						const auto [b_w, b_h] = xdraw::measure_text( it->name );

						spans.push_back( { "zapped the fuck out of ", false, a_w, a_h } );
						spans.push_back( { it->name, true, b_w, b_h } );
					}
					else
					{
						const auto damage_text = std::to_string( it->damage );

						const auto [a_w, a_h] = xdraw::measure_text( "hit " );
						const auto [b_w, b_h] = xdraw::measure_text( it->name );
						const auto [c_w, c_h] = xdraw::measure_text( " for " );
						const auto [d_w, d_h] = xdraw::measure_text( damage_text );
						const auto [e_w, e_h] = xdraw::measure_text( " in " );
						const auto [f_w, f_h] = xdraw::measure_text( it->hitgroup );

						spans.push_back( { "hit ", false, a_w, a_h } );
						spans.push_back( { it->name, true, b_w, b_h } );
						spans.push_back( { " for ", false, c_w, c_h } );
						spans.push_back( { damage_text, true, d_w, d_h } );
						spans.push_back( { " in ", false, e_w, e_h } );
						spans.push_back( { it->hitgroup, true, f_w, f_h } );

						if ( !it->reason.empty( ) )
						{
							const auto [g_w, g_h] = xdraw::measure_text( ", " );
							const auto [h_w, h_h] = xdraw::measure_text( it->reason );

							spans.push_back( { ", ", false, g_w, g_h } );
							spans.push_back( { it->reason, false, h_w, h_h } );
						}
					}
				}

				auto text_total_w{ 0.0f };
				auto text_h{ 0.0f };

				for ( const auto& span : spans )
				{
					text_total_w += span.w;
					text_h = std::max( text_h, span.h );
				}

				const auto text_pill_w = text_total_w + text_pad_x * 2.0f;
				const auto total_w = inner_pad + icon_size + inner_pad + text_pill_w + inner_pad;

				const auto x = base_x + slide_x;
				const auto y = base_y + y_offset;

				if ( elapsed <= fade_start )
				{
					draw_list.rect_filled_blurred( x, y, total_w, h, xdraw::corner_radius{ r } );
				}

				draw_list.rect_filled( x, y, total_w, h, scale_alpha( s.window_bg ), xdraw::corner_radius{ r } );
				draw_list.rect_filled( x + inner_pad, y + inner_pad, icon_size, inner_h, scale_alpha( icon_color ), xdraw::corner_radius{ inner_r } );

				const auto& icon = it->is_miss ? miss_icon : hit_icon;
				if ( icon )
				{
					const auto icon_draw = icon_size - icon_inner_pad * 2.0f;
					const auto ix = std::floor( x + inner_pad + icon_inner_pad );
					const auto iy = std::floor( y + inner_pad + ( inner_h - icon_draw ) * 0.5f );
					draw_list.image( ix, iy, icon_draw, icon_draw, icon.Get( ), scale_alpha( s.checkbox_mark_icon ) );
				}

				const auto tp_x = x + inner_pad + icon_size + inner_pad;
				draw_list.rect_filled( tp_x, y + inner_pad, text_pill_w, inner_h, scale_alpha( s.child_bg ), xdraw::corner_radius{ inner_r } );

				auto tx = tp_x + text_pad_x;
				const auto ty = y + ( h - text_h ) * 0.5f + text_nudge;

				for ( const auto& span : spans )
				{
					draw_list.text( tx, ty, span.text, span.accent ? accent_col : dim_col );
					tx += span.w;
				}

				y_offset += h + entry_spacing;
			}

			++it;
		}
	}

	void impacts::render_hit_effect( xdraw::draw_list& draw_list, float time )
	{
		const auto& cfg = settings::g_misc.m_impacts;

		if ( !cfg.hit_effect.value || this->m_hit_effect_time <= 0.0f )
		{
			return;
		}

		const auto elapsed = time - this->m_hit_effect_time;
		const auto duration = cfg.hit_effect_duration;

		if ( elapsed > duration )
		{
			this->m_hit_effect_time = 0.0f;
			return;
		}

		const auto progress = elapsed / duration;
		const auto fade_in = std::min( elapsed * 15.0f, 1.0f );
		const auto fade_in_smooth = 1.0f - std::pow( 1.0f - fade_in, 3.0f );
		const auto fade_out = 1.0f - std::pow( progress, 0.6f );
		const auto intensity = fade_in_smooth * fade_out * ( cfg.hit_effect_strength / 100.0f );

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto sw = static_cast< float >( screen_w );
		const auto sh = static_cast< float >( screen_h );

		const auto r = cfg.hit_effect_color.value.r;
		const auto g = cfg.hit_effect_color.value.g;
		const auto b = cfg.hit_effect_color.value.b;

		constexpr auto band_count{ 32 };

		for ( auto i = 0; i < band_count; ++i )
		{
			const auto t = static_cast< float >( i ) / static_cast< float >( band_count - 1 );
			const auto size = 0.01f + t * 0.35f;
			const auto falloff = std::pow( 1.0f - t, 2.5f );
			const auto a = static_cast< std::uint8_t >( std::min( intensity * falloff * 280.0f, 255.0f ) );

			if ( a == 0 )
			{
				continue;
			}

			const auto edge = xdraw::color{ r, g, b, a };
			const auto clear = xdraw::color{ r, g, b, 0 };

			const auto tx = sw * size;
			const auto ty = sh * size;

			draw_list.rect_filled_gradient( 0.0f, 0.0f, tx, sh, edge, clear, clear, edge );
			draw_list.rect_filled_gradient( sw - tx, 0.0f, tx, sh, clear, edge, edge, clear );
			draw_list.rect_filled_gradient( 0.0f, 0.0f, sw, ty, edge, edge, clear, clear );
			draw_list.rect_filled_gradient( 0.0f, sh - ty, sw, ty, clear, clear, edge, edge );
		}
	}

	void impacts::render_bullet_impact_overlays( xdraw::draw_list& draw_list, float time )
	{
		const auto& cfg = settings::g_misc.m_impacts;

		if ( !cfg.bullet_impact_effect.value )
		{
			return;
		}

		const auto type = cfg.bullet_impact_effect_type.value;
		if ( type == settings::misc::impacts::bullet_impact_type::sparks )
		{
			return;
		}

		std::unique_lock lock( this->m_mtx );

		const auto duration = cfg.bullet_impact_effect_duration.value;
		constexpr auto half_size{ 1.75f };

		for ( auto it = this->m_bullet_impacts.begin( ); it != this->m_bullet_impacts.end( ); )
		{
			const auto elapsed = time - it->time;

			if ( elapsed > duration )
			{
				it = this->m_bullet_impacts.erase( it );
				continue;
			}

			const auto progress = elapsed / duration;
			const auto fade = 1.0f - ( progress * progress );
			const auto alpha = static_cast< std::uint8_t >( fade * 255.0f );

			if ( alpha == 0 )
			{
				++it;
				continue;
			}

			const math::vector3 corners[ 8 ]
			{
				it->position + math::vector3{ -half_size, -half_size, -half_size },
				it->position + math::vector3{  half_size, -half_size, -half_size },
				it->position + math::vector3{  half_size,  half_size, -half_size },
				it->position + math::vector3{ -half_size,  half_size, -half_size },
				it->position + math::vector3{ -half_size, -half_size,  half_size },
				it->position + math::vector3{  half_size, -half_size,  half_size },
				it->position + math::vector3{  half_size,  half_size,  half_size },
				it->position + math::vector3{ -half_size,  half_size,  half_size },
			};

			float sx[ 8 ]{}, sy[ 8 ]{};
			auto all_valid{ true };

			for ( auto i = 0; i < 8; ++i )
			{
				const auto proj = systems::g_view.project( corners[ i ] );
				if ( !systems::g_view.projection_valid( proj ) )
				{
					all_valid = false;
					break;
				}

				sx[ i ] = proj.x;
				sy[ i ] = proj.y;
			}

			if ( !all_valid )
			{
				++it;
				continue;
			}

			const auto scale_alpha = [ alpha ]( xdraw::color c ) -> xdraw::color { return xdraw::color{ c.r, c.g, c.b, static_cast< std::uint8_t >( ( c.a / 255.0f ) * ( alpha / 255.0f ) * 255.0f ) }; };
			const auto fill_col = scale_alpha( cfg.bullet_impact_effect_fill_color.value );
			const auto edge_col = scale_alpha( cfg.bullet_impact_effect_edge_color.value );

			constexpr int faces[ 6 ][ 4 ]
			{
				{ 0, 3, 2, 1 },
				{ 4, 5, 6, 7 },
				{ 0, 1, 5, 4 },
				{ 2, 3, 7, 6 },
				{ 0, 4, 7, 3 },
				{ 1, 2, 6, 5 },
			};

			for ( const auto& f : faces )
			{
				float poly[ 8 ]
				{
					sx[ f[ 0 ] ], sy[ f[ 0 ] ],
					sx[ f[ 1 ] ], sy[ f[ 1 ] ],
					sx[ f[ 2 ] ], sy[ f[ 2 ] ],
					sx[ f[ 3 ] ], sy[ f[ 3 ] ],
				};

				draw_list.convex_filled( { poly, 8 }, fill_col );
			}

			constexpr std::pair<int, int> edges[ 12 ]
			{
				{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
				{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
				{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
			};

			if ( cfg.bullet_impact_effect_glow )
			{
				auto& glow = xdraw::get_glow( );

				const auto edge_glow_a = static_cast< std::uint8_t >( static_cast< float >( edge_col.a ) * cfg.bullet_impact_effect_glow_strength );
				const auto edge_glow_col = xdraw::color{ edge_col.r, edge_col.g, edge_col.b, edge_glow_a };

				const auto fill_glow_a = static_cast< std::uint8_t >( static_cast< float >( fill_col.a ) * cfg.bullet_impact_effect_glow_strength );
				const auto fill_glow_col = xdraw::color{ fill_col.r, fill_col.g, fill_col.b, fill_glow_a };

				for ( const auto& f : faces )
				{
					float poly[ 8 ]
					{
						sx[ f[ 0 ] ], sy[ f[ 0 ] ],
						sx[ f[ 1 ] ], sy[ f[ 1 ] ],
						sx[ f[ 2 ] ], sy[ f[ 2 ] ],
						sx[ f[ 3 ] ], sy[ f[ 3 ] ],
					};

					glow.convex_filled( { poly, 8 }, fill_glow_col );
				}

				for ( const auto& [a, b] : edges )
				{
					const float line[ 4 ]{ sx[ a ], sy[ a ], sx[ b ], sy[ b ] };
					glow.polyline( { line, 4 }, edge_glow_col, false, 1.0f );
				}
			}

			for ( const auto& [a, b] : edges )
			{
				const float line[ 4 ]{ sx[ a ], sy[ a ], sx[ b ], sy[ b ] };
				draw_list.polyline( { line, 4 }, edge_col, false, 1.0f );
			}

			++it;
		}
	}

	namespace custom_sound_detail {

		[[nodiscard]] std::wstring sounds_directory( )
		{
			wchar_t app_data[ MAX_PATH ]{};
			if ( FAILED( SHGetFolderPathW( nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, app_data ) ) )
			{
				return {};
			}

			const auto root = std::wstring( app_data ) + L"\\velocity";
			const auto sounds = root + L"\\sounds";

			CreateDirectoryW( root.c_str( ), nullptr );
			CreateDirectoryW( sounds.c_str( ), nullptr );

			return sounds;
		}

		[[nodiscard]] std::string sanitize_filename( std::string_view name )
		{
			std::string out{};
			out.reserve( name.size( ) );

			for ( const auto c : name )
			{
				if ( std::isalnum( static_cast< unsigned char >( c ) ) || c == '_' || c == '-' || c == '.' )
				{
					out.push_back( static_cast< char >( c ) );
				}
			}

			return out;
		}

		[[nodiscard]] bool has_extension( std::string_view name, std::string_view ext )
		{
			if ( name.size( ) < ext.size( ) )
			{
				return false;
			}

			return _strnicmp( name.data( ) + name.size( ) - ext.size( ), ext.data( ), ext.size( ) ) == 0;
		}

void play_engine_path( const char* sound_path, float volume )
		{
			struct
			{
				char padding[ 1080 ];
				int argc;
				const char** argv;
			} args{};

			char volume_str[ 16 ];
			std::snprintf( volume_str, sizeof( volume_str ), "%.2f", volume / 100.0f );

			const char* sound_args[ ]{ "playvol", sound_path, volume_str };
			args.argc = 3;
			args.argv = sound_args;

			memory::call<void>( PATTERN (patterns::play_sound), 0.0f, &args );
		}

		void play_wav_direct( const std::wstring& path, float volume )
		{
			using PlaySoundW_t = BOOL( WINAPI* )( LPCWSTR, HMODULE, DWORD );
			using waveOutSetVolume_t = UINT( WINAPI* )( UINT_PTR, DWORD );

			static const auto winmm = []() -> HMODULE {
				HMODULE mod = GetModuleHandleW( L"winmm.dll" );
				return mod ? mod : LoadLibraryW( L"winmm.dll" );
			}();

			if ( !winmm )
			{
				return;
			}

			static const auto play_fn = reinterpret_cast<PlaySoundW_t>( GetProcAddress( winmm, "PlaySoundW" ) );
			if ( !play_fn )
			{
				return;
			}

			static const auto set_vol_fn = reinterpret_cast<waveOutSetVolume_t>( GetProcAddress( winmm, "waveOutSetVolume" ) );
			if ( set_vol_fn )
			{
				const auto level = static_cast<WORD>( std::clamp( volume / 100.0f, 0.0f, 1.0f ) * 0xFFFFu );
				const DWORD vol = static_cast<DWORD>( level ) | ( static_cast<DWORD>( level ) << 16 );
				set_vol_fn( static_cast<UINT_PTR>( static_cast<UINT>( -1 ) ), vol ); // WAVE_MAPPER
			}

			// SND_FILENAME(0x20000) | SND_ASYNC(0x1) | SND_NODEFAULT(0x2)
			play_fn( path.c_str( ), nullptr, 0x00020003u );
		}

		[[nodiscard]] std::wstring resolve_sound_path( std::string_view filename )
		{
			const auto sanitized = sanitize_filename( filename );
			if ( sanitized.empty( ) )
			{
				return {};
			}

			const auto directory = sounds_directory( );
			if ( directory.empty( ) )
			{
				return {};
			}

			const auto wide_name = std::wstring( sanitized.begin( ), sanitized.end( ) );
			const auto full_path = directory + L"\\" + wide_name;

			if ( !std::filesystem::exists( full_path ) )
			{
				return {};
			}

			return full_path;
		}


} // namespace custom_sound_detail

	std::string impacts::custom_sounds_directory_narrow( )
	{
		const auto directory = custom_sound_detail::sounds_directory( );
		if ( directory.empty( ) )
		{
			return {};
		}

		return std::string( directory.begin( ), directory.end( ) );
	}

	std::vector<std::string> impacts::list_custom_sounds( )
	{
		std::vector<std::string> files{};

		const auto directory = custom_sound_detail::sounds_directory( );
		if ( directory.empty( ) )
		{
			return files;
		}

		std::error_code ec{};
		for ( const auto& entry : std::filesystem::directory_iterator( directory, ec ) )
		{
			if ( ec || !entry.is_regular_file( ) )
			{
				continue;
			}

			const auto filename = entry.path( ).filename( ).string( );
			if ( filename.empty( ) )
			{
				continue;
			}

			if ( !custom_sound_detail::has_extension( filename, ".wav" ) )
			{
				continue;
			}

			files.push_back( filename );
		}

		std::sort( files.begin( ), files.end( ) );
		return files;
	}

	void impacts::play_custom_sound( std::string_view filename, float volume ) const
	{
		const auto path = custom_sound_detail::resolve_sound_path( filename );
		if ( !path.empty( ) )
		{
			custom_sound_detail::play_wav_direct( path, volume );
		}
	}

	void impacts::play_sound( settings::misc::impacts::sound_type type, float volume, std::string_view custom_file )
	{
		if ( type == settings::misc::impacts::sound_type::custom )
		{
			this->play_custom_sound( custom_file, volume );
			return;
		}

		const char* sound_path{ nullptr };

		switch ( type )
		{
		case settings::misc::impacts::sound_type::shop_click:
			sound_path = "sounds/ui/panorama/mainmenu_press_shop_01";
			break;
		case settings::misc::impacts::sound_type::home_click:
			sound_path = "sounds/ui/panorama/mainmenu_press_home_01";
			break;
		case settings::misc::impacts::sound_type::bell:
			sound_path = "sounds/training/timer_bell";
			break;
		case settings::misc::impacts::sound_type::killcard:
			sound_path = "sounds/ui/killcard_1";
			break;
		case settings::misc::impacts::sound_type::bullet_casing:
			sound_path = "sounds/weapons/fx/tink/bullet_casing_07";
			break;
		case settings::misc::impacts::sound_type::coin_pickup:
			sound_path = "sounds/ui/coin_pickup_01";
			break;
		case settings::misc::impacts::sound_type::item_drop:
			sound_path = "sounds/ui/item_drop";
			break;
		case settings::misc::impacts::sound_type::popcan:
			sound_path = "sounds/physics/metal/metal_popcan_impact_hard3";
			break;
		case settings::misc::impacts::sound_type::key_press:
			sound_path = "sounds/weapons/c4/key_press7";
			break;
		default:
			return;
		}

		custom_sound_detail::play_engine_path( sound_path, volume );
	}

	void impacts::play_hit_effect( std::uintptr_t victim_pawn )
	{
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );

		this->m_hit_effect_time = current_time;
	}

	void impacts::play_death_effect( std::uintptr_t victim_pawn )
	{
		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager )
		{
			return;
		}

		constexpr auto particle_path{ "particles/embedded/fade.vpcf" };

		if ( !this->m_death_effect_loaded )
		{
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

			memory::call<void>(PATTERN (patterns::init_particle_path_buffer), &buffer, particle_path );

			buffer.m_unknown4 = 'fcpv';

			memory::call<void>(PATTERN (patterns::resource_system_precache), addresses::globals::resource_system, &buffer, "" );

			this->m_death_effect_loaded = true;
		}

		auto effect_index{ detail::invalid_particle_effect };
		memory::call<int*>(PATTERN (patterns::particle_create_effect), particle_manager, &effect_index, particle_path, 8, 0ll, 0ll, 0ll, 0 );

		if ( effect_index == detail::invalid_particle_effect )
		{
			return;
		}

		const math::vector3 color{ static_cast< float >( settings::g_misc.m_impacts.death_effect_color.value.r ), static_cast< float >( settings::g_misc.m_impacts.death_effect_color.value.g ), static_cast< float >( settings::g_misc.m_impacts.death_effect_color.value.b ) };
		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 2, &color, 0 );

		struct
		{
			std::intptr_t xy{ 0x7f7fffff7f7fffffll };
			int z{ 0x7f7fffff };
		} default_pos;

		memory::call<bool>(PATTERN (patterns::particle_set_entity_binding), particle_manager, effect_index, 0, victim_pawn, 1, nullptr, &default_pos, 1, 0ll );
		memory::call<bool>(PATTERN (patterns::particle_set_entity_binding), particle_manager, effect_index, 1, victim_pawn, 1, nullptr, &default_pos, 1, 0ll );
	}

	void impacts::play_bullet_impact_effect( const math::vector3& position )
	{
		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager )
		{
			return;
		}

		constexpr auto particle_path{ "particles/embedded/sparks.vpcf" };

		if ( !this->m_bullet_impact_effect_loaded )
		{
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

			memory::call<void>(PATTERN (patterns::init_particle_path_buffer), &buffer, particle_path );

			buffer.m_unknown4 = 'fcpv';

			memory::call<void>(PATTERN (patterns::resource_system_precache), addresses::globals::resource_system, &buffer, "" );

			this->m_bullet_impact_effect_loaded = true;
		}

		auto effect_index{ detail::invalid_particle_effect };
		memory::call<int*>(PATTERN (patterns::particle_create_effect), particle_manager, &effect_index, particle_path, 8, 0ll, 0ll, 0ll, 0 );

		if ( effect_index == detail::invalid_particle_effect )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_impacts;

		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 0, &position, 0 );

		const math::vector3 color{ static_cast< float >( cfg.bullet_impact_effect_color_spark.value.r ), static_cast< float >( cfg.bullet_impact_effect_color_spark.value.g ), static_cast< float >( cfg.bullet_impact_effect_color_spark.value.b ) };
		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 1, &color, 0 );
	}

	void impacts::play_bullet_tracer( const math::vector3& position )
	{
		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager || !systems::g_local.get( ).is_alive )
		{
			return;
		}

		constexpr auto particle_path{ "particles/embedded/tracer.vpcf" };

		if ( !this->m_bullet_tracers_loaded )
		{
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

			memory::call<void>(PATTERN (patterns::init_particle_path_buffer), &buffer, particle_path );

			buffer.m_unknown4 = 'fcpv';

			memory::call<void>(PATTERN (patterns::resource_system_precache), addresses::globals::resource_system, &buffer, "" );

			this->m_bullet_tracers_loaded = true;
		}

		auto effect_index{ detail::invalid_particle_effect };
		memory::call<int*>(PATTERN (patterns::particle_create_effect), particle_manager, &effect_index, particle_path, 8, 0ll, 0ll, 0ll, 0 );

		if ( effect_index == detail::invalid_particle_effect )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_impacts;

		memory::call<bool>( PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 0, &this->m_buffered_eye_position, 0 );
		memory::call<bool>( PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 1, &position, 0 );

		const math::vector3 color{ static_cast< float >( cfg.bullet_tracer_color.value.r ), static_cast< float >( cfg.bullet_tracer_color.value.g ), static_cast< float >( cfg.bullet_tracer_color.value.b ) };
		memory::call<bool>( PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 2, &color, 0 );

		const math::vector3 lifetime{ cfg.bullet_tracer_duration, 0.0f, 0.0f };
		memory::call<bool>( PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 3, &lifetime, 0 );
	}

	void impacts::flush_buffered_impacts( )
	{
		if ( this->m_buffered_impacts.empty( ) )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_impacts;
		const auto& final_pos = this->m_buffered_impacts.back( );

		const auto ray = final_pos - this->m_buffered_eye_position;
		const auto ray_len = ray.length( );

		if ( ray_len < 0.1f )
		{
			return;
		}

		const auto ray_dir = ray * ( 1.0f / ray_len );

		for ( const auto& impact : this->m_buffered_impacts )
		{
			const auto to_impact = impact - this->m_buffered_eye_position;
			const auto proj = to_impact.dot( ray_dir );
			const auto corrected = this->m_buffered_eye_position + ray_dir * std::max( proj, 0.0f );

			const auto type = cfg.bullet_impact_effect_type.value;
			const auto show_overlay = type == settings::misc::impacts::bullet_impact_type::overlay || type == settings::misc::impacts::bullet_impact_type::both;

			if ( cfg.bullet_impact_effect.value && show_overlay )
			{
				this->m_bullet_impacts.push_back( { corrected,  this->m_buffered_impact_time } );

				if ( this->m_bullet_impacts.size( ) > 64 )
				{
					this->m_bullet_impacts.erase( this->m_bullet_impacts.begin( ) );
				}
			}
		}

		if ( cfg.bullet_tracers.value )
		{
			this->play_bullet_tracer( final_pos );
		}
	}

} // namespace features::misc
