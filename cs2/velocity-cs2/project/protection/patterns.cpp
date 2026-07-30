#include <protection/game_addresses.hpp>

namespace patterns {

	const ::protection::addresses::address_t& add_entity = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:90200000488D05*????????48890733D2+78~"),
		::protection::addresses::address_type::pattern,
		"client.dll:90200000488D05*????????48890733D2+78~");

	const ::protection::addresses::address_t& analyze_pe_module = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:004C89BDA0000000>E8????????84C07445"),
		::protection::addresses::address_type::pattern,
		"client.dll:004C89BDA0000000>E8????????84C07445");

	const ::protection::addresses::address_t& base_fire_guns_get_inaccuracy = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????84C00F84C6FEFFFF"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????84C00F84C6FEFFFF");

	const ::protection::addresses::address_t& build_diagnostic_response = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:8954241053565741544155415641574881ECE0090000488BD948898C24380100004533E44533C033D2488D8C24A0000000E8????????488D05????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:8954241053565741544155415641574881ECE0090000488BD948898C24380100004533E44533C033D2488D8C24A0000000E8????????488D05????????");

	const ::protection::addresses::address_t& cmd_interpreter = ADDRESS_IMPL(
		::protection::addresses::hash("rendersystemdx11.dll:>E8????????4183BDC000000000"),
		::protection::addresses::address_type::pattern,
		"rendersystemdx11.dll:>E8????????4183BDC000000000");

	const ::protection::addresses::address_t& create_move = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:FFFFFFFF488D05*????????48890D????????+28~"),
		::protection::addresses::address_type::pattern,
		"client.dll:FFFFFFFF488D05*????????48890D????????+28~");

	const ::protection::addresses::address_t& csgo_input = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:84C0740C488D0D*????????E8????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:84C0740C488D0D*????????E8????????");

	const ::protection::addresses::address_t& draw_flash_effect = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????440F2F2D????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????440F2F2D????????");

	const ::protection::addresses::address_t& draw_legs = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48894C2420498BCD>E8????????488B55B0"),
		::protection::addresses::address_type::pattern,
		"client.dll:48894C2420498BCD>E8????????488B55B0");

	const ::protection::addresses::address_t& draw_overhead = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????84C00F843C090000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????84C00F843C090000");

	const ::protection::addresses::address_t& draw_scene_object = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:488D05*????????488907488B7C2448+8~"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:488D05*????????488907488B7C2448+8~");

	const ::protection::addresses::address_t& draw_scene_object_array = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:488BC44889501048894808555356574154415541564157488DA838F9FFFF"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:488BC44889501048894808555356574154415541564157488DA838F9FFFF");

	const ::protection::addresses::address_t& draw_skybox_array = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:06000000488D05*????????48890133FF+60~"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:06000000488D05*????????48890133FF+60~");

	const ::protection::addresses::address_t& engine_client_cmd = ADDRESS_IMPL(
		::protection::addresses::hash("engine2.dll:488BC448895808488968104889701857415641574881EC700100000F2970D8410FB6E98D42FC4D8BF88BFA4C8BF183F801763EBAFFFFFFFF488D0D????????E8????????4885C0750B"),
		::protection::addresses::address_type::pattern,
		"engine2.dll:488BC448895808488968104889701857415641574881EC700100000F2970D8410FB6E98D42FC4D8BF88BFA4C8BF183F801763EBAFFFFFFFF488D0D????????E8????????4885C0750B");

	const ::protection::addresses::address_t& entity_list = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????8BFBC1EB0E"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????8BFBC1EB0E");

	const ::protection::addresses::address_t& filesystem_close = ADDRESS_IMPL(
		::protection::addresses::hash("filesystem_stdio.dll:>E8????????FFD34C8BA42498000000"),
		::protection::addresses::address_type::pattern,
		"filesystem_stdio.dll:>E8????????FFD34C8BA42498000000");

	const ::protection::addresses::address_t& find_hud_element = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????4885C0488D751C"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????4885C0488D751C");

	const ::protection::addresses::address_t& frame_input_ring_base = ADDRESS_IMPL(
		::protection::addresses::hash("engine2.dll:488D05*????????0F1004C8"),
		::protection::addresses::address_type::pattern,
		"engine2.dll:488D05*????????0F1004C8");

	const ::protection::addresses::address_t& frame_input_ring_idx = ADDRESS_IMPL(
		::protection::addresses::hash("engine2.dll:486315*????????83FA0A7D61"),
		::protection::addresses::address_type::pattern,
		"engine2.dll:486315*????????83FA0A7D61");

	const ::protection::addresses::address_t& frame_stage_notify = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C241848896C2420574883EC40488BF9"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C241848896C2420574883EC40488BF9");

	const ::protection::addresses::address_t& game_entity_system = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????EB028BC6"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????EB028BC6");

	const ::protection::addresses::address_t& game_event_get_controller = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D05*????????4D8BF8488901+80~"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D05*????????4D8BF8488901+80~");

	const ::protection::addresses::address_t& game_event_get_float = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????0F28D8895C2420"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????0F28D8895C2420");

	const ::protection::addresses::address_t& game_event_get_int = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????3D00800000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????3D00800000");

	const ::protection::addresses::address_t& game_event_get_pawn = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D05*????????4D8BF8488901+88~"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D05*????????4D8BF8488901+88~");

	const ::protection::addresses::address_t& game_event_get_string = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????85DB0F9FC3"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????85DB0F9FC3");

	const ::protection::addresses::address_t& game_event_manager = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????498BF0498BD9~"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????498BF0498BD9~");

	const ::protection::addresses::address_t& game_rules = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????4C897010"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????4C897010");

	const ::protection::addresses::address_t& game_scene_node_set_mesh_group = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????8B852C850100"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????8B852C850100");

	const ::protection::addresses::address_t& game_scene_node_set_skeleton = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????4084ED7417"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????4084ED7417");

	const ::protection::addresses::address_t& game_trace_manager = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????488D3452~"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????488D3452~");

	const ::protection::addresses::address_t& generate_primitives = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:488D05*????????488907488B7C2448+20~"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:488D05*????????488907488B7C2448+20~");

	const ::protection::addresses::address_t& get_aim_punch = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:140000488D542420>E8????????F30F1015????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:140000488D542420>E8????????F30F1015????????");

	const ::protection::addresses::address_t& get_bone_index = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????85C00F888A000000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????85C00F888A000000");

	const ::protection::addresses::address_t& get_glow_color = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????F30F10BE300E0000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????F30F10BE300E0000");

	const ::protection::addresses::address_t& get_inaccuracy = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24105556574881ECB0000000440F29842480000000498BF8488BEA488BD9E8????????488BF0450F57C0"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24105556574881ECB0000000440F29842480000000498BF8488BEA488BD9E8????????488BF0450F57C0");

	const ::protection::addresses::address_t& get_interp_amount = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????418B9668030000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????418B9668030000");

	const ::protection::addresses::address_t& get_interpolated_shoot_position = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????8B86F8030000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????8B86F8030000");

	const ::protection::addresses::address_t& get_net_channel = ADDRESS_IMPL(
		::protection::addresses::hash("engine2.dll:4C8B05????????4D85C07410"),
		::protection::addresses::address_type::pattern,
		"engine2.dll:4C8B05????????4D85C07410");

	const ::protection::addresses::address_t& get_scene_param = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????F30F1038"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????F30F1038");

	const ::protection::addresses::address_t& get_tick_view_angles = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C2408574881ECF0000000F30F100A488D8C2410010000418BD8488BFAE8????????F30F104F04"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C2408574881ECF0000000F30F100A488D8C2410010000418BD8488BFAE8????????F30F104F04");

	const ::protection::addresses::address_t& get_transforms_for_hitbox_list = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????84C0410F45EC"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????84C0410F45EC");

	const ::protection::addresses::address_t& get_usercmd = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:80000000498B4F10>E8????????4885C07419"),
		::protection::addresses::address_type::pattern,
		"client.dll:80000000498B4F10>E8????????4885C07419");

	const ::protection::addresses::address_t& get_usercmd_base = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4883EC28>E8????????8B8010590000"),
		::protection::addresses::address_type::pattern,
		"client.dll:4883EC28>E8????????8B8010590000");

	const ::protection::addresses::address_t& get_view_angles = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:8B0D????????8BD3>E8????????F20F1000"),
		::protection::addresses::address_type::pattern,
		"client.dll:8B0D????????8BD3>E8????????F20F1000");

	const ::protection::addresses::address_t& get_world_group_handle = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????418B5F10"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????418B5F10");

	const ::protection::addresses::address_t& get_world_group_id = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????F3410F10B674FFFFFF"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????F3410F10B674FFFFFF");

	const ::protection::addresses::address_t& global_vars = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B05*????????448B4044"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B05*????????448B4044");

	const ::protection::addresses::address_t& handle_view_angles = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:FFFFFFFF488D05*????????48890D????????+40~"),
		::protection::addresses::address_type::pattern,
		"client.dll:FFFFFFFF488D05*????????48890D????????+40~");

	const ::protection::addresses::address_t& history_field_alloc = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????488BD0488D4E28"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????488BD0488D4E28");

	const ::protection::addresses::address_t& hud = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B05*????????4885C07471"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B05*????????4885C07471");

	const ::protection::addresses::address_t& hud_death_notice_clear = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:85C07509488D4EE0>E8????????4C8D9C24D0000000"),
		::protection::addresses::address_type::pattern,
		"client.dll:85C07509488D4EE0>E8????????4C8D9C24D0000000");

	const ::protection::addresses::address_t& hud_weapon_selection_update = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:498BE35FC3488BCB>E8????????488BCBE8????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:498BE35FC3488BCB>E8????????488BCBE8????????");

	const ::protection::addresses::address_t& init_particle_path_buffer = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????488D05????????C745808CF0DC51"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????488D05????????C745808CF0DC51");

	const ::protection::addresses::address_t& init_particle_path_buffer_alt = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24??574883EC??8B41??488D79"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24??574883EC??8B41??488D79");

	const ::protection::addresses::address_t& is_enemy_on_radar = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????4C8D6710"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????4C8D6710");

	const ::protection::addresses::address_t& is_glowing = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:0000488BEA488BF9>E8????????4533F684C0"),
		::protection::addresses::address_type::pattern,
		"client.dll:0000488BEA488BF9>E8????????4533F684C0");

	const ::protection::addresses::address_t& item_system = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B1D*????????458B2C06"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B1D*????????458B2C06");

	const ::protection::addresses::address_t& kv3_alloc = ADDRESS_IMPL(
		::protection::addresses::hash("tier0.dll:>E8????????4C8BF0EB03"),
		::protection::addresses::address_type::pattern,
		"tier0.dll:>E8????????4C8BF0EB03");

	const ::protection::addresses::address_t& kv3_load = ADDRESS_IMPL(
		::protection::addresses::hash("tier0.dll:44242848897C2420>E8????????0FB6D88B4C2444"),
		::protection::addresses::address_type::pattern,
		"tier0.dll:44242848897C2420>E8????????0FB6D88B4C2444");

	const ::protection::addresses::address_t& level_initialization = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D05*????????C6411000+B8~"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D05*????????C6411000+B8~");

	const ::protection::addresses::address_t& level_shutdown = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4883EC??488B0D????????488D15????????4533C94533C0488B01FF50304885C074??488B0D????????488BD04C8B0141FF50404883C4??"),
		::protection::addresses::address_type::pattern,
		"client.dll:4883EC??488B0D????????488D15????????4533C94533C0488B01FF50304885C074??488B0D????????488BD04C8B0141FF50404883C4??");

	const ::protection::addresses::address_t& light_data_queue = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:488B05*????????48C1E104+8"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:488B05*????????48C1E104+8");

	const ::protection::addresses::address_t& light_scene_object = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:>E8????????440F285C2460"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:>E8????????440F285C2460");

	const ::protection::addresses::address_t& local_player_controller = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48391D*????????7504B001"),
		::protection::addresses::address_type::pattern,
		"client.dll:48391D*????????7504B001");

	const ::protection::addresses::address_t& log_internal = ADDRESS_IMPL(
		::protection::addresses::hash("tier0.dll:>E8????????448B55B3"),
		::protection::addresses::address_type::pattern,
		"tier0.dll:>E8????????448B55B3");

	const ::protection::addresses::address_t& material_create = ADDRESS_IMPL(
		::protection::addresses::hash("materialsystem2.dll:488D05*????????48895C2428+E8~"),
		::protection::addresses::address_type::pattern,
		"materialsystem2.dll:488D05*????????48895C2428+E8~");

	const ::protection::addresses::address_t& material_manager = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????80A5E7000000EF"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????80A5E7000000EF");

	const ::protection::addresses::address_t& net_client = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????4C8D4520"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????4C8D4520");

	const ::protection::addresses::address_t& override_view = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:A8000000488D05*????????4C89742420+78~"),
		::protection::addresses::address_type::pattern,
		"client.dll:A8000000488D05*????????4C89742420+78~");

	const ::protection::addresses::address_t& parse_report_hit = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E80945F5FF"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E80945F5FF");

	const ::protection::addresses::address_t& particle_create_effect = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????837C2440FF"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????837C2440FF");

	const ::protection::addresses::address_t& particle_destroy_effect = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D05????????0F29B424B0000000488987D8000000E8????????8B97E000000041B101450FB6C1488BC8>E8????????E8????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D05????????0F29B424B0000000488987D8000000E8????????8B97E000000041B101450FB6C1488BC8>E8????????E8????????");

	const ::protection::addresses::address_t& particle_manager = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B1D*????????44896C2450"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B1D*????????44896C2450");

	const ::protection::addresses::address_t& particle_set_control_point = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????F2410F104668"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????F2410F104668");

	const ::protection::addresses::address_t& particle_set_entity_binding = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????8B742450"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????8B742450");

	const ::protection::addresses::address_t& particle_stop_effect = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:C6410800488D05*????????4889014533F6+18~"),
		::protection::addresses::address_type::pattern,
		"client.dll:C6410800488D05*????????4889014533F6+18~");

	const ::protection::addresses::address_t& planted_c4 = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4C8B05*????????85D27E25"),
		::protection::addresses::address_type::pattern,
		"client.dll:4C8B05*????????85D27E25");

	const ::protection::addresses::address_t& post_network_data_received = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C241048894C24085556574154415541564157488DAC24B0FCFFFF4881EC500400004C8BE9488B0D????????488B01FF90B8000000488BC8488B10FF5238498BCD8945848BF0E8????????498BCDE8????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C241048894C24085556574154415541564157488DAC24B0FCFFFF4881EC500400004C8BE9488B0D????????488B01FF90B8000000488BC8488B10FF5238498BCD8945848BF0E8????????498BCDE8????????");

	const ::protection::addresses::address_t& prediction_finish_move = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????488B8398010000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????488B8398010000");

	const ::protection::addresses::address_t& prediction_player = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488BD748893D*????????488BCB"),
		::protection::addresses::address_type::pattern,
		"client.dll:488BD748893D*????????488BCB");

	const ::protection::addresses::address_t& prediction_process_movement = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:8BC5498BD5488BCB>E8????????488B034C8BC5"),
		::protection::addresses::address_type::pattern,
		"client.dll:8BC5498BD5488BCB>E8????????488B034C8BC5");

	const ::protection::addresses::address_t& prediction_reset_pawn = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????C1EB09F6D3"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????C1EB09F6D3");

	const ::protection::addresses::address_t& prediction_seed = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:8B3D*????????488B03488BCB"),
		::protection::addresses::address_type::pattern,
		"client.dll:8B3D*????????488B03488BCB");

	const ::protection::addresses::address_t& prediction_set_pawn = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????660F6E8DD8000000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????660F6E8DD8000000");

	const ::protection::addresses::address_t& prediction_set_state = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????4963DF4585FF"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????4963DF4585FF");

	const ::protection::addresses::address_t& prediction_setup_move = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:D5488BCD458B4044>E8????????488B034C8BC5"),
		::protection::addresses::address_type::pattern,
		"client.dll:D5488BCD458B4044>E8????????488B034C8BC5");

	const ::protection::addresses::address_t& prediction_state = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????33D28B5B38"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????33D28B5B38");

	const ::protection::addresses::address_t& prepare_scene_material = ADDRESS_IMPL(
		::protection::addresses::hash("materialsystem2.dll:48895C24084889742410574883EC30488B5920"),
		::protection::addresses::address_type::pattern,
		"materialsystem2.dll:48895C24084889742410574883EC30488B5920");

	const ::protection::addresses::address_t& process_input_event = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:CCCC48895C2408574883EC20C6410800488D05*????????488901488BD9+20~"),
		::protection::addresses::address_type::pattern,
		"client.dll:CCCC48895C2408574883EC20C6410800488D05*????????488901488BD9+20~");

	const ::protection::addresses::address_t& read_frame_input = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????4D8BC58BD3"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????4D8BC58BD3");

	const ::protection::addresses::address_t& remove_entity = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:90200000488D05*????????48890733D2+80~"),
		::protection::addresses::address_type::pattern,
		"client.dll:90200000488D05*????????48890733D2+80~");

	const ::protection::addresses::address_t& render_crosshair = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488BC844887C2430>E8????????84C00F849B000000"),
		::protection::addresses::address_type::pattern,
		"client.dll:488BC844887C2430>E8????????84C00F849B000000");

	const ::protection::addresses::address_t& render_decals = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:B001488BD7498BCD>E8????????BAFFFFFFFF"),
		::protection::addresses::address_type::pattern,
		"client.dll:B001488BD7498BCD>E8????????BAFFFFFFFF");

	const ::protection::addresses::address_t& render_game_system = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????488B14FA~"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????488B14FA~");

	const ::protection::addresses::address_t& render_scope = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488BC453574883EC68488BFA"),
		::protection::addresses::address_type::pattern,
		"client.dll:488BC453574883EC68488BFA");

	const ::protection::addresses::address_t& render_smoke = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:5C24284889442420>E8????????488B5C2460"),
		::protection::addresses::address_type::pattern,
		"client.dll:5C24284889442420>E8????????488B5C2460");

	const ::protection::addresses::address_t& render_view = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4C8BDC49895B1049896B18565741564881ECF0000000410F2973D8498D53084C8BF1410F297BC8488B0D????????450F2943B8488B01FF9008030000C644242801488D15????????4533C9488D8C248000000041B8310100008B0089442420E8????????33D2498BCEE8????????488D8C2480000000"),
		::protection::addresses::address_type::pattern,
		"client.dll:4C8BDC49895B1049896B18565741564881ECF0000000410F2973D8498D53084C8BF1410F297BC8488B0D????????450F2943B8488B01FF9008030000C644242801488D15????????4533C9488D8C248000000041B8310100008B0089442420E8????????33D2498BCEE8????????488D8C2480000000");

	const ::protection::addresses::address_t& resource_system_load = ADDRESS_IMPL(
		::protection::addresses::hash("resourcesystem.dll:48895C24??48896C24??48897424??574883EC??488B01"),
		::protection::addresses::address_type::pattern,
		"resourcesystem.dll:48895C24??48896C24??48897424??574883EC??488B01");

	const ::protection::addresses::address_t& resource_system_precache = ADDRESS_IMPL(
		::protection::addresses::hash("resourcesystem.dll:405355574881EC80000000"),
		::protection::addresses::address_type::pattern,
		"resourcesystem.dll:405355574881EC80000000");

	const ::protection::addresses::address_t& serialize_move_crc = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:C7488D55D0488BCE>E8????????488B55E8"),
		::protection::addresses::address_type::pattern,
		"client.dll:C7488D55D0488BCE>E8????????488B55E8");

	const ::protection::addresses::address_t& service_read = ADDRESS_IMPL(
		::protection::addresses::hash("filesystem_stdio.dll:00488907488D05*????????488987E0000000~"),
		::protection::addresses::address_type::pattern,
		"filesystem_stdio.dll:00488907488D05*????????488987E0000000~");

	const ::protection::addresses::address_t& set_info = ADDRESS_IMPL(
		::protection::addresses::hash("engine2.dll:40554157488D6C24??4881EC????????4533FF"),
		::protection::addresses::address_type::pattern,
		"engine2.dll:40554157488D6C24??4881EC????????4533FF");

	const ::protection::addresses::address_t& set_player_model = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D15????????488BCB>E8????????488BD7488BCB"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D15????????488BCB>E8????????488BD7488BCB");

	const ::protection::addresses::address_t& set_shader_param = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????0FB64325"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????0FB64325");

	const ::protection::addresses::address_t& set_view_angles = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????488B8E20120000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????488B8E20120000");

	const ::protection::addresses::address_t& set_voice_data = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????4C39B5C8140000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????4C39B5C8140000");

	const ::protection::addresses::address_t& simulation_player = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4C3905*????????410F94C6"),
		::protection::addresses::address_type::pattern,
		"client.dll:4C3905*????????410F94C6");

	const ::protection::addresses::address_t& sort_primitives = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:>E8????????F041FF86F40B0000"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:>E8????????F041FF86F40B0000");

	const ::protection::addresses::address_t& play_sound = ADDRESS_IMPL(
		::protection::addresses::hash("soundsystem.dll:4C8BDC55415541564157"),
		::protection::addresses::address_type::pattern,
		"soundsystem.dll:4C8BDC55415541564157");

	const ::protection::addresses::address_t& string_copy = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????0F104588"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????0F104588");

	const ::protection::addresses::address_t& subtick_move_alloc = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B54CA088D4101894708EB16488B0F>E8????????488BD0488BCF"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B54CA088D4101894708EB16488B0F>E8????????488BD0488BCF");

	const ::protection::addresses::address_t& swap_chain_storage = ADDRESS_IMPL(
		::protection::addresses::hash("rendersystemdx11.dll:48892D*????????660F7F05????????"),
		::protection::addresses::address_type::pattern,
		"rendersystemdx11.dll:48892D*????????660F7F05????????");

	const ::protection::addresses::address_t& trace_bullet = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????4532ED410F28FD"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????4532ED410F28FD");

	const ::protection::addresses::address_t& trace_bullet_data_init = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C240848896C2410488974241857415641574883EC50F20F1002"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C240848896C2410488974241857415641574883EC50F20F1002");

	const ::protection::addresses::address_t& trace_bullet_free = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????3BDE744E"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????3BDE744E");

	const ::protection::addresses::address_t& trace_bullet_update = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????F3440F106574"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????F3440F106574");

	const ::protection::addresses::address_t& trace_filter_init = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:7D78F3440F58457C>E8????????F30F1005????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:7D78F3440F58457C>E8????????F30F1005????????");

	const ::protection::addresses::address_t& trace_filter_set_collision = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:000041B800010000>E8????????4C397F38"),
		::protection::addresses::address_type::pattern,
		"client.dll:000041B800010000>E8????????4C397F38");

	const ::protection::addresses::address_t& trace_hull = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????0F2F754C"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????0F2F754C");

	const ::protection::addresses::address_t& trace_ray = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:20488D95E0000000>E8????????488B3D????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:20488D95E0000000>E8????????488B3D????????");

	const ::protection::addresses::address_t& trace_ray_entity = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:44246848897C2420>E8????????488B3D????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:44246848897C2420>E8????????488B3D????????");

	const ::protection::addresses::address_t& update_fov_sensitivity = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C240848896C24104889742418574883EC20488BB918120000"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C240848896C24104889742418574883EC20488BB918120000");

	const ::protection::addresses::address_t& utl_vector_push = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????4C8BD0458B4A10"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????4C8BD0458B4A10");

	const ::protection::addresses::address_t& view_matrix = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D0D*????????48C1E006"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D0D*????????48C1E006");

	const ::protection::addresses::address_t& viewmodel_update_mesh = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????498D8D??????????????488D5424"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????498D8D??????????????488D5424");

	const ::protection::addresses::address_t& weapon_calculate_spread = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:28F3440F11442420>E8????????488D85B0000000"),
		::protection::addresses::address_type::pattern,
		"client.dll:28F3440F11442420>E8????????488D85B0000000");

	const ::protection::addresses::address_t& weapon_get_entity_index = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????448B4500"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????448B4500");

	const ::protection::addresses::address_t& weapon_get_model_path = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C2410564883EC20488B1D????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C2410564883EC20488B1D????????");

	const ::protection::addresses::address_t& weapon_get_recoil_offset = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????488D44243C"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????488D44243C");

	const ::protection::addresses::address_t& weapon_get_viewmodel = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:40534883EC20488BD9E8????????4883BB8803000000"),
		::protection::addresses::address_type::pattern,
		"client.dll:40534883EC20488BD9E8????????4883BB8803000000");

	const ::protection::addresses::address_t& weapon_recoil_data = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D3D*????????498D4308"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D3D*????????498D4308");

	const ::protection::addresses::address_t& weapon_set_mesh_group_mask = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????E9????????83B8A80A0000000F8FAF00000083B8C00A0000000F8FA20000000F57C048895DA7488D15????????660F7F45C7"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????E9????????83B8A80A0000000F8FAF00000083B8C00A0000000F8FA20000000F57C048895DA7488D15????????660F7F45C7");

	const ::protection::addresses::address_t& weapon_update_accuracy = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:405741564883EC68488BF9E8????????4C8BF04885C0"),
		::protection::addresses::address_type::pattern,
		"client.dll:405741564883EC68488BF9E8????????4C8BF04885C0");

	const ::protection::addresses::address_t& weapon_update_composite_material = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:00400FB6D7488BCB>E8????????488D4C2420"),
		::protection::addresses::address_type::pattern,
		"client.dll:00400FB6D7488BCB>E8????????488D4C2420");

	const ::protection::addresses::address_t& weapon_update_mesh = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????498D8C2408060000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????498D8C2408060000");

	const ::protection::addresses::address_t& weapon_update_skin = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C241048896C2418488974242057415641574883EC40488BEA488BF1488D542460E8????????488B4610"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C241048896C2418488974242057415641574883EC40488BEA488BF1488D542460E8????????488B4610");

} // namespace patterns
