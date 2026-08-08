#include <pch/pch.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* hitbox_names[ ]{ "头部", "胸部", "腹部", "手臂", "腿部", "爪子" };
	constexpr const char* pitch_items[ ]{ "无", "向下", "向上", "自定义" };
	constexpr const char* aa_yaw_items[ ]{ "背向", "真实视角", "自定义" };
	constexpr const char* jitter_items[ ]{ "无", "中心", "边缘", "全身", "三向", "旋转" };

	} // namespace detail

	void menu::draw_ragebot( float group_w ) const
	{
		auto& s = settings::g_combat;
		auto& rb = s.m_ragebot;
		auto& aa = s.m_antiaim;
		auto& qp = s.m_quickpeek;
		auto& dp = s.m_duckpeek;
		auto& zb = s.m_zeusbot;
		auto& kb = s.m_knifebot;
		auto& autos = s.m_autos;
		auto& lg = s.m_lagcomp;

		auto& wg = rb.groups[ this->m_subtab ];

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap + tokens::sidebar_w + tokens::gap;
		const auto body_y = wy + tokens::gap + tokens::subtab_bar_h + tokens::gap;
		const auto content_w = this->m_w - tokens::gap * 2.0f - tokens::sidebar_w - tokens::gap;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		xui::layout::set_cursor( content_x - wx, body_y - wy );

		if ( xui::begin_child( "##ragebot_aimbot", col_w ) )
		{
			xui::checkbox( "启用", rb.enabled );
			xui::checkbox( "vac绕过", rb.vac_bypass );
			xui::checkbox( "双击 (DT)", rb.m_double_tap.enabled );
			if ( xui::begin_popup( "##dt_popup", 240.0f ) )
			{
				xui::checkbox( "充能预览##dt", rb.m_double_tap.preview );
				xui::checkbox( "快速拉栓##dt", rb.m_double_tap.quick_bolt );
				xui::color_picker( "充能中颜色##dt", rb.m_double_tap.charging_color );
				xui::color_picker( "充能完成颜色##dt", rb.m_double_tap.ready_color );
				xui::color_picker( "充能失败颜色##dt", rb.m_double_tap.failed_color );
				xui::end_popup( );
			}
			xui::checkbox( "自动开镜", s.m_autos.scope );
			xui::checkbox( "静默", wg.silent );
			static const char* no_spread_items[ ] = { "强制", "种子" };
			xui::checkbox( "无扩散", wg.no_spread );
			if ( xui::begin_popup( "##nospread_popup", 200.0f ) )
			{
				xui::combo( "模式##nospread", wg.no_spread_mode.value, no_spread_items, 2 );
				xui::end_popup( );
			}
			xui::checkbox( "空中强制开枪", wg.force_shot_air );
			if ( xui::begin_popup( "##forceair_popup", 200.0f ) )
			{
				xui::slider_int( "命中率##forceair", wg.force_hitchance_air, 0, 100, "%d%%" );
				xui::end_popup( );
			}
			xui::checkbox( "地面强制开枪", wg.force_shot );
			if ( xui::begin_popup( "##force_popup", 200.0f ) )
			{
				xui::slider_int( "命中率##force", wg.force_hitchance, 0, 100, "%d%%" );
				xui::end_popup( );
			}
			xui::checkbox( "外推", lg.extrapolation);
			if ( xui::begin_popup( "##extrapolation_popup", 230.0f ) )
			{
				xui::checkbox( "自动外推##ext", lg.extrapolation_auto );
				xui::slider_int( "外推tick##ext", lg.max_extrapolate_ticks, 1, 18, "%d" );
				xui::checkbox( "路径矫正##ext", lg.extrapolation_correct );
				xui::checkbox( "空中目标外推##ext", lg.extrapolation_air );
				xui::end_popup( );
			}
			xui::checkbox( "回溯", lg.backtrack );
			if ( xui::begin_popup( "##backtrack_popup", 230.0f ) )
			{
				xui::slider_int( "最高回溯##bt", lg.max_backtrack_ticks, 1, 12, "%d tick" );
				xui::end_popup( );
			}
			xui::slider_float( "最大视角", wg.max_fov, 1.0f, 180.0f, "%.0f°" );

			xui::slider_int( "命中率", wg.hitchance, 25, 100, "%d%%" );
			xui::slider_int( "最低伤害", wg.min_damage, 5, 101, wg.min_damage.value >= 101 ? "致命" : "%d" );
			/*xui::slider_int( "最大回溯", s.m_lagcomp.max_backtrack_ticks, 1, 13, "%d tick(s)" );*/

			xui::checkbox( "命中率覆盖", wg.hitchance_override );
			if ( xui::begin_popup( "##hitchance_popup", 220.0f ) )
			{
				xui::slider_int( "数值##hc", wg.hitchance_override_value, 0, 100, "%d%%" );
				xui::end_popup( );
			}

			xui::checkbox( "最低伤害覆盖", wg.min_damage_override );
			if ( xui::begin_popup( "##mindamage_popup", 220.0f ) )
			{
				xui::slider_int( "数值##md", wg.min_damage_override_value, 0, 130, "%d" );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		if ( xui::begin_child( "##ragebot_extras", col_w, 190.0f, true ) )
		{
			xui::checkbox( "强制身体瞄准", wg.body_aim );
		xui::checkbox( "动态点缩放", wg.dynamic_pointscale );
		xui::checkbox( "调试多点", wg.debug_multipoints );
		xui::slider_float( "点缩放", wg.pointscale, 0.0f, 100.0f, "%.0f%%" );
		xui::checkbox( "头部额外多点", wg.extra_head_points );
		if ( wg.extra_head_points.value )
		{
			xui::slider_int( "头部额外多点缩放", wg.extra_head_scale, 0, 100, "%d%%" );
		}
		xui::checkbox( "身体额外多点", wg.extra_body_points );
		if ( wg.extra_body_points.value )
		{
			xui::slider_int( "身体额外多点缩放", wg.extra_body_scale, 0, 100, "%d%%" );
		}
		xui::multicombo( "命中盒", wg.hitboxes, detail::hitbox_names, 6 );

			xui::end_child( );
		}

		xui::layout::set_cursor( right_x - wx, body_y - wy );

		if ( xui::begin_child( "##ragebot_antiaim", col_w ) )
		{
			xui::checkbox( "反瞄准", aa.enabled );

			xui::combo( "俯仰", aa.pitch.value, detail::pitch_items, 4 );
			xui::combo( "偏航", aa.yaw.value, detail::aa_yaw_items, 3 );

			if ( aa.pitch.value == settings::combat::antiaim::pitch_mode::custom )
			{
				xui::slider_float( "俯仰角度##aa_pitch", aa.pitch_value, -89.0f, 89.0f, "%.1f°" );
			}
			if ( aa.yaw.value == settings::combat::antiaim::yaw_mode::custom )
			{
				xui::slider_float( "身体偏移##aa_yaw", aa.yaw_offset, 0.0f, 360.0f, "%.0f°" );
			}
			xui::slider_float( "侧步角度##aa_side", aa.side_offset, 1.0f, 180.0f, "%.0f°" );
			if ( aa.auto_yaw_adjust.value )
			{
				xui::slider_float( "调整量##aa_adj", aa.auto_yaw_adjust_amount, 0.0f, 180.0f, "%.0f°" );
			}

			xui::checkbox( "身体抖动", aa.jitter );
			if ( aa.jitter.value )
			{
				xui::combo( "抖动模式##aa_jit", aa.jitter_mode.value, detail::jitter_items, 6 );
				xui::slider_float( "抖动幅度##aa_jit_amt", aa.jitter_amount, -180.0f, 180.0f, "%.0f°" );
			}

			xui::checkbox( "补偿横滚", aa.auto_yaw_adjust );
			xui::checkbox( "强制左", aa.manual_left );
			xui::checkbox( "强制右", aa.manual_right );
			xui::checkbox( "开枪隐藏", aa.hide_shots );
			xui::checkbox( "避免背刺", aa.avoid_backstab );
			xui::checkbox( "方向指示器", aa.direction_indicator );

			if ( xui::begin_popup( "##aa_indicator", 220.0f ) )
			{
				xui::color_picker( "颜色##aa_ind", aa.direction_indicator_color );
				xui::checkbox( "发光##aa_ind", aa.direction_indicator_glow );
				xui::slider_float( "发光强度##aa_ind", aa.direction_indicator_glow_strength, 0.1f, 1.0f, "%.2f" );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		if ( xui::begin_child( "##ragebot_otherbots", col_w ) )
		{
			xui::checkbox( "自动左轮", autos.revolver );
			xui::checkbox( "空中急停(shift)", autos.air_stop );

			xui::checkbox( "电击枪机器人", zb.enabled );
			if ( xui::begin_popup( "##zb_settings", 220.0f ) )
			{
				xui::slider_float( "最大视角##zb", zb.max_fov, 1, 180, "%.0f°" );
				xui::checkbox( "使用后丢弃##zb", zb.drop_after );
				xui::end_popup( );
			}

			xui::checkbox( "刀机器人", kb.enabled );
			if ( xui::begin_popup( "##kb_settings", 220.0f ) )
			{
				xui::slider_float( "最大视角##kb", kb.max_fov, 1, 180, "%.0f°" );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		if ( xui::begin_child( "##ragebot_peek", col_w ) )
		{
			xui::checkbox( "快速探身", qp.enabled );
			if ( xui::begin_popup( "##qp_colors", 220.0f ) )
			{
				xui::color_picker( "基础颜色##qp", qp.color );
				xui::color_picker( "收回颜色##qp", qp.retrack_color );
				xui::end_popup( );
			}

			xui::checkbox( "下蹲探身", dp.enabled );

			xui::end_child( );
		}
	}

} // namespace rendering
