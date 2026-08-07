#include <pch/pch.hpp>
#include <utilities/diag.hpp>
#include <core/resources/fonts/inter.hpp>
#include <core/resources/fonts/pixel7.hpp>
#include <core/resources/fonts/cjk_subset.hpp>
#include "../rendering.hpp"

namespace rendering {

	namespace {

		// CJK glyphs are not present in the embedded Latin families. Prefer a
		// system font that covers Simplified Chinese so the menu can render
		// localized text; the first successfully loaded font becomes the xui
		// primary font, so the fallback chain keeps the menu functional on
		// systems without any CJK font installed.
		[[nodiscard]] const wchar_t* find_chinese_font_path( )
		{
			static constexpr const wchar_t* k_candidates[ ] =
			{
				L"C:\\Windows\\Fonts\\msyh.ttc",   // Microsoft YaHei
				L"C:\\Windows\\Fonts\\msyhbd.ttc", // Microsoft YaHei Bold
				L"C:\\Windows\\Fonts\\simhei.ttf", // SimHei
				L"C:\\Windows\\Fonts\\simsun.ttc", // SimSun
			};

			for ( const auto* path : k_candidates )
			{
				if ( GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES )
				{
					return path;
				}
			}

			return nullptr;
		}

		// A CJK atlas needs room for thousands of glyphs; 2048x2048 holds the
		// whole menu vocabulary at menu sizes.
		constexpr auto k_cjk_atlas{ 2048 };

	} // namespace

	void fonts::initialize( )
	{
		const auto chinese_path = find_chinese_font_path( );
		if ( chinese_path )
		{
			diag::writef( diag::level::info, "[fonts] loading CJK font: %ls", chinese_path );
			this->load_family_file( this->chinese, chinese_path, { 12.0f, 15.0f, 18.0f } );
		}
		else
		{
			diag::write( diag::level::warning, "[fonts] no system CJK font found" );
		}

		// Fallback: if no system CJK font is available (or its atlas could not
		// be created), the embedded subset keeps the localized menu readable.
		if ( !this->chinese[ size::normal ] )
		{
			diag::write( diag::level::warning, "[fonts] system CJK font unavailable; using embedded subset" );
			this->load_family( this->chinese, std::as_bytes( std::span{ resources::fonts::cjk_subset } ), { 12.0f, 15.0f, 18.0f } );
		}

		this->load_family( this->inter_medium, std::as_bytes( std::span{ resources::fonts::inter::regular } ), { 12.0f, 15.0f, 18.0f } );
		this->load_family( this->inter_bold, std::as_bytes( std::span{ resources::fonts::inter::bold } ), { 12.0f, 15.0f, 18.0f } );
		this->load_family( this->smallest_pixel7, std::as_bytes( std::span{ resources::fonts::pixel7::smallest } ), { 9.0f, 10.5f, 14.0f } );

		// Every glyph a Latin font lacks (e.g. CJK menu text when the CJK font
		// failed to become the primary font) resolves through this chain.
		for ( auto i = 0ull; i < static_cast< std::size_t >( size::count ); ++i )
		{
			const auto s = static_cast< size >( i );
			if ( this->chinese[ s ] )
			{
				if ( this->inter_medium[ s ] ) this->inter_medium[ s ]->fallback = this->chinese[ s ];
				if ( this->inter_bold[ s ] ) this->inter_bold[ s ]->fallback = this->chinese[ s ];
				if ( this->smallest_pixel7[ s ] ) this->smallest_pixel7[ s ]->fallback = this->chinese[ s ];
			}
		}

		// The renderer registers its embedded Inter font as the primary font
		// during device setup, before this initializer runs. Promote the CJK
		// family to primary so the localized menu actually renders; Latin
		// glyphs come from the CJK font itself, so nothing is lost.
		if ( this->chinese[ size::normal ] )
		{
			xdraw::set_primary_font( this->chinese[ size::normal ] );
		}

		diag::writef(
			diag::level::info,
			"[fonts] primary_size=%.1f chinese=%s inter=%s pixel7=%s",
			xdraw::primary_font( ) ? xdraw::primary_font( )->size : 0.0f,
			this->chinese[ size::normal ] ? "loaded" : "null",
			this->inter_medium[ size::normal ] ? "loaded" : "null",
			this->smallest_pixel7[ size::normal ] ? "loaded" : "null" );
	}

	void fonts::load_family( family_t& family, std::span<const std::byte> data, const std::array<float, static_cast< std::size_t >( size::count )>& sizes )
	{
		for ( auto i = 0ull; i < sizes.size( ); ++i )
		{
			family.sizes[ i ] = xdraw::load_font( data, sizes[ i ] );
		}
	}

	void fonts::load_family_file( family_t& family, const wchar_t* path, const std::array<float, static_cast< std::size_t >( size::count )>& sizes )
	{
		for ( auto i = 0ull; i < sizes.size( ); ++i )
		{
			// The CJK atlas is large; if texture creation fails (driver limits,
			// low vram), retry with the standard atlas size before giving up.
			family.sizes[ i ] = xdraw::load_font_file( path, sizes[ i ], k_cjk_atlas, k_cjk_atlas );
			if ( !family.sizes[ i ] )
			{
				family.sizes[ i ] = xdraw::load_font_file( path, sizes[ i ] );
			}

			if ( !family.sizes[ i ] )
			{
				diag::writef( diag::level::warning, "[fonts] load_font_file failed: %ls size=%.0f", path, sizes[ i ] );
			}
		}
	}

} // namespace rendering
