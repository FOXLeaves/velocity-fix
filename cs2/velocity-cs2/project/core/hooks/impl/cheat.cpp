#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/hooking/hooking.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/security/security.hpp>
#include <core/rendering/rendering.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
#include "../hooks.hpp"

namespace hooks {

	bool cheat::initialize () {
		if (!hooking::manager::create ({
			{ &m_present, &present, xs ("present"), addresses::functions::present },
			{ &m_resize_buffers, &resize_buffers, xs ("resize_buffers"), addresses::functions::resize_buffers },
			{ &m_cmd_interpreter, &cmd_interpreter, xs ("cmd_interpreter"), PATTERN (patterns::cmd_interpreter) },
			{ &m_frame_stage_notify, &frame_stage_notify, xs ("frame_stage_notify"), PATTERN (patterns::frame_stage_notify) },
			{ &m_create_move, &create_move, xs ("create_move"), PATTERN (patterns::create_move) },
			{ &m_handle_view_angles, &handle_view_angles, xs ("handle_view_angles"), PATTERN (patterns::handle_view_angles) },
			{ &m_add_entity, &add_entity, xs ("add_entity"), PATTERN (patterns::add_entity) },
			{ &m_remove_entity, &remove_entity, xs ("remove_entity"), PATTERN (patterns::remove_entity) },
			{ &m_render_view, &render_view, xs ("render_view"), PATTERN (patterns::render_view) },
			{ &m_draw_skybox_array, &draw_skybox_array, xs ("draw_skybox_array"), PATTERN (patterns::draw_skybox_array) },
			{ &m_light_scene_object, &light_scene_object, xs ("light_scene_object"), PATTERN (patterns::light_scene_object) },
			{ &m_draw_scene_object_array, &draw_scene_object_array, xs ("draw_scene_object_array"), PATTERN (patterns::draw_scene_object_array) },
			{ &m_draw_scene_object, &draw_scene_object, xs ("draw_scene_object"), PATTERN (patterns::draw_scene_object) },
			{ &m_is_glowing, &is_glowing, xs ("is_glowing"), PATTERN (patterns::is_glowing) },
			{ &m_get_glow_color, &get_glow_color, xs ("get_glow_color"), PATTERN (patterns::get_glow_color) },
			{ &m_generate_primitives, &generate_primitives, xs ("generate_primitives"), PATTERN (patterns::generate_primitives) },
			{ &m_parse_report_hit, &parse_report_hit, xs ("parse_report_hit"), PATTERN (patterns::parse_report_hit) },
			{ &m_get_scene_param, &get_scene_param, xs ("get_scene_param"), PATTERN (patterns::get_scene_param) },
			{ &m_set_shader_param, &set_shader_param, xs ("set_shader_param"), PATTERN (patterns::set_shader_param) },
			{ &m_override_view, &override_view, xs ("override_view"), PATTERN (patterns::override_view) },
			{ &m_update_fov_sensitivity, &update_fov_sensitivity, xs ("update_fov_sensitivity"), PATTERN (patterns::update_fov_sensitivity) },
			{ &m_render_scope, &render_scope, xs ("render_scope"), PATTERN (patterns::render_scope) },
			{ &m_render_crosshair, &render_crosshair, xs ("render_crosshair"), PATTERN (patterns::render_crosshair) },
			{ &m_prepare_scene_material, &prepare_scene_material, xs ("prepare_scene_material"), PATTERN (patterns::prepare_scene_material) },
			{ &m_post_network_data_received, &post_network_data_received, xs ("post_network_data_received"), PATTERN (patterns::post_network_data_received) },
			{ &m_draw_overhead, &draw_overhead, xs ("draw_overhead"), PATTERN (patterns::draw_overhead) },
			{ &m_draw_legs, &draw_legs, xs ("draw_legs"), PATTERN (patterns::draw_legs) },
			{ &m_is_enemy_on_radar, &is_enemy_on_radar, xs ("is_enemy_on_radar"), PATTERN (patterns::is_enemy_on_radar) },
			{ &m_get_transforms_for_hitbox_list, &get_transforms_for_hitbox_list, xs ("get_transforms_for_hitbox_list"), PATTERN (patterns::get_transforms_for_hitbox_list) },
			{ &m_sort_primitives, &sort_primitives, xs ("sort_primitives"), PATTERN (patterns::sort_primitives) },
			{ &m_get_inaccuracy, &get_inaccuracy, xs ("get_inaccuracy"), PATTERN (patterns::get_inaccuracy) },
			{ &m_get_interpolated_shoot_position, &get_interpolated_shoot_position, xs ("get_interpolated_shoot_position"), PATTERN (patterns::get_interpolated_shoot_position) },
			{ &m_level_initialization, &level_initialization, xs ("level_initialization"), PATTERN (patterns::level_initialization) },
			{ &m_level_shutdown, &level_shutdown, xs ("level_shutdown"), PATTERN (patterns::level_shutdown) },
			{ &m_read_frame_input, &read_frame_input, xs ("read_frame_input"), PATTERN (patterns::read_frame_input) },
			{ &m_process_input_event, &process_input_event, xs ("process_input_event"), PATTERN (patterns::process_input_event) },
			{ &m_render_decals, &render_decals, xs ("render_decals"), PATTERN (patterns::render_decals) },
			{ &m_render_smoke, &render_smoke, xs ("render_smoke"), PATTERN (patterns::render_smoke) },
			{ &m_draw_flash_effect, &draw_flash_effect, xs ("draw_flash_effect"), PATTERN (patterns::draw_flash_effect) },
			{ &m_set_info, &set_info, xs ("set_info"), PATTERN (patterns::set_info) }
			})) {
			return false;
		}

		return true;
	}

	void cheat::shutdown( )
	{
		m_wnd_proc.reset( );
		m_om_set_render_targets.reset( );
		m_present.reset( );
		m_resize_buffers.reset( );
		m_cmd_interpreter.reset( );
		m_frame_stage_notify.reset( );
		m_create_move.reset( );
		m_handle_view_angles.reset( );
		m_add_entity.reset( );
		m_remove_entity.reset( );
		m_render_view.reset( );
		m_draw_skybox_array.reset( );
		m_light_scene_object.reset( );
		m_draw_scene_object_array.reset( );
		m_draw_scene_object.reset( );
		m_is_glowing.reset( );
		m_get_glow_color.reset( );
		m_generate_primitives.reset( );
		m_parse_report_hit.reset( );
		m_get_scene_param.reset( );
		m_set_shader_param.reset( );
		m_override_view.reset( );
		m_update_fov_sensitivity.reset( );
		m_render_scope.reset( );
		m_render_crosshair.reset( );
		m_prepare_scene_material.reset( );
		m_post_network_data_received.reset( );
		m_draw_overhead.reset( );
		m_draw_legs.reset( );
		m_is_enemy_on_radar.reset( );
		m_get_transforms_for_hitbox_list.reset( );
		m_sort_primitives.reset( );
		m_get_inaccuracy.reset( );
		m_get_interpolated_shoot_position.reset( );
		m_level_initialization.reset( );
		m_read_frame_input.reset( );
		m_process_input_event.reset( );
		m_render_decals.reset( );
		m_render_smoke.reset( );
		m_render_smoke_map.reset( );
		m_render_smoke_unmap.reset( );
		m_draw_flash_effect.reset( );
		m_set_info.reset( );
	}

	HRESULT __fastcall cheat::present( IDXGISwapChain* thisptr, UINT sync_interval, UINT flags )
	{
		rendering::g_context.on_present( thisptr );

		if ( !m_wnd_proc.is_enabled( ) && rendering::g_context.get_window( ) )
		{
			if ( m_wnd_proc.create( reinterpret_cast< void* >( GetWindowLongPtrW( rendering::g_context.get_window( ), GWLP_WNDPROC ) ), &wnd_proc ) )
			{
				m_wnd_proc.enable( );
			}
		}

		if ( !m_om_set_render_targets.is_enabled( ) && rendering::g_context.is_initialized( ) )
		{
			const auto om_addr = memory::get_vfunc( reinterpret_cast< std::uintptr_t >( rendering::g_context.get_context( ) ), 33 );
			if ( m_om_set_render_targets.create( reinterpret_cast< void* >( om_addr ), &om_set_render_targets ) )
			{
				m_om_set_render_targets.enable( );
			}
		}

		return m_present.call<HRESULT>( thisptr, sync_interval, flags );
	}

	HRESULT __fastcall cheat::resize_buffers( IDXGISwapChain* thisptr, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT new_format, UINT swap_chain_flags )
	{
		rendering::g_context.on_resize_buffers( );

		const auto result = m_resize_buffers.call<long>( thisptr, buffer_count, width, height, new_format, swap_chain_flags );
		if ( SUCCEEDED( result ) )
		{
			rendering::g_context.on_resize_buffers_post( thisptr );
		}

		return result;
	}

	LRESULT __stdcall cheat::wnd_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
	{
		if ( msg == WM_ACTIVATE && LOWORD( wparam ) != WA_INACTIVE && rendering::g_menu.is_open( ) && rendering::g_context.get_window( ) == hwnd )
		{
			if ( addresses::globals::input_system )
			{
				memory::call_vfunc<void>( addresses::globals::input_system, 76, false );
			}

			SetCursor( LoadCursor( nullptr, IDC_ARROW ) );
			rendering::g_menu.apply_saved_cursor( );
		}

		if ( msg == WM_KEYDOWN && !( lparam & ( 1 << 30 ) ) && static_cast< int >( wparam ) == settings::g_misc.menu_key )
		{
			rendering::g_menu.toggle( );
			return 0;
		}

		xui::wndproc( msg, wparam, lparam );

		if ( rendering::g_menu.is_open( ) )
		{
			switch ( msg )
			{
			case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
			case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
			case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
			case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
			case WM_MOUSEMOVE:
				return 0;
			default:
				break;
			}
		}

		return m_wnd_proc.call<LRESULT>( hwnd, msg, wparam, lparam );
	}

	void __stdcall cheat::om_set_render_targets( ID3D11DeviceContext* ctx, UINT num_views, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv )
	{
		m_om_set_render_targets.call<void>( ctx, num_views, rtvs, dsv );
	}

	void __fastcall cheat::cmd_interpreter( std::uintptr_t render_thread, std::uintptr_t item, std::uint8_t flag )
	{
		m_cmd_interpreter.call<void>( render_thread, item, flag );
	}

	void __fastcall cheat::frame_stage_notify( std::uintptr_t thisptr, int stage )
	{
		if ( systems::g_entities.is_empty( ) )
		{
			systems::g_entities.force_update( );
		}

		systems::g_local.update( );

		if ( systems::g_local.get( ).is_valid( ) && systems::g_view.has_camera( ) )
		{
			if ( stage == 6 )
			{
				features::changer::g_guns.on_frame_stage_notify( );
				features::combat::g_shared.lc( ).run( );
				features::esp::player::g_chams.bt( ).update( );
				features::esp::player::g_chams.os ().update ();

				features::misc::g_scoreboard_weapons.on_frame_stage_notify ();

			}

			if ( stage == 7 )
			{
				features::changer::g_agents.on_frame_stage_notify( );
				features::changer::g_gloves.on_frame_stage_notify( );
				features::changer::g_knives.on_frame_stage_notify( );

				features::world::g_scene.on_frame_stage_notify( );
				features::world::g_weather.on_frame_stage_notify( );
				features::misc::g_other.on_frame_stage_notify( );
				features::misc::g_impacts.on_frame_stage_notify( );

				
			}
		}

		{
			static auto was_active{ false };
			const auto is_active = settings::g_misc.m_removals.skybox_3d.value;

			if ( is_active != was_active )
			{
				CONVAR ("r_draw3dskybox")->m_value.i1 = is_active;
				was_active = is_active;
			}
		}

		m_frame_stage_notify.call<void>( thisptr, stage );

		if (systems::g_local.get ().is_valid () && systems::g_view.has_camera ()) {
			if (stage == 6) {
				features::misc::g_other.do_kill_feed_preservation( );
			}
		}
	}

	void __fastcall cheat::create_move( std::uintptr_t thisptr, int slot, bool active )
	{
		const auto local = systems::g_local.get( );

		if ( !local.pawn || !local.controller )
		{
			return m_create_move.call<void>( thisptr, slot, active );
		}

		const auto cmd = systems::g_input.get_current_cmd( local.controller );
		if ( cmd && systems::g_input.is_subtick_overwrite( cmd ) )
		{
			systems::g_input.set_weapon_select( cmd, thisptr );
			return;
		}

		m_create_move.call<void>( thisptr, slot, active );

		{
			features::combat::g_shared.invalidate_if_needed( );
			features::combat::g_legit.invalidate_if_needed( );
			features::combat::g_misc.quickpeek( ).reset_if_needed( );
		}

		if ( !local.is_alive || !systems::g_view.has_camera( ) )
		{
			return;
		}

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return;
		}

		const auto last_cmd_processed = memory::read<std::uint32_t>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_nLastCommandNumberProcessed"_hash ) );
		if ( !last_cmd_processed )
		{
			return;
		}

		systems::g_input.update( );
		{
			const auto current_cmd = systems::g_input.get( );
			if ( !current_cmd || !current_cmd->csgo_user_cmd.has_base( ) )
			{
				return;
			}

			systems::g_input.desubtick( current_cmd );
			systems::g_prediction.capture_prestate( local.pawn, movement_services );

			{
				features::movement::g_airstrafe.store_angles( );
				features::combat::g_shared.update( );

				features::combat::g_misc.antiaim( ).on_create_move( current_cmd );
				features::combat::g_misc.autostop( ).on_create_move( current_cmd );
			}

			{
				features::movement::g_slowwalk.on_create_move( current_cmd );
				features::movement::g_edgebug.on_create_move( current_cmd );
				features::movement::g_edgejump.on_create_move( current_cmd );
				features::movement::g_edgestop.on_create_move( current_cmd );
				features::movement::g_jumpbug.on_create_move( current_cmd );
				features::movement::g_bhop.on_create_move( current_cmd );
				features::movement::g_fastladder.on_create_move( current_cmd );

				{
					features::combat::g_rage.on_create_move( current_cmd );
					features::combat::g_legit.on_create_move( current_cmd );
				}

				features::combat::g_misc.duckpeek( ).on_create_move( current_cmd );
				features::movement::g_test_strafer.on_create_move( current_cmd );
				features::movement::g_airstrafe.on_create_move( current_cmd );
				features::misc::g_projectile_trajectory.on_create_move( current_cmd );
			}

			features::combat::g_misc.quickpeek( ).on_create_move( current_cmd );

			if ( current_cmd->csgo_user_cmd.base( )->subtick_moves( ).size( ) > 0
				&& !features::movement::g_test_strafer.handled_this_tick( ) )
			{
				current_cmd->csgo_user_cmd.mutable_base( )->set_forwardmove( 0.0f );
				current_cmd->csgo_user_cmd.mutable_base( )->set_leftmove( 0.0f );
			}

			//systems::g_legit_input.on_create_move( current_cmd );
		}
		systems::g_input.apply( );
	}

	void __fastcall cheat::handle_view_angles( std::uintptr_t thisptr, int a2 )
	{
		const auto view_angles = systems::g_input.get_view_angles( );

		m_handle_view_angles.call<void>( thisptr, a2 );

		systems::g_input.set_view_angles( view_angles );
	}

	void __fastcall cheat::add_entity( std::uintptr_t thisptr, std::uintptr_t entity, std::uint32_t handle )
	{
		systems::g_entities.on_add_entity( entity, handle );

		m_add_entity.call<void>( thisptr, entity, handle );
	}

	void __fastcall cheat::remove_entity( std::uintptr_t thisptr, std::uintptr_t entity, std::uint32_t handle )
	{
		systems::g_entities.on_remove_entity( entity, handle );

		m_remove_entity.call<void>( thisptr, entity, handle );
	}

	std::uintptr_t __fastcall cheat::render_view( std::uintptr_t thisptr )
	{
		const auto result = m_render_view.call<std::uintptr_t>( thisptr );

		systems::g_view.update( thisptr + 0x10 );
		systems::g_frame_data.update( );

		return result;
	}

	void __fastcall cheat::draw_skybox_array( std::uintptr_t thisptr, std::uintptr_t a2, std::uintptr_t mesh_array, int mesh_count, int a5, std::uintptr_t a6, std::uintptr_t a7 )
	{
		features::world::g_scene.on_draw_skybox_array_pre( mesh_array, mesh_count );

		m_draw_skybox_array.call<void>( thisptr, a2, mesh_array, mesh_count, a5, a6, a7 );

		features::world::g_scene.on_draw_skybox_array_post( mesh_array, mesh_count );
	}

	std::uintptr_t __fastcall cheat::light_scene_object( std::uintptr_t thisptr, std::uintptr_t object, std::uintptr_t a3 )
	{
		features::world::g_scene.on_light_scene_object_pre( object );

		const auto result = m_light_scene_object.call<std::uintptr_t>( thisptr, object, a3 );

		features::world::g_scene.on_light_scene_object_post( object );

		return result;
	}

	void __fastcall cheat::draw_scene_object_array( std::uintptr_t thisptr, std::uintptr_t a2, std::uintptr_t object_array )
	{
		m_draw_scene_object_array.call<void>( thisptr, a2, object_array );

		features::world::g_scene.on_draw_scene_object_array( object_array );
	}

	std::uintptr_t __fastcall cheat::draw_scene_object( std::uintptr_t a1, std::uintptr_t a2, std::uintptr_t batch, int batch_count, int a5, std::uintptr_t a6, std::uintptr_t a7, std::uintptr_t a8 )
	{
		features::world::g_scene.on_draw_scene_object( batch, batch_count );

		return m_draw_scene_object.call<std::uintptr_t>( a1, a2, batch, batch_count, a5, a6, a7, a8 );
	}

	bool __fastcall cheat::is_glowing( std::uintptr_t glow_property )
	{
		if ( glow_property )
		{
			const auto owner_entity = memory::read<std::uintptr_t>( glow_property + 0x18 );
			if ( owner_entity )
			{
				const auto owner_hash = fnv1a::runtime_hash( systems::g_entities.get_schema_name( owner_entity ) );
				if ( owner_hash )
				{
					if ( features::esp::player::g_glow.on_is_glowing( owner_entity, owner_hash ) )
					{
						return true;
					}

					if ( features::esp::item::g_glow.on_is_glowing( owner_entity, owner_hash ) )
					{
						return true;
					}
				}
			}
		}

		return m_is_glowing.call<bool>( glow_property );
	}

	void __fastcall cheat::get_glow_color( std::uintptr_t glow_property, float* color )
	{
		if ( glow_property )
		{
			const auto owner_entity = memory::read<std::uintptr_t>( glow_property + 0x18 );
			if ( owner_entity )
			{
				const auto owner_hash = fnv1a::runtime_hash( systems::g_entities.get_schema_name( owner_entity ) );
				if ( owner_hash )
				{
					if ( features::esp::player::g_glow.on_get_glow_color( owner_entity, owner_hash, color ) )
					{
						return;
					}

					if ( features::esp::item::g_glow.on_get_glow_color( owner_entity, owner_hash, color ) )
					{
						return;
					}
				}
			}
		}

		m_get_glow_color.call<void>( glow_property, color );
	}

	void __fastcall cheat::generate_primitives( std::uintptr_t thisptr, std::uintptr_t scene_object, std::uintptr_t scene_view, std::uintptr_t primitive_buffer )
	{
		if ( scene_object )
		{
			if ( features::esp::player::g_chams.bt( ).is_active( scene_object ) )
			{
				return;
			}

			if ( features::esp::player::g_chams.os( ).is_active( scene_object ) ) 
			{
				return;
			}

			const auto owner_handle = memory::read<std::uint32_t>( scene_object + 0xc0 );
			if ( owner_handle )
			{
				const auto owner_entity = systems::g_entities.lookup( owner_handle );
				if ( owner_entity )
				{
					const auto owner_hash = fnv1a::runtime_hash( systems::g_entities.get_schema_name( owner_entity ) );
					if ( owner_hash )
					{
						if ( features::esp::player::g_chams.on_generate_primitives( owner_entity, owner_hash, scene_object, primitive_buffer, m_generate_primitives.original<void( __fastcall* )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t )>( ), thisptr, scene_view ) )
						{
							return;
						}

						if ( features::esp::item::g_chams.on_generate_primitives( owner_entity, owner_hash, scene_object, primitive_buffer, m_generate_primitives.original<void( __fastcall* )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t )>( ), thisptr, scene_view ) )
						{
							return;
						}

						systems::g_model_preview.on_generate_primitives(
							owner_entity,
							owner_hash,
							scene_object,
							primitive_buffer,
							m_generate_primitives.original<void( __fastcall* )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t )>( ),
							thisptr,
							scene_view
						);
					}
				}
			}
		}

		m_generate_primitives.call<void>( thisptr, scene_object, scene_view, primitive_buffer );
	}

	std::uintptr_t __fastcall cheat::parse_report_hit( std::uintptr_t thisptr, std::uintptr_t a2, std::uintptr_t a3 )
	{
		const auto result = m_parse_report_hit.call<std::uintptr_t>( thisptr, a2, a3 );

		features::misc::g_impacts.on_report_hit( thisptr );

		return result;
	}

	__m128* __fastcall cheat::get_scene_param( __m128i* scene_data, __m128* out_buffer, std::uint32_t hash, __m128* default_value )
	{
		const auto result = m_get_scene_param.call<__m128*>( scene_data, out_buffer, hash, default_value );

		features::world::g_scene.on_get_scene_param( out_buffer, hash );

		return result;
	}

	std::uintptr_t __fastcall cheat::set_shader_param( __m128i* map, std::uint32_t hash, __m128i* value )
	{
		features::world::g_scene.on_set_shader_param( value, hash );

		return m_set_shader_param.call<std::uintptr_t>( map, hash, value );
	}

	void __fastcall cheat::override_view( std::uintptr_t thisptr, std::uintptr_t view_setup )
	{
		m_override_view.call<void>( thisptr, view_setup );

		features::misc::g_camera.on_override_view( view_setup );
		features::misc::g_removals.on_override_view( view_setup );
		features::combat::g_misc.duckpeek( ).on_override_view( view_setup );
	}

	void __fastcall cheat::update_fov_sensitivity( std::uintptr_t thisptr )
	{
		m_update_fov_sensitivity.call<void>( thisptr );

		features::misc::g_camera.update_fov_sensitivity( thisptr );
	}

	void __fastcall cheat::render_scope( std::uintptr_t a1, std::uintptr_t a2 )
	{
		m_render_scope.call<void>( a1, a2 );

		if ( settings::g_misc.m_removals.scope.value )
		{
			memory::write<std::uint8_t>( a2 + 4, 0 );
		}
	}

	bool __fastcall cheat::render_crosshair( std::uintptr_t a1 )
	{
		if ( settings::g_misc.m_removals.crosshair.value )
		{
			return false;
		}

		return m_render_crosshair.call<bool>( a1 );
	}

	float __fastcall cheat::prepare_scene_material( std::uintptr_t material, void* a2, float a3 )
	{
		features::misc::g_removals.on_prepare_scene_material( material );

		return m_prepare_scene_material.call<float>( material, a2, a3 );
	}

	void __fastcall cheat::post_network_data_received( std::uintptr_t thisptr )
	{
		m_post_network_data_received.call<void>( thisptr );
	}

	bool __fastcall cheat::draw_overhead( std::uintptr_t pawn )
	{
		if ( settings::g_misc.m_removals.overhead.value && pawn == systems::g_local.get( ).pawn )
		{
			return false;
		}

		return m_draw_overhead.call<bool>( pawn );
	}

	std::uintptr_t __fastcall cheat::draw_legs( std::uintptr_t a1, std::uintptr_t a2, std::uintptr_t a3, std::uintptr_t a4, std::uintptr_t a5 )
	{
		if ( settings::g_misc.m_removals.legs.value )
		{
			return 0;
		}

		return m_draw_legs.call<std::uintptr_t>( a1, a2, a3, a4, a5 );
	}

	char __fastcall cheat::is_enemy_on_radar( std::uintptr_t a1, std::uintptr_t a2 )
	{
		if ( settings::g_misc.reveal_radar.value )
		{
			return 0;
		}

		return m_is_enemy_on_radar.call<char>( a1, a2 );
	}

	bool __fastcall cheat::get_transforms_for_hitbox_list( std::uintptr_t a1, std::uintptr_t a2, int* a3 )
	{
		if ( !features::combat::g_shared.autowalling( ) )
		{
			return m_get_transforms_for_hitbox_list.call<bool>( a1, a2, a3 );
		}

		const auto record = features::combat::g_shared.current_autowall_record( );
		if ( !record || !record->valid || !record->is_applied )
		{
			return m_get_transforms_for_hitbox_list.call<bool>( a1, a2, a3 );
		}

		const auto entity_bone_cache = memory::safe_read<std::uintptr_t>( a1 + 480 ).value_or( 0 );
		const auto target_scene = record->game_scene_node ? record->game_scene_node : memory::read<std::uintptr_t>( record->pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto target_bone_cache = target_scene ? memory::safe_read<std::uintptr_t>( target_scene + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 ).value_or( 0 ) : 0;
		const auto result = m_get_transforms_for_hitbox_list.call<bool>( a1, a2, a3 );

		if ( !result )
		{
			return false;
		}

		const auto is_plausible = [ ]( std::uintptr_t ptr ) noexcept -> bool
			{
				return ptr >= 0x10000ull && ptr < 0x00007FFFFFFFFFFFull;
			};

		if ( !is_plausible( entity_bone_cache ) || !is_plausible( target_bone_cache ) || entity_bone_cache != target_bone_cache )
		{
			return result;
		}

		const auto count = *a3;
		const auto output_array = memory::read<std::uintptr_t>( a2 + 16 );

		if ( !output_array || count <= 0 )
		{
			return result;
		}

		const auto skeleton_ptr_ptr = memory::read<std::uintptr_t>( a1 + 512 );
		if ( !skeleton_ptr_ptr )
		{
			return result;
		}

		const auto skeleton_ptr = memory::read<std::uintptr_t>( skeleton_ptr_ptr );
		if ( !skeleton_ptr )
		{
			return result;
		}

		const auto shape_array = memory::read<std::uintptr_t>( reinterpret_cast< std::uintptr_t >( a3 ) + 8 );
		if ( !shape_array )
		{
			return result;
		}

		for ( auto i = 0; i < count; ++i )
		{
			const auto shape_ptr = shape_array + 16ull * i;
			const auto bone_index = memory::call<int>(PATTERN (patterns::get_bone_index), skeleton_ptr, shape_ptr );

			if ( bone_index < 0 || bone_index >= record->bone_count )
			{
				continue;
			}

			const auto dst = output_array + 32ull * i;
			std::memcpy( reinterpret_cast< void* >( dst ), &record->bones[ bone_index ], 32 );
		}

		return true;
	}

	void __fastcall cheat::sort_primitives( std::uintptr_t thisptr, std::uintptr_t a2, std::uintptr_t a3, std::uint32_t a4 )
	{
		m_sort_primitives.call<void>( thisptr, a2, a3, a4 );

		features::esp::player::g_chams.on_sort_primitives( a3, a4 );
	}

	float __fastcall cheat::get_inaccuracy( std::uintptr_t thisptr, float* a2, float* a3 )
	{
		const auto result = m_get_inaccuracy.call<float>( thisptr, a2, a3 );

#if defined(__clang__) || defined(__GNUC__)
		const auto result_address = reinterpret_cast< std::uintptr_t >( __builtin_return_address( 0 ) );
#else
		const auto result_address = reinterpret_cast< std::uintptr_t >( _ReturnAddress( ) );
#endif

		if ( result_address > PATTERN (patterns::base_fire_guns_get_inaccuracy) && result_address < PATTERN (patterns::base_fire_guns_get_inaccuracy) + 0x600 )
		{
			features::misc::g_impacts.on_base_fire_guns_get_inaccuracy( thisptr, result );
		}

		return result;
	}

	float* __fastcall cheat::get_interpolated_shoot_position( std::uintptr_t thisptr, float* out, int* tick_frac )
	{
		const auto result = m_get_interpolated_shoot_position.call<float*>( thisptr, out, tick_frac );

		features::misc::g_impacts.on_get_interpolated_shoot_position( thisptr, out );

		return result;
	}

	std::uintptr_t __fastcall cheat::level_initialization( std::uintptr_t a1, const char* new_map )
	{
		if ( new_map && new_map[ 0 ] )
		{
			const char* leaf = std::strrchr( new_map, '/' );
			rendering::g_widgets.s_map_name = leaf ? leaf + 1 : new_map;
		}
		else
		{
			rendering::g_widgets.s_map_name.clear( );
		}

		features::world::g_scene.reset_skybox_state( );
		features::misc::g_impacts.on_level_change( );
		features::misc::g_scoreboard_weapons.on_level_change( );

		return m_level_initialization.call<std::uintptr_t>( a1, new_map );
	}

	std::uintptr_t __fastcall cheat::level_shutdown( std::uintptr_t a1 )
	{
		rendering::g_widgets.s_map_name.clear();

		// clear all local player data on level shutdown
		systems::g_local.reset();

		return m_level_shutdown.call<std::uintptr_t>( a1 );
	}

	void __fastcall cheat::read_frame_input( std::uintptr_t a1, std::uint32_t a2 )
	{
		m_read_frame_input.call<void>( a1, a2 );
	}

	void __fastcall cheat::process_input_event( std::uintptr_t csgo_input, int slot, float frametime )
	{
		systems::g_legit_input.on_process_input_event( csgo_input, slot );
		m_process_input_event.call<void>( csgo_input, slot, frametime );
	}

	std::uintptr_t __fastcall cheat::render_decals( std::uintptr_t a1, std::uintptr_t a2, bool a3, bool a4 )
	{
		if ( settings::g_misc.m_removals.decals )
		{
			return 0ull;
		}

		return m_render_decals.call<std::uintptr_t>( a1, a2, a3, a4 );
	}

	void __fastcall cheat::render_smoke( std::uintptr_t a1, std::uintptr_t a2, int a3, int a4, std::uintptr_t a5, std::uintptr_t a6 )
	{
		static std::once_flag alright;
		std::call_once( alright, [ & ]
			{
				if ( !a2 )
				{
					return;
				}

				if ( m_render_smoke_map.create( reinterpret_cast< void* >( memory::get_vfunc( a2, 32 ) ), &render_smoke_map ) )
				{
					m_render_smoke_map.enable( );
				}

				if ( m_render_smoke_unmap.create( reinterpret_cast< void* >( memory::get_vfunc( a2, 33 ) ), &render_smoke_unmap ) )
				{
					m_render_smoke_unmap.enable( );
				}
			} );

		if ( settings::g_misc.m_removals.smoke.value )
		{
			return;
		}

		m_render_smoke.call<void>( a1, a2, a3, a4, a5, a6 );
	}

	std::uintptr_t __fastcall cheat::render_smoke_map( std::uintptr_t thisptr, std::size_t size, std::uintptr_t* out_ptr )
	{
		const auto result = m_render_smoke_map.call<std::uintptr_t>( thisptr, size, out_ptr );

		features::world::g_smoke.on_map( result, size, out_ptr && *out_ptr ? *out_ptr : 0 );

		return result;
	}

	void __fastcall cheat::render_smoke_unmap( std::uintptr_t thisptr, std::uintptr_t ctx, std::size_t size )
	{
		features::world::g_smoke.on_unmap( ctx );
		m_render_smoke_unmap.call<void>( thisptr, ctx, size );
	}

	char __fastcall cheat::set_info( std::uintptr_t rcx, std::uintptr_t a2 )
	{
		/// @TODO: more shit code related to name changer
		//if ( settings::g_misc.m_name_changer.enabled.value && a2 > 0x1000 )
		//{
		//	const auto arg_list = memory::read<std::uintptr_t>( a2 + 0x440 );
		//	if ( arg_list > 0x1000 )
		//	{
		//		const auto key = memory::read<const char*>( arg_list + 8 );
		//		if ( key && _stricmp( key, "name" ) == 0 )
		//		{
		//			const auto name_cvar = systems::convars::find( "name"_hash );
		//			if ( name_cvar )
		//			{
		//				memory::write<std::int32_t>( name_cvar + 0x10, 33408 );
		//			}

		//			const auto& display = features::misc::other::s_display_name;
		//			if ( !display.empty( ) )
		//			{
		//				*reinterpret_cast< const char** >( arg_list + 0x10 ) = display.c_str( );
		//			}
		//		}
		//	}
		//}

		return m_set_info.call<char>( rcx, a2 );
	}

	void __fastcall cheat::draw_flash_effect( std::uintptr_t a1, int a2, std::uintptr_t* a3, std::uintptr_t a4, __m128* a5 )
	{
		if ( settings::g_misc.m_removals.flash_alpha.value < 100.0f && settings::g_misc.m_removals.flash_alpha.value != 0.0f )
		{
			const auto view_pawn = systems::g_local.get( ).view_pawn( );
			if ( view_pawn )
			{
				const auto max = settings::g_misc.m_removals.flash_alpha.value / 100.0f * 255.0f;
				memory::write<float>( view_pawn + SCHEMA( "C_CSPlayerPawnBase", "m_flFlashMaxAlpha"_hash ), max );
			}
		}

		if ( settings::g_misc.m_removals.flash_alpha.value != 0.0f )
		{
			m_draw_flash_effect.call<void>( a1, a2, a3, a4, a5 );
		}
	}

} // namespace hooks