#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/hooking/hooking.hpp>
#include <utilities/security/security.hpp>
#include <utilities/logging/logging.hpp>
#include <protection/game_addresses.hpp>
#include "../hooks.hpp"
#include <core/systems/systems.hpp>

namespace hooks {

	bool vac::initialize( )
	{
		if ( !hooking::manager::create( {
			{ &m_analyze_pe_module, &analyze_pe_module, xs( "analyze_pe_module" ), PATTERN (patterns::analyze_pe_module) },
			{ &m_build_diagnostic_response, &build_diagnostic_response, xs( "build_diagnostic_response" ), PATTERN (patterns::build_diagnostic_response) },
			{ &m_string_copy, &string_copy, xs( "string_copy" ), PATTERN (patterns::string_copy) }
			} ) )
		{
			return false;
		}

		return true;
	}

	void vac::shutdown( )
	{
		m_analyze_pe_module.reset( );
		m_build_diagnostic_response.reset( );
		m_string_copy.reset( );
	}

	static auto make_gradient_label (const char* text, std::uint8_t sr, std::uint8_t sg, std::uint8_t sb, std::uint8_t er, std::uint8_t eg, std::uint8_t eb) -> std::string {
		const auto len = std::strlen (text);
		if (len == 0)
			return {};

		std::string result {};
		result.reserve (len * 40);

		for (auto i = 0ull; i < len; ++i) {
			const auto t = len > 1 ? static_cast<float>(i) / static_cast<float>(len - 1) : 0.0f;
			const auto r = static_cast<std::uint8_t>(sr + (er - sr) * t);
			const auto g = static_cast<std::uint8_t>(sg + (eg - sg) * t);
			const auto b = static_cast<std::uint8_t>(sb + (eb - sb) * t);

			char tag [48];
			std::snprintf (tag, sizeof (tag), "<font color='#%02X%02X%02X'>%c</font>", r, g, b, text [i]);
			result += tag;
		}

		return result;
	}

	static void chat_print (const char* label_text, std::uint8_t sr, std::uint8_t sg, std::uint8_t sb,
												 std::uint8_t er, std::uint8_t eg, std::uint8_t eb,
												 const char* msg) {

		const auto local = systems::g_local.get ();
		if (!local.is_valid() || !local.is_alive || systems::g_local.is_in_cinematic () || !local.pawn) {
			return;
		}

		const auto hud_element = memory::call<std::uintptr_t> (PATTERN (patterns::find_hud_element), xs ("CCSGO_HudVoiceStatus"));
		if (!hud_element)
			return;

		const auto voice = hud_element - 32;
		const auto label = make_gradient_label (label_text, sr, sg, sb, er, eg, eb);

		char buf [1024];
		std::snprintf (buf, sizeof (buf), "%s <font color='#CCCCCC'>- %s</font>", label.c_str (), msg);

		std::uint8_t flags [2] {1, 0};
		memory::call<void> (PATTERN (patterns::set_voice_data), voice, buf, 0xFFFFFFFF, flags);
	}

	std::uint8_t __fastcall vac::analyze_pe_module( std::uintptr_t module_base, std::uintptr_t out_info, char calculate_hash )
	{
		const auto result = m_analyze_pe_module.call<std::uint8_t>( module_base, out_info, calculate_hash );
		if ( !result || !calculate_hash )
		{
			return result;
		}

		const auto cached = security::integrity::get( module_base );
		if ( cached && cached->valid )
		{
			const auto vac_crc = memory::read<std::uint32_t>( out_info + 12 );

			logging::console::print( xs( "module hash check: base=0x{:X} vac_crc=0x{:08X} cached_crc=0x{:08X} {} [vac]" ), module_base, vac_crc, cached->crc32, ( vac_crc != cached->crc32 ) ? xs( "mismatch - fixing" ) : xs( "ok" ) );

			chat_print ("vac", 0xFF, 0x4C, 0x4C, 0xFF, 0xA0, 0x00,
			std::format ("is sniffing hashes: base=0x{:X} got=0x{:08X} real=0x{:08X} {}",
						module_base, vac_crc, cached->crc32,
						(vac_crc != cached->crc32) ? "lying piece of shit, patched" : "matches, letting it go").c_str ());


			memory::write( out_info + 12, cached->crc32 );
		}

		return result;
	}

	void __fastcall vac::build_diagnostic_response( std::intptr_t request, DWORD thread_id )
	{
		const auto diag_count = memory::read<int>( request + 72 );
		const auto diag_array = memory::read<std::uintptr_t>( request + 80 );

		for ( auto i = 0; i < diag_count; i++ )
		{
			const auto diag = memory::read<std::uintptr_t>( diag_array + 8 * static_cast< std::size_t >( i ) + 8 );
			const auto type = memory::read<int>( diag + 68 );

			if ( type == 28 )
			{
				const auto address = memory::read<void*>( diag + 48 );
				const auto size = memory::read<std::uint32_t>( diag + 64 );

				if ( security::regions::is_protected( address, size ) )
				{
					memory::write<void*>( diag + 48, nullptr );
					logging::console::print( xs( "tried to dump memory at: 0x{:X} size {} [vac]" ), reinterpret_cast< std::uintptr_t >( address ), size );
					chat_print ("vac", 0xFF, 0x4C, 0x4C, 0xFF, 0xA0, 0x00,
					std::format ("tried to dump our memory at 0x{:X} ({} bytes) - blocked",
								reinterpret_cast<std::uintptr_t>(address), size).c_str ());
				}
			}
			else if ( type == 29 )
			{
				auto module_str_ptr = memory::read<std::uintptr_t>( diag + 24 ) & ~3ull;
				if ( memory::read<std::uintptr_t>( module_str_ptr + 24 ) > 0xf )
				{
					module_str_ptr = memory::read<std::uintptr_t>( module_str_ptr );
				}

				auto function_str_ptr = memory::read<std::uintptr_t>( diag + 32 ) & ~3ull;
				if ( memory::read<std::uintptr_t>( function_str_ptr + 24 ) > 0xf )
				{
					function_str_ptr = memory::read<std::uintptr_t>( function_str_ptr );
				}

				const auto module_name = reinterpret_cast< const char* >( module_str_ptr );
				const auto func_name = reinterpret_cast< const char* >( function_str_ptr );

				chat_print ("vac", 0xFF, 0x4C, 0x4C, 0xFF, 0xA0, 0x00,
				std::format ("being nosy about {}!{} - noted",
							module_name ? module_name : "null", func_name ? func_name : "null").c_str ());

				logging::console::print( xs( "checked a function: {}!{} [vac]" ), module_name ? module_name : "null", func_name ? func_name : "null" );
			}
		}

		m_build_diagnostic_response.call<void>( request, thread_id );
	}

	std::intptr_t __fastcall vac::string_copy( void* dest, const void* src, std::uintptr_t size )
	{
		const auto src_addr = reinterpret_cast< std::uintptr_t >( src );
		const auto prologue = security::prologues::get( src_addr );

		if ( prologue )
		{
			const auto copy_size = std::min<std::size_t>( size, prologue->length );

			std::uint8_t clean_buffer[ 64 ]{};
			std::memcpy( clean_buffer, prologue->bytes, copy_size );

			if ( size > copy_size )
			{
				std::memcpy( clean_buffer + copy_size, reinterpret_cast< const void* >( src_addr + copy_size ), std::min<std::size_t>( size - copy_size, sizeof( clean_buffer ) - copy_size ) );
			}

			chat_print ("vac", 0xFF, 0x4C, 0x4C, 0xFF, 0xA0, 0x00,
			std::format ("tried to read our bytes at 0x{:X} ({} bytes) - fed it garbage",
						src_addr, size).c_str ());
			logging::console::print( xs( "spoofed function bytes at 0x{:X} size {} [vac]" ), src_addr, size );

			return m_string_copy.call<std::intptr_t>( dest, clean_buffer, size );
		}

		return m_string_copy.call<std::intptr_t>( dest, src, size );
	}

} // namespace hooks