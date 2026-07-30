#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>
#include <protection/game_addresses.hpp>
#include "../addresses.hpp"

namespace addresses::functions {

	bool initialize( )
	{

		// cheat hooks
		present         = memory::get_vfunc( addresses::globals::swap_chain, 8 );
		resize_buffers  = memory::get_vfunc( addresses::globals::swap_chain, 13 );
		return true;
	}

} // namespace addresses::functions
