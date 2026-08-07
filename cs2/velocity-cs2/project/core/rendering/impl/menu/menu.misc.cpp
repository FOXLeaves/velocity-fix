#include <pch/pch.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* sound_types[ ]{ "商店点击", "主页点击", "铃声", "击杀卡片", "弹道", "金币拾取", "物品掉落", "易拉罐", "按键音", "自定义" };
		constexpr auto k_sound_type_count{ static_cast< int >( std::size( sound_types ) ) };

		void draw_custom_sound_picker( config::str& file_setting, std::string_view combo_label, std::string_view preview_id, float preview_volume )
		{
			const auto files = features::misc::impacts::list_custom_sounds( );

			static std::vector<std::string> cached_files{};
			static std::vector<const char*> cached_ptrs{};
			cached_files = files;
			cached_ptrs.clear( );
			cached_ptrs.reserve( cached_files.size( ) );

			for ( const auto& file : cached_files )
			{
				cached_ptrs.push_back( file.c_str( ) );
			}

			if ( !cached_ptrs.empty( ) )
			{
				auto selected{ 0 };
				for ( auto i = 0; i < static_cast< int >( cached_files.size( ) ); ++i )
				{
					if ( cached_files[ static_cast< std::size_t >( i ) ] == file_setting.value )
					{
						selected = i;
						break;
					}
				}

				if ( xui::combo( combo_label, selected, cached_ptrs.data( ), static_cast< int >( cached_ptrs.size( ) ) ) )
				{
					file_setting = cached_files[ static_cast< std::size_t >( selected ) ];
				}
			}

			xui::text_input( "声音文件", file_setting.value, 64, "hit.wav" );

			if ( xui::button( preview_id, 96.0f, 22.0f ) )
			{
				features::misc::g_impacts.play_custom_sound( file_setting.value, preview_volume );
			}
		}
		constexpr const char* marker_types[ ]{ "经典", "伤害", "两者都" };
		constexpr const char* impact_types[ ]{ "叠加效果", "火花", "两者都" };

		constexpr const char* primary_weapons[ ]{ "关闭", "步枪", "瞄准步枪", "鸟狙", "大狙", "自动狙击" };
		constexpr const char* secondary_weapons[ ]{ "关闭", "双持贝瑞塔", "五七/tec-9", "沙鹰", "左轮" };
		constexpr const char* grenade_names[ ]{ "莫洛托夫", "高爆手雷", "烟雾弹", "闪光弹", "诱饵弹" };

		constexpr const char* hat_types[ ]{ "水桶", "天使光环" };

	} // namespace detail

	void menu::draw_misc( float group_w ) const
	{
		auto& m = settings::g_misc;

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
			auto& impacts = m.m_impacts;
			auto& traj = m.m_projectile_trajectory;
			auto& dlights = m.m_dlight;
			auto& pen = settings::g_combat.m_penetration_crosshair;
			auto& mov = settings::g_movement;
			auto& ab = m.m_autobuy;

			if ( xui::begin_child( "##misc_impacts", col_w ) )
			{
				xui::checkbox( "命中日志", impacts.hit_log );
				if ( xui::begin_popup( "##hitlog_popup", 220.0f ) )
				{
					xui::slider_float( "持续时间##hl", impacts.hit_log_duration, 0.5f, 10.0f, "%.1fs" );
					xui::end_popup( );
				}

				xui::checkbox( "控制台日志", impacts.console_log );
				xui::checkbox( "聊天日志", impacts.chat_log );

				xui::checkbox( "未命中日志", impacts.miss_log );
				if ( xui::begin_popup( "##misslog_popup", 220.0f ) )
				{
					xui::slider_float( "持续时间##ml", impacts.miss_log_duration, 0.5f, 10.0f, "%.1fs" );
					xui::end_popup( );
				}

				xui::checkbox( "命中音效", impacts.hit_sound );
				if ( xui::begin_popup( "##hitsound_popup", 220.0f ) )
				{
					xui::combo( "音效##hs", impacts.hit_sound_type.value, detail::sound_types, detail::k_sound_type_count );

					xui::slider_float( "音量##hs", impacts.hit_sound_volume, 1.0f, 100.0f, "%.0f%%" );

					if ( impacts.hit_sound_type.value == settings::misc::impacts::sound_type::custom )
					{
						detail::draw_custom_sound_picker( impacts.custom_hit_sound, "声音##hs", "声音##hs", impacts.hit_sound_volume.value );
					}

					xui::end_popup( );
				}

				xui::checkbox( "命中标记", impacts.hit_marker );
				if ( xui::begin_popup( "##hitmarker_popup", 220.0f ) )
				{
					xui::combo( "类型##hm", impacts.hit_marker_type.value, detail::marker_types, 3 );

					xui::slider_float( "持续时间##hm", impacts.hit_marker_duration, 0.1f, 5.0f, "%.1fs" );
					xui::color_picker( "颜色##hm", impacts.hit_marker_color );
					xui::end_popup( );
				}

				xui::checkbox( "命中特效", impacts.hit_effect );
				if ( xui::begin_popup( "##hitfx_popup", 220.0f ) )
				{
					xui::color_picker( "颜色##hitfx", impacts.hit_effect_color );
					xui::slider_float( "持续时间##hitfx", impacts.hit_effect_duration, 0.1f, 5.0f, "%.1fs" );
					xui::slider_float( "强度##hitfx", impacts.hit_effect_strength, 1.0f, 100.0f, "%.0f%%" );
					xui::end_popup( );
				}

				xui::checkbox( "死亡音效", impacts.death_sound );
				if ( xui::begin_popup( "##deathsound_popup", 220.0f ) )
				{
					xui::combo( "音效##ds", impacts.death_sound_type.value, detail::sound_types, detail::k_sound_type_count );

					xui::slider_float( "音量##ds", impacts.death_sound_volume, 1.0f, 100.0f, "%.0f%%" );

					if ( impacts.death_sound_type.value == settings::misc::impacts::sound_type::custom )
					{
						detail::draw_custom_sound_picker( impacts.custom_death_sound, "声音##ds", "声音##ds", impacts.death_sound_volume.value );
					}

					xui::end_popup( );
				}

				xui::checkbox( "死亡特效", impacts.death_effect );
				if ( xui::begin_popup( "##deathfx_popup", 220.0f ) )
				{
					xui::color_picker( "颜色##deathfx", impacts.death_effect_color );
					xui::end_popup( );
				}

				xui::checkbox ("记分板武器", m.m_scoreboard_weapons.enabled);
				if (xui::begin_popup ("##scoreboardeq_popup", 220.0f)) {
					xui::color_picker ("颜色##scoreboardeq", m.m_scoreboard_weapons.color);
					xui::end_popup ();
				}

				xui::checkbox( "弹道", impacts.bullet_impact_effect );
				if ( xui::begin_popup( "##bulletfx_popup", 220.0f ) )
				{
					xui::combo( "类型##bulletfx", impacts.bullet_impact_effect_type.value, detail::impact_types, 3 );

					const auto type = impacts.bullet_impact_effect_type.value;
					const auto show_overlay = type == settings::misc::impacts::bullet_impact_type::overlay || type == settings::misc::impacts::bullet_impact_type::both;
					const auto show_sparks = type == settings::misc::impacts::bullet_impact_type::sparks || type == settings::misc::impacts::bullet_impact_type::both;

					if ( show_overlay )
					{
						xui::slider_float( "持续时间##bulletfx", impacts.bullet_impact_effect_duration, 0.1f, 5.0f, "%.1fs" );
						xui::color_picker( "填充##bulletfx", impacts.bullet_impact_effect_fill_color );
						xui::color_picker( "描边##bulletfx", impacts.bullet_impact_effect_edge_color );

						xui::checkbox( "发光##bulletfx", impacts.bullet_impact_effect_glow );
						if ( impacts.bullet_impact_effect_glow )
						{
							xui::slider_float( "发光强度##bulletfx", impacts.bullet_impact_effect_glow_strength, 0.1f, 1.0f, "%.2f" );
						}
					}

					if ( show_sparks )
					{
						xui::color_picker( "火花##bulletfx", impacts.bullet_impact_effect_color_spark );
					}

					xui::end_popup( );
				}

				xui::checkbox( "子弹曳光", impacts.bullet_tracers );
				if ( xui::begin_popup( "##tracers_popup", 220.0f ) )
				{
					xui::slider_float( "持续时间##tracer", impacts.bullet_tracer_duration, 0.1f, 5.0f, "%.1fs" );
					xui::color_picker( "颜色##tracer", impacts.bullet_tracer_color );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			if ( xui::begin_child( "##misc_visuals", col_w ) )
			{
				xui::checkbox( "投掷物轨迹", traj.enabled );
				if ( xui::begin_popup( "##traj_popup", 220.0f ) )
				{
					xui::checkbox( "直线投掷", traj.straight_throw );
					xui::color_picker( "持握颜色", traj.held_color );
					xui::color_picker( "投掷颜色", traj.thrown_color );
					xui::color_picker( "将造成伤害(持握)颜色", traj.will_deal_damage_held_color );
					xui::color_picker( "将造成伤害(投掷)颜色", traj.will_deal_damage_thrown_color );
					xui::end_popup( );
				}

				xui::checkbox( "动态光源", dlights.enabled );
				if ( xui::begin_popup( "##dlight_popup", 220.0f ) )
				{
					xui::color_picker( "颜色##dl", dlights.color );
					xui::slider_float( "半径##dl", dlights.radius, 50.0f, 15000.0f, "%.0f" );
					xui::slider_float( "Z轴偏移#dl", dlights.z_offset, 0.0f, 100.0f, "%.0f" );
					xui::end_popup( );
				}

				xui::checkbox( "穿墙准星", pen.enabled );
				if ( xui::begin_popup( "##pen_popup", 220.0f ) )
				{
					xui::checkbox( "发光##pen", pen.glow );
					xui::slider_float( "发光强度##pen", pen.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::color_picker( "穿透颜色#pen", pen.can_penetrate_fill );
					xui::color_picker( "穿透描边颜色#pen", pen.can_penetrate_outline );
					xui::color_picker( "遮挡颜色#pen", pen.blocked_fill );
					xui::color_picker( "遮挡描边颜色#pen", pen.blocked_outline );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			xui::layout::set_cursor( right_x - wx, body_y - wy );

			if ( xui::begin_child( "##misc_movement", col_w ) )
			{
				xui::checkbox( "连跳", mov.bhop );
				xui::checkbox( "空中加速", mov.airstrafe );
				xui::checkbox( "测试加速", mov.m_test_strafer.enabled );

				if ( xui::begin_popup( "##airstrafe_popup", 220.0f ) )
				{
					xui::checkbox( "全向加速", mov.airstrafe_fully_directional );
					xui::checkbox( "加速调试", mov.m_strafe_debug );
					xui::end_popup( );
				}

				xui::checkbox( "跳bug", mov.jumpbug );
				xui::checkbox( "快速爬梯", mov.fastladder );
				xui::checkbox( "边缘跳", mov.edgejump );
				xui::checkbox( "边缘停", mov.edgestop );
				xui::checkbox( "边缘bug", mov.edgebug );
				if ( xui::begin_popup( "##edgebug_popup", 240.0f ) )
				{
					static const char* edgebug_modes[] = { "0: 总是", "1: 空中跳跃 (按住)", "2: 空中松开跳跃", "3: 快速移动", "4: 快速vz" };
					xui::combo( "模式##eb", mov.edgebug_mode.value, edgebug_modes, 5 );
					xui::slider_int( "次数##eb", mov.edgebug_passes, 1, 5, "%d" );
					xui::checkbox( "跳跃步骤##eb", mov.edgebug_include_jump_steps );
					xui::end_popup( );
				}
				xui::checkbox( "慢走", mov.slowwalk );

				if ( xui::begin_popup( "##slowwalk_popup", 220.0f ) )
				{
					xui::slider_float( "速度", mov.slowwalk_speed, 1.0f, 100.0f, "%.2fs" );
					xui::end_popup( );
				}
				xui::checkbox( "小跳", mov.mini_jump );

				xui::end_child( );
			}

		if ( xui::begin_child( "##misc_other", col_w ) )
		{
			xui::checkbox( "自动接受比赛", m.auto_accept );
			xui::checkbox( "显示雷达", m.reveal_radar );
				xui::checkbox( "保留击杀信息", m.preserve_killfeed );
				xui::checkbox( "禁用游戏日志", m.disable_game_logs );

				xui::checkbox( "自动购买", ab.enabled );
				if ( xui::begin_popup( "##autobuy_popup", 220.0f ) )
				{
					xui::combo( "主武器#ab", ab.primary_weapon, detail::primary_weapons, 6 );
					xui::combo( "副武器#ab", ab.secondary_weapon, detail::secondary_weapons, 5 );
					xui::checkbox( "护甲##ab", ab.armor );
					xui::checkbox( "拆弹器#ab", ab.defuser );
					xui::checkbox( "电击枪#ab", ab.taser );
					xui::multicombo( "手雷##ab", ab.grenades, detail::grenade_names, 5 );
					xui::end_popup( );
				}

				xui::checkbox( "队标", m.m_name_changer.clantag );
				xui::checkbox( "改名", m.m_name_changer.override_name );
				if ( xui::begin_popup( "##override_name_popup", 220.0f ) )
				{
					xui::text_input( "名字##nc", m.m_name_changer.name.value, 32, "player name..." );
					xui::end_popup( );
				}

				xui::checkbox( "水印", m.m_watermark.enabled );
				if ( xui::begin_popup( "##watermark_popup", 200.0f ) )
				{
					xui::checkbox( "帧数##wm",      m.m_watermark.show_fps );
					xui::checkbox( "延迟##wm",     m.m_watermark.show_ping );
					xui::checkbox( "时间##wm",     m.m_watermark.show_time );
					xui::checkbox( "玩家名字#wm",     m.m_watermark.show_user );
					xui::checkbox( "地图##wm",      m.m_watermark.show_map );
					xui::checkbox( "服务器刷新率##wm",m.m_watermark.show_tick );
					xui::checkbox( "速度##wm", m.m_watermark.show_velocity );
					xui::end_popup( );
				}

				xui::end_child( );
			}
		}

		if ( subtab == 1 )
		{
			auto& rem = m.m_removals;

			if ( xui::begin_child( "##misc_removals", col_w ) )
			{
				xui::checkbox( "移除准星", rem.crosshair );
				xui::checkbox( "移除瞄准镜画面", rem.scope );
				xui::checkbox( "移除头顶信息", rem.overhead );
				xui::checkbox( "移除腿部", rem.legs );
				xui::checkbox( "移除后坐力", rem.recoil );
				xui::checkbox( "移除天空盒雾气", rem.skybox_fog );
				xui::checkbox( "移除3D天空盒", rem.skybox_3d );
				xui::checkbox( "移除贴花", rem.decals );
				xui::checkbox( "移除烟雾", rem.smoke );
				xui::slider_float( "闪光弹透明度#flash", rem.flash_alpha, 0.0f, 100.0f, "%.0f%%" );

				xui::end_child( );
			}
		}

		if ( subtab == 2 )
		{
			auto& cam = m.m_camera;
			auto& vm = m.m_viewmodel_adjust;

			if ( xui::begin_child( "##misc_camera", col_w ) )
			{
				xui::checkbox( "自定义视角", cam.change_fov );
				if ( xui::begin_popup( "##fov_popup", 220.0f ) )
				{
					xui::slider_float( "视角", cam.fov, 60.0f, 150.0f, "%.0f" );
					xui::checkbox( "开镜时覆盖视角", cam.scoped_fov_override );
					if ( cam.scoped_fov_override.value )
					{
						xui::slider_float( "单次开镜缩放", cam.scoped_fov, 10.0f, 90.0f, "%.0f" );
						xui::slider_float( "二次开镜缩放", cam.scoped_fov_2, 10.0f, 90.0f, "%.0f" );
					}
					xui::end_popup( );
				}

				xui::checkbox( "第三人称", cam.thirdperson );
				if ( xui::begin_popup( "##tp_popup", 220.0f ) )
				{
					xui::slider_float( "距离", cam.thirdperson_distance, 35.0f, 200.0f, "%.0f" );
					xui::slider_float( "碰撞体积大小", cam.thirdperson_hull_size, 0.0f, 20.0f, "%.0f" );
					xui::end_popup( );
				}

				xui::checkbox( "宽高比例", cam.change_aspect_ratio );
				if ( xui::begin_popup( "##ar_popup", 220.0f ) )
				{
					xui::slider_float( "比例##ar", cam.aspect_ratio, 1.0f, 1.78f, "%.3f" );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			xui::layout::set_cursor( right_x - wx, body_y - wy );

			if ( xui::begin_child( "##misc_viewmodel", col_w ) )
			{
				xui::checkbox( "持枪视角调整", vm.enabled );
				if ( xui::begin_popup( "##vm_popup", 220.0f ) )
				{
					xui::slider_float( "X偏移", vm.offset_x, -10.0f, 10.0f, "%.1f" );
					xui::slider_float( "Y偏移", vm.offset_y, -10.0f, 10.0f, "%.1f" );
					xui::slider_float( "Z偏移", vm.offset_z, -10.0f, 10.0f, "%.1f" );
					xui::slider_float( "视角", vm.fov, 54.0f, 90.0f, "%.0f" );
					xui::end_popup( );
				}

				xui::end_child( );
			}
		}

		if ( subtab == 3 )
		{
			auto& hud = m.m_hud;

			if ( xui::begin_child( "##misc_hud", col_w ) )
			{
				xui::checkbox( "准星叠加图层", hud.m_crosshair.enabled );
				if ( xui::begin_popup( "##xhair_popup", 220.0f ) )
				{
					xui::slider_float( "大小##xhair", hud.m_crosshair.size, 0.5f, 10.0f, "%.1f" );
					xui::slider_float( "描边##xhair", hud.m_crosshair.outline, 0.0f, 4.0f, "%.1f" );
					xui::color_picker( "颜色##xhair", hud.m_crosshair.color );
					xui::color_picker( "描边颜色##xhair", hud.m_crosshair.outline_color );
					xui::end_popup( );
				}

				xui::checkbox( "开镜叠加层", hud.m_scope.enabled );
				if ( xui::begin_popup( "##scope_popup", 220.0f ) )
				{
					xui::slider_float( "线条长度", hud.m_scope.line_length, 10.0f, 500.0f, "%.0f" );
					xui::slider_float( "间距##scope", hud.m_scope.gap, 0.0f, 50.0f, "%.0f" );
					xui::slider_float( "粗细##scope", hud.m_scope.thickness, 0.5f, 5.0f, "%.2f" );
					xui::slider_float( "动画速度", hud.m_scope.anim_speed, 1.0f, 30.0f, "%.0f" );
					xui::color_picker( "颜色##scope", hud.m_scope.color );
					xui::checkbox( "淡入##scope", hud.m_scope.fade_in );

					xui::layout::separator( );

					xui::checkbox( "发光##scope", hud.m_scope.glow );
					xui::slider_float( "发光强度##scope", hud.m_scope.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				xui::checkbox( "速度计数器", hud.m_velocity.counter );
				xui::checkbox( "速度图表", hud.m_velocity.chart );
				if ( xui::begin_popup( "##velocity_hud_popup", 220.0f ) )
				{
					xui::color_picker( "颜色##velocity", hud.m_velocity.color );
					xui::slider_float( "底部偏移", hud.m_velocity.bottom_offset, 20.0f, 200.0f, "%.0f" );
					xui::slider_float( "图表宽度", hud.m_velocity.chart_width, 120.0f, 320.0f, "%.0f" );
					xui::slider_float( "图表高度", hud.m_velocity.chart_height, 24.0f, 80.0f, "%.0f" );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			xui::layout::set_cursor( right_x - wx, body_y - wy );

			if ( xui::begin_child( "##misc_hud_hat", col_w ) )
			{
				xui::checkbox( "帽子", hud.m_hat.enabled );
				if ( xui::begin_popup( "##hat_popup", 220.0f ) )
				{
					xui::combo( "类型##hat", hud.m_hat.type.value, detail::hat_types, 2 );
					xui::color_picker( "颜色##hat", hud.m_hat.color );
					xui::color_picker( "次要颜色##hat", hud.m_hat.secondary_color );
					xui::checkbox( "发光##hat", hud.m_hat.glow );
					xui::slider_float( "发光强度##hat", hud.m_hat.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				xui::end_child( );
			}
		}
	}

} // namespace rendering