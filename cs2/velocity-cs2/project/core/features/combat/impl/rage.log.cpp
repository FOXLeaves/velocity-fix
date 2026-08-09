#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <utilities/diag.hpp>
#include <utilities/random/random.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

// Shot log module: console output for delivered shots and the impacts
// (hit/miss tracker) linkage, split out of rage.cpp. fire_gun reports
// every shot through here; the tracker owns the hit/miss resolution
// and the on-screen logs.
//
// Console output is budgeted by AMMO, exactly like the impacts chat/miss
// quota: log_shot queues one entry per fire decision, and the per-tick
// ammo-confirmation pass (flush_console_shot_logs) prints as many
// entries as bullets actually left the clip - one bullet = one line, a
// double-tap pair (two bullets) = two lines, decisions that never fired
// (no ammo consumed, server rejection) are dropped. No delay-based
// throttling.
namespace features::combat {

	namespace {

		constexpr auto k_max_pending_shot_logs{ 16 };

		struct pending_shot_log
		{
			int severity{ 2 };
			std::string text{};
		};

		std::deque<pending_shot_log>& pending_shot_logs( )
		{
			static std::deque<pending_shot_log> queue{};
			return queue;
		}

		int& previous_clip( )
		{
			static int clip{ -1 };
			return clip;
		}

		void queue_console_shot_log( int severity, const std::string& text )
		{
			if ( !settings::g_misc.m_impacts.console_log.value )
			{
				return;
			}

			auto& queue = pending_shot_logs( );
			// Backlog guard: decisions that never consumed ammo (stale
			// fire expression, server rejection) must not grow the queue
			// without bound - drop the oldest entry.
			if ( queue.size( ) >= k_max_pending_shot_logs )
			{
				queue.pop_front( );
			}
			queue.push_back( pending_shot_log{ severity, text } );
		}

	} // namespace

	void rage::flush_console_shot_logs( )
	{
		const auto& ctx = g_shared.ctx( );
		if ( !settings::g_misc.m_impacts.console_log.value || !ctx.weapon )
		{
			return;
		}

		// Ammo confirmation: the clip difference between ticks is exactly
		// how many rounds really left the barrel (server-synced) - the same
		// signal the double-tap charge tracking uses.
		const auto clip = memory::read<int>( ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_hash ) );
		auto& prev = previous_clip( );

		auto budget{ 0 };
		if ( prev >= 0 && clip < prev )
		{
			budget = prev - clip;
		}
		prev = clip;

		auto& queue = pending_shot_logs( );
		while ( budget > 0 && !queue.empty( ) )
		{
			logging::console::print_severity( queue.front( ).severity, xs( "{}" ), queue.front( ).text );
			queue.pop_front( );
			--budget;
		}

		// Any leftover budget (clip dropped faster than decisions, weapon
		// switch) is discarded - entries are only printed for confirmed
		// bullets, never retroactively.
	}

	void rage::log_revolver_aim( const target& tgt, const math::vector3& aim_angle, int stamp_tick )
	{
		queue_console_shot_log( 2, std::format(
			"[r8] aim ({:.2f},{:.2f}) -> final ({:.2f},{:.2f}) tick {}",
			tgt.hit.aim_angle.x, tgt.hit.aim_angle.y,
			aim_angle.x, aim_angle.y,
			stamp_tick ) );
	}

	void rage::log_shot( systems::input::usercmd* cmd, const target& tgt, const math::vector3& aim_angle, const math::vector3& aim_position, const math::vector3& shoot_eye, int stamp_tick, bool was_forced, bool seed_mode )
	{
		const auto& shared_ctx = g_shared.ctx( );

		const auto hitgroup_name = systems::g_hitboxes.hitgroup_to_name( tgt.hit.hitgroup );
		const auto bt_delta = std::max( stamp_tick - tgt.hit.record->tick, 0 );
		queue_console_shot_log( 2, std::format(
			"[velocity] 射击 {}，生命 {}，伤害 {:.0f}（{}），命中率 {:.0f}%，回溯 {}t{}",
			hitgroup_name,
			tgt.hit.health,
			tgt.hit.damage,
			hitgroup_name,
			tgt.hitchance * 100.0f,
			bt_delta,
			was_forced ? "（强制）" : "" ) );

		features::misc::g_impacts.on_boom(
			{
				.victim_pawn = tgt.hit.pawn,
				.command_number = cmd->command_number,
				.hitgroup = tgt.hit.hitgroup,
				.target_health = tgt.hit.health,
				.damage = tgt.hit.damage,
				.hitchance = tgt.hitchance,
				.inaccuracy = shared_ctx.inaccuracy,
				.spread = shared_ctx.spread,
				.aim_angle = aim_angle,
				.aim_position = aim_position,
				.shoot_position = shoot_eye,
				.tick = tgt.hit.record->tick,
				.stamp_tick = stamp_tick,
				.skeleton = g_shared.lc( ).get_skeleton( *tgt.hit.record ),
				.forced = was_forced,
				.extrapolated = tgt.hit.record->extrapolated,
				.penetrated = tgt.hit.penetrated,
				.seed_mode = seed_mode,
				.dt = settings::g_combat.m_ragebot.m_double_tap.enabled.value
					&& shared_ctx.item_def_idx != cstypes::item_definition_index::weapon_r8_revolver,
			} );
	}

} // namespace features::combat
