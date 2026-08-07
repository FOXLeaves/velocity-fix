#include <pch/pch.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* k_cham_material_names[ ]{
			"液态", "金属", "哑光", "平涂", "泛光", "描边", "发光", "电流", "扭曲", "全息", "珍珠",
			"液态 (iz)", "哑光 (iz)", "平涂 (iz)", "泛光 (iz)", "描边 (iz)", "发光 (iz)", "扭曲 (iz)", "全息 (iz)"
		};
		constexpr auto k_cham_material_count = static_cast< int >( settings::esp::cham_ids::count );

		inline static void draw_chams_layer( const char* label, const char* popup_id, settings::esp::chams_layer& layer )
		{
			xui::checkbox( label, layer.enabled );
			if ( xui::begin_popup( popup_id, 220.0f ) )
			{
				xui::combo( "材质", layer.material.value, k_cham_material_names, k_cham_material_count );
				xui::color_picker( "颜色", layer.color );
				xui::end_popup( );
			}
		}

		inline static void draw_chams_config( const char* label, const char* id_suffix, settings::esp::chams_config& cfg, bool show_overlay = true )
		{
			xui::checkbox( label, cfg.enabled );

			char label_buf[ 64 ]{};
			char popup_id[ 64 ]{};

			std::snprintf( label_buf, sizeof( label_buf ), "主图层##%s", id_suffix );
			std::snprintf( popup_id, sizeof( popup_id ), "##primary_%s", id_suffix );
			draw_chams_layer( label_buf, popup_id, cfg.primary );

			std::snprintf( label_buf, sizeof( label_buf ), "副图层##%s", id_suffix );
			std::snprintf( popup_id, sizeof( popup_id ), "##secondary_%s", id_suffix );
			draw_chams_layer( label_buf, popup_id, cfg.secondary );

			if ( show_overlay )
			{
				std::snprintf( label_buf, sizeof( label_buf ), "叠加图层##%s", id_suffix );
				std::snprintf( popup_id, sizeof( popup_id ), "##overlay_%s", id_suffix );
				draw_chams_layer( label_buf, popup_id, cfg.overlay );
			}
		}

	} // namespace detail

	void menu::draw_player( float group_w ) const
	{
		auto& esp = settings::g_esp;
		auto& p = esp.m_player;

		const auto col_w = ( this->m_body_w - tokens::gap ) * 0.5f;
		const auto subtab = this->m_subtab;
		const auto has_overlay = ( subtab <= 1 );

		auto& glow = (subtab == 0) ? p.m_glow.enemy : p.m_glow.team;
		auto& glow_ragdoll = (subtab == 0) ? p.m_glow.enemy_ragdoll : p.m_glow.team_ragdoll;

		xui::layout::set_cursor( this->m_body_x - this->m_x, this->m_body_y - this->m_y );

		if ( has_overlay )
		{
			auto& ov = p.m_overlay[ subtab ];

			if ( xui::begin_child( "##player_esp", col_w ) )
			{
				xui::checkbox( "透视覆盖层", ov.enabled );

				xui::checkbox( "方框", ov.m_box.enabled );
				if ( xui::begin_popup( "##box_popup", 220.0f ) )
				{
					constexpr const char* box_styles[ ]{ "完整", "四角" };
					xui::combo( "样式##box", ov.m_box.style.value, box_styles, 2 );

					xui::checkbox( "填充", ov.m_box.fill );
					xui::checkbox( "描边", ov.m_box.outline );
					xui::slider_float( "转角长度", ov.m_box.corner_length, 2.0f, 20.0f, "%.0f" );
					xui::color_picker( "可见颜色##box", ov.m_box.visible_color );
					xui::color_picker( "遮挡颜色##box", ov.m_box.occluded_color );
					xui::end_popup( );
				}

				xui::checkbox( "骨骼", ov.m_skeleton.enabled );
				if ( xui::begin_popup( "##skeleton_popup", 220.0f ) )
				{
					constexpr const char* skel_modes[ ]{ "正常", "回溯" };
					xui::combo( "模式##skel", ov.m_skeleton.type.value, skel_modes, 2 );

					xui::slider_float( "粗细##skel", ov.m_skeleton.thickness, 0.5f, 4.0f, "%.1f" );
					xui::color_picker( "可见颜色##skel", ov.m_skeleton.visible_color );
					xui::color_picker( "遮挡颜色##skel", ov.m_skeleton.occluded_color );
					xui::end_popup( );
				}

				xui::checkbox( "血条", ov.m_health_bar.enabled );
				if ( xui::begin_popup( "##health_popup", 220.0f ) )
				{
					constexpr const char* bar_positions[ ]{ "左侧", "顶部", "底部" };
					xui::combo( "位置##hp", ov.m_health_bar.position.value, bar_positions, 3 );

					xui::checkbox( "描边##hp", ov.m_health_bar.outline_setting );
					xui::checkbox( "渐变##hp", ov.m_health_bar.gradient );
					xui::checkbox( "显示数值##hp", ov.m_health_bar.show_value );
					xui::checkbox( "发光##hp", ov.m_health_bar.glow );
					xui::color_picker( "满血颜色##hp", ov.m_health_bar.full_color );
					xui::color_picker( "低血颜色##hp", ov.m_health_bar.low_color );
					xui::color_picker( "背景##hp", ov.m_health_bar.background_color );
					xui::color_picker( "描边颜色##hp", ov.m_health_bar.outline_color );
					xui::color_picker( "文本颜色##hp", ov.m_health_bar.text_color );
					xui::color_picker( "发光颜色##hp", ov.m_health_bar.glow_color );
					xui::slider_float( "发光强度##hp", ov.m_health_bar.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				xui::checkbox( "弹药条", ov.m_ammo_bar.enabled );
				if ( xui::begin_popup( "##ammo_popup", 220.0f ) )
				{
					constexpr const char* bar_positions[ ]{ "左侧", "顶部", "底部" };
					xui::combo( "位置##ammo", ov.m_ammo_bar.position.value, bar_positions, 3 );

					xui::checkbox( "描边##ammo", ov.m_ammo_bar.outline_setting );
					xui::checkbox( "渐变##ammo", ov.m_ammo_bar.gradient );
					xui::checkbox( "显示数值##ammo", ov.m_ammo_bar.show_value );
					xui::checkbox( "发光##ammo", ov.m_ammo_bar.glow );
					xui::color_picker( "满弹颜色##ammo", ov.m_ammo_bar.full_color );
					xui::color_picker( "低弹颜色##ammo", ov.m_ammo_bar.low_color );
					xui::color_picker( "背景##ammo", ov.m_ammo_bar.background_color );
					xui::color_picker( "描边颜色##ammo", ov.m_ammo_bar.outline_color );
					xui::color_picker( "文本颜色##ammo", ov.m_ammo_bar.text_color );
					xui::color_picker( "发光颜色##ammo", ov.m_ammo_bar.glow_color );
					xui::slider_float( "发光强度##ammo", ov.m_ammo_bar.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				xui::checkbox( "名字", ov.m_name.enabled );
				if ( xui::begin_popup( "##name_popup", 220.0f ) )
				{
					xui::color_picker( "颜色##name", ov.m_name.color );
					xui::end_popup( );
				}

				xui::checkbox( "武器", ov.m_weapon.enabled );
				if ( xui::begin_popup( "##weapon_popup", 220.0f ) )
				{
					constexpr const char* display_types[ ]{ "文本", "图标", "文本 + 图标" };
					xui::combo( "显示##wep", ov.m_weapon.display.value, display_types, 3 );

					xui::color_picker( "文本颜色##wep", ov.m_weapon.text_color );
					xui::color_picker( "图标颜色##wep", ov.m_weapon.icon_color );
					xui::end_popup( );
				}

				xui::checkbox( "信息标志", ov.m_info_flags.enabled );
				if ( xui::begin_popup( "##flags_popup", 220.0f ) )
				{
					constexpr const char* flag_names[ ]{ "金钱", "护甲", "拆弹器", "开镜", "拆弹中", "被闪", "延迟", "距离" };
					xui::multicombo( "标志##mc", ov.m_info_flags.flags, flag_names, settings::esp::player::overlay::info_flags::count );

					xui::color_picker( "金钱##flags", ov.m_info_flags.money_color );
					xui::color_picker( "护甲##flags", ov.m_info_flags.armor_color );
					xui::color_picker( "拆弹器##flags", ov.m_info_flags.kit_color );
					xui::color_picker( "开镜##flags", ov.m_info_flags.scoped_color );
					xui::color_picker( "拆弹中##flags", ov.m_info_flags.defusing_color );
					xui::color_picker( "被闪##flags", ov.m_info_flags.flashed_color );
					xui::color_picker( "距离##flags", ov.m_info_flags.distance_color );
					xui::end_popup( );
				}

				xui::checkbox( "屏幕外箭头", ov.m_oof_arrow.enabled );
				if ( xui::begin_popup( "##oof_popup", 220.0f ) )
				{
					xui::checkbox( "发光##oof", ov.m_oof_arrow.glow );
					xui::slider_float( "宽度##oof", ov.m_oof_arrow.width, 4.0f, 40.0f, "%.0f" );
					xui::slider_float( "高度##oof", ov.m_oof_arrow.height, 4.0f, 40.0f, "%.0f" );
					xui::slider_float( "半径X##oof", ov.m_oof_arrow.radius_x, 50.0f, 600.0f, "%.0f" );
					xui::slider_float( "半径Y##oof", ov.m_oof_arrow.radius_y, 50.0f, 600.0f, "%.0f" );
					xui::slider_float( "发光强度##oof", ov.m_oof_arrow.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::color_picker( "可见颜色##oof", ov.m_oof_arrow.visible_color );
					xui::color_picker( "遮挡颜色##oof", ov.m_oof_arrow.occluded_color );
					xui::end_popup( );
				}

				// Ragebot diagnostics on the enemy body.
				xui::checkbox( "回溯显示", p.m_backtrack_display.enabled );
				if ( xui::begin_popup( "##bt_popup", 220.0f ) )
				{
					xui::color_picker( "颜色##bt", p.m_backtrack_display.color );
					xui::end_popup( );
				}

				xui::checkbox( "外推显示", p.m_extrapolation_display.enabled );
				if ( xui::begin_popup( "##extpopup2", 220.0f ) )
				{
					xui::color_picker( "颜色##ext2", p.m_extrapolation_display.color );
					xui::end_popup( );
				}


				xui::end_child ();

				if (xui::begin_child ("##player_glow", col_w)) {
					xui::checkbox ("发光", glow.enabled);
					if (xui::begin_popup ("##glow_popup", 220.0f)) {
						xui::color_picker ("颜色##glow", glow.color);
						xui::end_popup ();
					}

					xui::checkbox ("尸体发光", glow_ragdoll.enabled);
					if (xui::begin_popup ("##glow_rag_popup", 220.0f)) {
						xui::color_picker ("颜色##glow_rag", glow_ragdoll.color);
						xui::end_popup ();
					}

					xui::end_child ();
				}


			}
		}
		else
		{
			if ( xui::begin_child( "##local_chams_glow", col_w ) )
			{
				detail::draw_chams_config( "变色", "local_main", p.m_chams.local );

				xui::layout::separator( );

				xui::checkbox( "降低透明度", esp.m_local_alpha.enabled );
				if ( xui::begin_popup( "##local_alpha_popup", 220.0f ) )
				{
					xui::slider_float( "透明度", esp.m_local_alpha.opacity, 0.0f, 1.0f, "%.2f" );
					xui::checkbox( "仅开镜时", esp.m_local_alpha.only_scoped );
					xui::end_popup( );
				}

				xui::layout::separator( );

				detail::draw_chams_config( "尸体变色", "local_ragdoll", p.m_chams.local_ragdoll, false );

				xui::layout::separator( );

				xui::checkbox( "发光", p.m_glow.local.enabled );
				if ( xui::begin_popup( "##local_glow_popup", 220.0f ) )
				{
					xui::color_picker( "颜色##local_glow", p.m_glow.local.color );
					xui::end_popup( );
				}

				xui::checkbox( "尸体发光", p.m_glow.local_ragdoll.enabled );
				if ( xui::begin_popup( "##local_glow_rag_popup", 220.0f ) )
				{
					xui::color_picker( "颜色##local_glow_rag", p.m_glow.local_ragdoll.color );
					xui::end_popup( );
				}

				xui::end_child( );
			}
		}

		xui::layout::set_cursor( this->m_body_x - this->m_x + col_w + tokens::gap, this->m_body_y - this->m_y );

		if ( has_overlay )
		{
			auto& chams = ( subtab == 0 ) ? p.m_chams.enemy : p.m_chams.team;
			auto& chams_ragdoll = ( subtab == 0 ) ? p.m_chams.enemy_ragdoll : p.m_chams.team_ragdoll;
			auto& glow = ( subtab == 0 ) ? p.m_glow.enemy : p.m_glow.team;
			auto& glow_ragdoll = ( subtab == 0 ) ? p.m_glow.enemy_ragdoll : p.m_glow.team_ragdoll;

			if ( xui::begin_child( "##player_chams", col_w ) )
			{
				detail::draw_chams_config( "变色", "main", chams );

				xui::layout::separator( );

				detail::draw_chams_config( "尸体变色", "ragdoll", chams_ragdoll, false );

				if ( subtab == 0 )
				{
					xui::layout::separator( );

					detail::draw_chams_config( "回溯变色", "bt", p.m_chams.backtrack, false );
/*					xui::layout::separator (); not enough menu space with this*/
					detail::draw_chams_config ("射击变色", "os", p.m_chams.onshot, false);

					xui::slider_float ("淡出##ft", p.m_chams.onshot_fade_time, 0.1f, 5.0f, "%.0f");

				}

				xui::end_child( );
			}

	}
	else
		{
			if ( xui::begin_child( "##viewmodel", col_w ) )
			{
				detail::draw_chams_config( "武器变色", "vm_weapon", esp.m_viewmodel.weapon );

				xui::layout::separator( );

				detail::draw_chams_config( "手臂变色", "vm_arms", esp.m_viewmodel.arms );

				xui::end_child( );
			}
		}
	}

} // namespace rendering