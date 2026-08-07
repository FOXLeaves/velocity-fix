#include <pch/pch.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* hitbox_names_legit[ ]{ "头部", "胸部", "腹部", "手臂", "腿部" };

	} // namespace detail

	void menu::draw_legitbot( float group_w ) const
	{
		auto& s = settings::g_combat;
		auto& lb = s.m_legitbot;
		auto& wg = lb.groups[ this->m_subtab ];

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap + tokens::sidebar_w + tokens::gap;
		const auto body_y = wy + tokens::gap + tokens::subtab_bar_h + tokens::gap;
		const auto content_w = this->m_w - tokens::gap * 2.0f - tokens::sidebar_w - tokens::gap;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		xui::layout::set_cursor( content_x - wx, body_y - wy );

		if ( xui::begin_child( "##legitbot_master", lb.enabled.value ? col_w : content_w ) )
		{
			xui::checkbox( "启用", lb.enabled );
			xui::end_child( );
		}

		if ( !lb.enabled.value )
		{
			return;
		}

		if ( xui::begin_child( "##legitbot_aimbot", col_w ) )
		{
			xui::checkbox( "瞄准机器人", wg.aimbot );

			xui::slider_float( "视角", wg.fov, 0.5f, 30.0f, "%.1f°" );
			xui::slider_int( "平滑", wg.smooth, 0, 100, "%d" );
			xui::multicombo( "命中盒", wg.hitboxes, detail::hitbox_names_legit, 5 );

			xui::checkbox( "可视化视角", wg.visualize_fov );

			if ( xui::begin_popup( "##fov_color_popup", 220.0f ) )
			{
				xui::color_picker( "颜色##fov", wg.fov_color );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		if ( xui::begin_child( "##legitbot_rcs", col_w ) )
		{
			xui::checkbox( "后坐力控制", wg.rcs );
			if ( xui::begin_popup( "##rcs_popup", 220.0f ) )
			{
				xui::slider_int( "最小##rcs", wg.rcs_min, 50, 150, "%d%%" );
				xui::slider_int( "最大##rcs", wg.rcs_max, 50, 150, "%d%%" );
				xui::end_popup( );
			}

			xui::checkbox( "独立后座补偿", wg.standalone_rcs );
			if ( xui::begin_popup( "##srcs_popup", 220.0f ) )
			{
				xui::slider_int( "强度##srcs", wg.standalone_rcs_strength, 0, 100, "%d%%" );
				xui::slider_int( "最小##srcs", wg.standalone_rcs_min, 50, 150, "%d%%" );
				xui::slider_int( "最大##srcs", wg.standalone_rcs_max, 50, 150, "%d%%" );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		xui::layout::set_cursor( right_x - wx, body_y - wy );

		if ( xui::begin_child( "##legitbot_triggerbot", col_w ) )
		{
			xui::checkbox( "扳机", wg.triggerbot );
			xui::slider_int( "延迟", wg.trigger_delay, 0, 250, "%d ms" );
			xui::slider_int( "命中率", wg.trigger_hitchance, 0, 100, "%d%%" );
			xui::checkbox( "仅爆头", wg.trigger_head_only );
			xui::checkbox( "使用种子", wg.give_me_your_seed );
			if ( wg.give_me_your_seed.value )
			{
				xui::slider_int( "露出最低伤害##seed", wg.seed_min_damage, 1, 125, "%d" );
				xui::checkbox( "种子约束", wg.seed_constraint );
			}

			xui::end_child( );
		}

		if ( xui::begin_child( "##legitbot_other", col_w ) )
		{
			xui::checkbox( "穿墙", wg.autowall );
			if ( xui::begin_popup( "##aw_popup", 220.0f ) )
			{
				xui::slider_int( "最低伤害##aw", wg.min_damage, 1, 125, "%d" );
				xui::end_popup( );
			}

			xui::end_child( );
		}
	}

} // namespace rendering