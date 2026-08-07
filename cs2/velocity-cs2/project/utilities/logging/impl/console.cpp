#include <pch/pch.hpp>
#include <utilities/diag.hpp>
#include <utilities/memory/memory.hpp>
#include <protection/patterns.hpp>
#include <protection/game_addresses.hpp>
#include "../logging.hpp"

namespace logging::console {

	namespace {

		std::uintptr_t s_log_a1{};
		std::uint32_t s_log_channel{};
		std::int32_t s_log_severity{};
		std::uintptr_t s_log_metadata{};

	}

	bool initialize () {
		// note: this used to resolve tier0's LoggingSystem_Log by hardcoded
		// ordinal, which is stale on current cs2 builds and crashed the game
		// whenever anything was printed. Logging now goes to the structured
		// diagnostics file next to the DLL, the debugger output, and the
		// in-game developer console through the engine log pipeline (the
		// console color follows the severity level).

		// The engine maps console severity level 4 to purple; patch the
		// color-table entry to green so kill logs render green (hit =
		// yellow level 2, miss = red level 3).
		const auto color_entry = PATTERN( patterns::severity4_color );
		if ( color_entry )
		{
			__try
			{
				DWORD old_protect{};
				auto* const slot = reinterpret_cast< std::uint8_t* >( color_entry + 6 );
				if ( VirtualProtect( slot, 4, PAGE_READWRITE, &old_protect ) )
				{
					*reinterpret_cast< std::uint32_t* >( slot ) = 0xFF00FF00u; // RGBA green
					VirtualProtect( slot, 4, old_protect, &old_protect );
				}
			}
			__except ( EXCEPTION_EXECUTE_HANDLER ) { }
		}

		return true;
	}

	void cache_engine_log_context (std::uintptr_t a1, std::uint32_t channel, std::int32_t severity, std::uintptr_t metadata) {
		s_log_a1 = a1;
		s_log_channel = channel;
		s_log_severity = severity;
		s_log_metadata = metadata;
	}

	void print_raw (const char* text, std::int32_t severity) {
		if ( !text ) {
			return;
		}

		const bool was_emitting = emitting;
		emitting = true;
		diag::write( diag::level::info, text );

		// Mirror into the in-game developer console through the engine log
		// pipeline (raw call - the context was captured from the log_internal
		// hook, so the message lands in the channel the game prints to).
		// A trailing newline keeps consecutive hit/miss lines separated.
		if ( s_log_a1 ) {
			__try {
				std::string line{ text };
				line += '\n';
				memory::call<std::intptr_t>(
					PATTERN( patterns::log_internal ),
					s_log_a1, s_log_channel, severity, s_log_metadata,
					line.c_str( ), nullptr );
			}
			__except ( EXCEPTION_EXECUTE_HANDLER ) { }
		}

		emitting = was_emitting;
	}

} // namespace logging::console
