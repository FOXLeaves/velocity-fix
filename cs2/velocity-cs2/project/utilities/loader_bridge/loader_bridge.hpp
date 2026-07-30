#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace loader_bridge {

	const std::string& username( );
	const std::vector<std::byte>& avatar_png( );
	bool consume_avatar_png( std::vector<std::byte>& out );

} // namespace loader_bridge
