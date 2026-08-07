#include <pch/pch.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

#include "../../rendering.hpp"

namespace rendering {

	void menu::draw_world( float group_w ) const
	{
		auto& w = settings::g_world;

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap + tokens::sidebar_w + tokens::gap;
		const auto body_y = wy + tokens::gap + tokens::subtab_bar_h + tokens::gap;
		const auto content_w = this->m_w - tokens::gap * 2.0f - tokens::sidebar_w - tokens::gap;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		const auto subtab = this->m_subtab;

		xui::layout::set_cursor( content_x - wx, body_y - wy );

		if ( subtab == 0 )
		{
			auto& item = settings::g_esp.m_item;
			auto& proj = settings::g_esp.m_projectile;
			auto& other = settings::g_esp.m_other;

			constexpr const char* display_types[ ]{ "文本", "图标", "文本 + 图标" };
			constexpr const char* cham_material_names[ ]{
				"液体", "金属", "哑光", "平面", "泛光", "描边", "发光", "电弧", "扭曲", "全息", "珍珠",
				"液体 (iz)", "哑光 (iz)", "平面 (iz)", "泛光 (iz)", "描边 (iz)", "发光 (iz)", "扭曲 (iz)", "全息 (iz)"
			};
			constexpr auto cham_material_count = static_cast< int >( settings::esp::cham_ids::count );

			auto draw_chams_layer = [ & ]( const char* label, const char* popup_id, settings::esp::chams_layer& layer )
				{
					xui::checkbox( label, layer.enabled );
					if ( xui::begin_popup( popup_id, 220.0f ) )
					{
						xui::combo( "材质", layer.material.value, cham_material_names, cham_material_count );
						xui::color_picker( "颜色", layer.color );
						xui::end_popup( );
					}
				};

			xui::layout::set_cursor( content_x - wx, body_y - wy );

			if ( xui::begin_child( "##esp_items", col_w ) )
			{
				static int item_group{};
				xui::combo( "分组##item_sel", item_group, settings::esp::item::k_group_names, settings::esp::item::k_group_count );

				xui::layout::separator( );

				xui::checkbox( "道具 ESP", item.m_overlay.group_toggle( item_group ) );
				if ( xui::begin_popup( "##ie_grp_cfg", 220.0f ) )
				{
					auto& g = item.m_overlay.groups[ item_group ];

					char id_d[ 32 ]{}, id_m[ 32 ]{}, id_t[ 32 ]{}, id_i[ 32 ]{};
					std::snprintf( id_d, sizeof( id_d ), "显示##ie%d", item_group );
					std::snprintf( id_m, sizeof( id_m ), "最大距离##ie%d", item_group );
					std::snprintf( id_t, sizeof( id_t ), "文字颜色##ie%d", item_group );
					std::snprintf( id_i, sizeof( id_i ), "图标颜色##ie%d", item_group );

					xui::combo( id_d, g.display.value, display_types, 3 );
					xui::slider_float( id_m, g.max_distance, 1.0f, 200.0f, "%.0fm" );
					xui::color_picker( id_t, g.text_color );
					xui::color_picker( id_i, g.icon_color );
					xui::end_popup( );
				}

				xui::checkbox( "道具 Chams", item.m_chams.group_toggle( item_group ) );
				if ( xui::begin_popup( "##ic_grp_cfg", 220.0f ) )
				{
					auto& g = item.m_chams.groups[ item_group ];

					char id_p[ 48 ]{}, id_pp[ 48 ]{}, id_s[ 48 ]{}, id_sp[ 48 ]{};
					std::snprintf( id_p, sizeof( id_p ), "主要##ic%d", item_group );
					std::snprintf( id_pp, sizeof( id_pp ), "##ic_p%d", item_group );
					std::snprintf( id_s, sizeof( id_s ), "次要##ic%d", item_group );
					std::snprintf( id_sp, sizeof( id_sp ), "##ic_s%d", item_group );

					draw_chams_layer( id_p, id_pp, g.primary );
					draw_chams_layer( id_s, id_sp, g.secondary );
					xui::end_popup( );
				}

				xui::checkbox( "道具发光", item.m_glow.group_toggle( item_group ) );
				if ( xui::begin_popup( "##ig_grp_cfg", 220.0f ) )
				{
					char id[ 32 ]{};
					std::snprintf( id, sizeof( id ), "颜色##ig%d", item_group );
					xui::color_picker( id, item.m_glow.groups[ item_group ].color );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			xui::layout::set_cursor( right_x - wx, body_y - wy );

			if ( xui::begin_child( "##esp_projectiles", col_w ) )
			{
				static auto proj_group{ 0 };
				xui::combo( "分组##proj_sel", proj_group, settings::esp::projectile::k_group_names, settings::esp::projectile::k_group_count );

				xui::layout::separator( );

				const auto is_inferno = ( proj_group == 5 );

				xui::checkbox( is_inferno ? "火焰 ESP" : "投掷物 ESP", proj.m_overlay.group_toggle( proj_group ) );

				if ( !is_inferno )
				{
					if ( xui::begin_popup( "##pe_grp_cfg", 220.0f ) )
					{
						auto& g = proj.m_overlay.groups[ proj_group ];

						char id_d[ 32 ]{}, id_m[ 32 ]{}, id_t[ 32 ]{}, id_i[ 32 ]{};
						std::snprintf( id_d, sizeof( id_d ), "显示##pe%d", proj_group );
						std::snprintf( id_m, sizeof( id_m ), "最大距离##pe%d", proj_group );
						std::snprintf( id_t, sizeof( id_t ), "文字颜色##pe%d", proj_group );
						std::snprintf( id_i, sizeof( id_i ), "图标颜色##pe%d", proj_group );

						xui::combo( id_d, g.display.value, display_types, 3 );
						xui::slider_float( id_m, g.max_distance, 1.0f, 200.0f, "%.0fm" );
						xui::color_picker( id_t, g.text_color );
						xui::color_picker( id_i, g.icon_color );
						xui::end_popup( );
					}
				}
				else
				{
					if ( xui::begin_popup( "##inferno_cfg", 220.0f ) )
					{
						xui::color_picker( "填充颜色##inf", proj.m_overlay.m_infernos.fill_color );
						xui::color_picker( "轮廓颜色##inf", proj.m_overlay.m_infernos.outline_color );
						xui::slider_float( "轮廓粗细##inf", proj.m_overlay.m_infernos.outline_thickness, 0.5f, 5.0f, "%.1f" );
						xui::checkbox( "发光##inf", proj.m_overlay.m_infernos.glow );
						xui::slider_float( "发光强度##inf", proj.m_overlay.m_infernos.glow_strength, 0.1f, 1.0f, "%.2f" );
						xui::end_popup( );
					}
				}

				const auto indicator_id = proj_group == 0 ? 0 : proj_group == 3 ? 1 : proj_group == 5 ? 2 : -1;
				if ( indicator_id >= 0 )
				{
					xui::checkbox( is_inferno ? "指示器" : "落点指示器", proj.m_overlay.m_indicator.get_group( indicator_id ).enabled );
					if ( xui::begin_popup( "##ind_grp_cfg", 220.0f ) )
					{
						auto& g = proj.m_overlay.m_indicator.get_group( indicator_id );

						char id_a[ 32 ]{}, id_i[ 32 ]{}, id_b[ 32 ]{}, id_g[ 32 ]{}, id_gs[ 32 ]{};
						std::snprintf( id_a, sizeof( id_a ), "轨迹颜色##ind%d", indicator_id );
						std::snprintf( id_i, sizeof( id_i ), "图标颜色##ind%d", indicator_id );
						std::snprintf( id_b, sizeof( id_b ), "背景##ind%d", indicator_id );
						std::snprintf( id_g, sizeof( id_g ), "发光##ind%d", indicator_id );
						std::snprintf( id_gs, sizeof( id_gs ), "发光强度##ind%d", indicator_id );

						xui::color_picker( id_a, g.arc_color );
						xui::color_picker( id_i, g.icon_color );
						xui::color_picker( id_b, g.background_color );
						xui::checkbox( id_g, g.glow );
						xui::slider_float( id_gs, g.glow_strength, 0.1f, 1.0f, "%.2f" );
						xui::end_popup( );
					}
				}

				xui::end_child( );
			}

			if ( xui::begin_child( "##esp_other", col_w ) )
			{
				xui::checkbox( "炸弹计时器", other.bomb_timer );
				xui::checkbox( "观战列表", other.spectator_list );

				xui::end_child( );
			}
		}

		if ( subtab == 1 )
		{
			auto& scene = w.m_scene;

			if ( xui::begin_child( "##world_scene_left", col_w ) )
			{
				xui::checkbox( "天空盒材质", scene.skybox.custom_skybox );
				if ( xui::begin_popup( "##skybox_popup", 220.0f ) )
				{
					const auto& skyboxes = features::world::g_scene.get_skyboxes( );
					if ( !skyboxes.empty( ) )
					{
						std::vector<const char*> names;
						names.reserve( skyboxes.size( ) );
						for ( const auto& skybox : skyboxes )
						{
							names.push_back( skybox.display_name.c_str( ) );
						}

						scene.skybox.selected_skybox.value = std::clamp(
							scene.skybox.selected_skybox.value, 0,
							static_cast<int>( skyboxes.size( ) ) - 1 );
						xui::combo(
							"天空盒", scene.skybox.selected_skybox.value,
							names.data( ), static_cast<int>( names.size( ) ) );
					}
					xui::end_popup( );
				}

				xui::checkbox( "天空盒颜色", scene.skybox.custom_color );
				if ( xui::begin_popup( "##skycolor_popup", 220.0f ) )
				{
					xui::color_picker( "天空颜色", scene.skybox.skybox_color );
					xui::color_picker( "云朵颜色", scene.skybox.cloud_color );
					xui::color_picker( "太阳颜色", scene.skybox.sun_color );
					xui::end_popup( );
				}

				xui::checkbox( "世界颜色", scene.world_setting );
				if ( xui::begin_popup( "##worldcolor_popup", 220.0f ) )
				{
					xui::color_picker( "颜色##world", scene.world_color );
					xui::end_popup( );
				}

				xui::checkbox( "光照", scene.lighting );
				if ( xui::begin_popup( "##lighting_popup", 220.0f ) )
				{
					xui::slider_float( "强度##light", scene.lighting_intensity, 0.0f, 2.0f, "%.2f" );
					xui::color_picker( "颜色##light", scene.lighting_color );
					xui::end_popup( );
				}

				xui::checkbox( "泛光", scene.bloom );
				if ( xui::begin_popup( "##bloom_popup", 220.0f ) )
				{
					xui::slider_float( "数值##bloom", scene.bloom_value, 0.0f, 2.0f, "%.2f" );
					xui::end_popup( );
				}

				xui::checkbox( "伽马", scene.gamma );
				if ( xui::begin_popup( "##gamma_popup", 220.0f ) )
				{
					xui::slider_float( "数值##gamma", scene.gamma_value, 0.5f, 5.0f, "%.1f" );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			xui::layout::set_cursor( right_x - wx, body_y - wy );

			if ( xui::begin_child( "##world_scene_right", col_w ) )
			{
				xui::checkbox( "景深", scene.dof );
				if ( xui::begin_popup( "##dof_popup", 220.0f ) )
				{
					xui::slider_float( "近处模糊", scene.dof_near_blurry, 0.0f, 50.0f, "%.0f" );
					xui::slider_float( "近处清晰", scene.dof_near_crisp, 0.0f, 100.0f, "%.0f" );
					xui::slider_float( "远处清晰", scene.dof_far_crisp, 100.0f, 2000.0f, "%.0f" );
					xui::slider_float( "远处模糊", scene.dof_far_blurry, 200.0f, 5000.0f, "%.0f" );
					xui::end_popup( );
				}

				xui::end_child( );
			}
		}

		if ( subtab == 2 )
		{
			auto& weather = w.m_weather;

			if ( xui::begin_child( "##world_weather", col_w ) )
			{
				xui::checkbox( "天气", weather.enabled );
				if ( xui::begin_popup( "##weather_popup", 220.0f ) )
				{
					constexpr const char* weather_types[ ]{ "雪", "雨", "星星" };
					xui::combo( "类型", weather.type.value, weather_types, 3 );

					xui::color_picker( "颜色##weather", weather.color );
					xui::end_popup( );
				}

				xui::checkbox( "雾", weather.fog_enabled );
				if ( xui::begin_popup( "##fog_popup", 220.0f ) )
				{
					xui::slider_float( "浓度##fog", weather.fog_density, 0.0f, 1.0f, "%.2f" );
					xui::slider_float( "各向异性", weather.fog_anisotropy, 0.0f, 1.0f, "%.2f" );
					xui::slider_float( "绘制距离", weather.fog_draw_distance, 500.0f, 20000.0f, "%.0f" );
					xui::color_picker( "颜色##fog", weather.fog_color );
					xui::end_popup( );
				}

				xui::checkbox( "湿润", weather.wetness );
				if ( xui::begin_popup( "##wetness_popup", 220.0f ) )
				{
					xui::slider_float( "浓度##wet", weather.wetness_density, 0.0f, 5.0f, "%.1f" );
					xui::slider_float( "速度##wet", weather.wetness_speed, 0.0f, 3.0f, "%.1f" );
					xui::end_popup( );
				}

				xui::checkbox( "风", weather.wind );
				if ( xui::begin_popup( "##wind_popup", 220.0f ) )
				{
					xui::slider_float( "强度##wind", weather.wind_strength, 0.0f, 5.0f, "%.1f" );
					xui::slider_float( "方向##wind", weather.wind_direction, 0.0f, 360.0f, "%.0f" );
					xui::slider_float( "湍流##wind", weather.wind_turbulence, 0.0f, 5.0f, "%.1f" );
					xui::end_popup( );
				}

				xui::end_child( );
			}
		}
	}

} // namespace rendering
