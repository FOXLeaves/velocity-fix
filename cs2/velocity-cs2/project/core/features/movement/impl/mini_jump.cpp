#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

#include "../movement.hpp"
#include "utils.hpp"

namespace features::movement {

	// Mini jump (celerity parity): while grounded with jump held, duck on
	// every grounded tick. The server applies the duck+jump combo on the
	// takeoff, cutting the hop height and the landing noise.
	void mini_jump::on_create_move( systems::input::usercmd* cmd ) const
	{
		if ( !settings::g_movement.mini_jump.value )
		{
			return;
		}

		// Systems that own the jump/duck input take over this tick.
		if ( features::movement::g_jumpbug.active_this_tick( ) || features::movement::g_edgebug.active_this_tick( ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		if ( utils::is_restricted_move_type( local.pawn ) )
		{
			return;
		}

		const auto& prestate = systems::g_prediction.pre( );
		if ( !( prestate.flags & cstypes::entity_flags::on_ground ) )
		{
			return;
		}

		const auto jump_held = ( cmd->buttons.value & cstypes::command_buttons::in_jump ) != 0
			|| ( GetAsyncKeyState( VK_SPACE ) & 0x8000 ) != 0;
		if ( !jump_held )
		{
			return;
		}

		cmd->buttons.value |= cstypes::command_buttons::in_duck;
		cmd->buttons.value_changed |= cstypes::command_buttons::in_duck;
		cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_duck;
	}

} // namespace features::movement
