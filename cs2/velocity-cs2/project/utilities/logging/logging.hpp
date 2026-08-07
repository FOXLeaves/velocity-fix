#pragma once

namespace logging {

	namespace console {

		bool initialize( );

		// severity selects the in-game console color (the engine maps it to
		// a color per severity level). Default 1 = info.
		void print_raw( const char* text, std::int32_t severity = 1 );

		// Cached from the engine log pipeline (hooks::utility::log_internal)
		// so print_raw can mirror messages into the in-game developer
		// console through the same channel the game itself prints to.
		void cache_engine_log_context( std::uintptr_t a1, std::uint32_t channel, std::int32_t severity, std::uintptr_t metadata );

		template <typename... args_t>
		void print( std::string_view fmt, args_t&&... args )
		{
			print_raw( std::vformat( fmt, std::make_format_args( args... ) ).c_str( ) );
		}

		template <typename... args_t>
		void print_severity( std::int32_t severity, std::string_view fmt, args_t&&... args )
		{
			print_raw( std::vformat( fmt, std::make_format_args( args... ) ).c_str( ), severity );
		}

		inline thread_local bool emitting{};

	} // namespace console

	namespace popup {

		bool initialize( );
		void show( const char* title, const char* message );

	} // namespace popup

} // namespace logging
