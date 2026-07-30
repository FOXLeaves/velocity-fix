#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>

#include "../systems.hpp"

namespace systems {

	void view::update( std::uintptr_t view )
	{
		this->m_matrix = memory::read<math::matrix4x4>( addresses::globals::view_matrix );

		if ( view )
		{
			this->m_origin = memory::read<math::vector3>( view + 0x0 );
			this->m_angles = memory::read<math::vector3>( view + 0xc );
			this->m_fov = memory::read<float>( view + 0x18 );
		}
		else
		{
			this->m_origin = { k_invalid, k_invalid, k_invalid };
			this->m_angles = { k_invalid, k_invalid, k_invalid };
			this->m_fov = k_invalid;
		}
	}

	math::vector2 view::project( const math::vector3& world_pos )
	{
		const auto& m = this->m_matrix;

		if ( m[ 3 ][ 3 ] == 0.0f )
		{
			return { this->k_invalid, this->k_invalid };
		}

		const auto w = m[ 3 ][ 0 ] * world_pos.x + m[ 3 ][ 1 ] * world_pos.y + m[ 3 ][ 2 ] * world_pos.z + m[ 3 ][ 3 ];

		if ( w < 0.01f )
		{
			return { this->k_invalid, this->k_invalid };
		}

		const auto x = m[ 0 ][ 0 ] * world_pos.x + m[ 0 ][ 1 ] * world_pos.y + m[ 0 ][ 2 ] * world_pos.z + m[ 0 ][ 3 ];
		const auto y = m[ 1 ][ 0 ] * world_pos.x + m[ 1 ][ 1 ] * world_pos.y + m[ 1 ][ 2 ] * world_pos.z + m[ 1 ][ 3 ];

		const auto display = xdraw::viewport_size( );
		const auto inv_w = 1.0f / w;

		return
		{
			display.first * 0.5f * ( 1.0f + x * inv_w ),
			display.second * 0.5f * ( 1.0f - y * inv_w )
		};
	}

	view::projection view::project_full( const math::vector3& world_pos ) const
	{
		const auto& m = this->m_matrix;

		const auto w = m[ 3 ][ 0 ] * world_pos.x + m[ 3 ][ 1 ] * world_pos.y + m[ 3 ][ 2 ] * world_pos.z + m[ 3 ][ 3 ];
		const auto x = m[ 0 ][ 0 ] * world_pos.x + m[ 0 ][ 1 ] * world_pos.y + m[ 0 ][ 2 ] * world_pos.z + m[ 0 ][ 3 ];
		const auto y = m[ 1 ][ 0 ] * world_pos.x + m[ 1 ][ 1 ] * world_pos.y + m[ 1 ][ 2 ] * world_pos.z + m[ 1 ][ 3 ];

		const auto [sw, sh] = xdraw::viewport_size( );
		const auto inv_w = 1.0f / w;

		const math::vector2 screen
		{
			sw * 0.5f * ( 1.0f + x * inv_w ),
			sh * 0.5f * ( 1.0f - y * inv_w )
		};

		projection r{};
		r.screen = screen;
		r.w = w;
		r.on_screen = w > 0.01f&& screen.x >= 0.0f && screen.x < static_cast< float >( sw )&& screen.y >= 0.0f && screen.y < static_cast< float >( sh );
		return r;
	}

} // namespace systems