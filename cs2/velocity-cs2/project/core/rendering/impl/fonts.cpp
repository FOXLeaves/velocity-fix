#include <pch/pch.hpp>
#include "../rendering.hpp"

namespace rendering {

	void fonts::initialize( )
	{
		this->load_family( this->inter_medium, rpack::fonts::inter::medium, { 12.0f, 15.0f, 18.0f } );
		this->load_family( this->inter_bold, rpack::fonts::inter::bold, { 12.0f, 15.0f, 18.0f } );
		this->load_family( this->smallest_pixel7, rpack::fonts::pixel7::smallest, { 9.0f, 10.5f, 14.0f } );
	}

	void fonts::load_family( family_t& family, std::uint32_t resource_id, const std::array<float, static_cast< std::size_t >( size::count )>& sizes )
	{
		auto data = std::as_bytes( rpack::reader::get( resource_id ) );

		for ( auto i = 0ull; i < sizes.size( ); ++i )
		{
			family.sizes[ i ] = xdraw::load_font( data, sizes[ i ] );
		}
	}

} // namespace rendering