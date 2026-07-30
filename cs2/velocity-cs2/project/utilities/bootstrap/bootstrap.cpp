#include <pch/pch.hpp>

#include <thread>
#include <vector>

#include <winhttp.h>

#include <external/rpack.hpp>

#pragma comment( lib, "winhttp.lib" )

namespace bootstrap {

	namespace {

		constexpr wchar_t k_dest_path[ ] = L"C:\\resources.rpak";
		constexpr wchar_t k_temp_path[ ] = L"C:\\resources.rpak.part";

		inline std::atomic<bool> g_worker_launched{};
		inline std::atomic<bool> g_worker_finished{};

		inline bool append_response_body( const void* chunk, DWORD size, HANDLE file )
		{
			DWORD written{};
			return WriteFile( file, chunk, size, &written, nullptr ) && written == size;
		}

		inline void resource_worker( )
		{
			DeleteFileW( k_temp_path );

			const auto h_session = WinHttpOpen( L"velocity-bootstrap/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
			if ( !h_session )
			{
				g_worker_finished.store( true, std::memory_order_release );
				return;
			}

			const DWORD access_flags = WINHTTP_FLAG_REFRESH | WINHTTP_FLAG_SECURE;

			const auto h_connect = WinHttpConnect( h_session, L"velocity.dog", INTERNET_DEFAULT_HTTPS_PORT, 0 );
			if ( !h_connect )
			{
				WinHttpCloseHandle( h_session );
				g_worker_finished.store( true, std::memory_order_release );
				return;
			}

			const auto h_request = WinHttpOpenRequest( h_connect, L"GET", L"/resources.rpak", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, access_flags );
			const auto req_ok =
				h_request
				&& WinHttpSendRequest( h_request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 )
				&& WinHttpReceiveResponse( h_request, nullptr );

			DWORD status{};
			DWORD status_size = sizeof( status );

			const auto hdr_ok =
				req_ok
				&& WinHttpQueryHeaders(
					h_request,
					WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
					WINHTTP_HEADER_NAME_BY_INDEX,
					&status,
					&status_size,
					WINHTTP_NO_HEADER_INDEX
				);

			if ( !hdr_ok )
			{
				if ( h_request )
				{
					WinHttpCloseHandle( h_request );
				}
				WinHttpCloseHandle( h_connect );
				WinHttpCloseHandle( h_session );
				g_worker_finished.store( true, std::memory_order_release );
				return;
			}

			const HANDLE file_handle = CreateFileW( k_temp_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

			if ( file_handle == INVALID_HANDLE_VALUE )
			{
				WinHttpCloseHandle( h_request );
				WinHttpCloseHandle( h_connect );
				WinHttpCloseHandle( h_session );
				g_worker_finished.store( true, std::memory_order_release );
				return;
			}

			DWORD total_read{};
			DWORD available{};

			for ( ;; )
			{
				if ( !WinHttpQueryDataAvailable( h_request, &available ) || available == 0 )
				{
					break;
				}

				std::vector<unsigned char> chunk( available );

				DWORD read{};
				if ( !WinHttpReadData( h_request, chunk.data( ), available, &read ) || read == 0 )
				{
					break;
				}

				total_read += read;

				if ( !append_response_body( chunk.data( ), read, file_handle ) )
				{
					CloseHandle( file_handle );
					DeleteFileW( k_temp_path );
					WinHttpCloseHandle( h_request );
					WinHttpCloseHandle( h_connect );
					WinHttpCloseHandle( h_session );
					g_worker_finished.store( true, std::memory_order_release );
					return;
				}
			}

			CloseHandle( file_handle );
			WinHttpCloseHandle( h_request );
			WinHttpCloseHandle( h_connect );
			WinHttpCloseHandle( h_session );

			if ( total_read == 0 || status != HTTP_STATUS_OK )
			{
				DeleteFileW( k_temp_path );
				g_worker_finished.store( true, std::memory_order_release );
				return;
			}

			DeleteFileW( k_dest_path );
			if ( !MoveFileExW( k_temp_path, k_dest_path, MOVEFILE_REPLACE_EXISTING ) )
			{
				DeleteFileW( k_temp_path );
			}

			g_worker_finished.store( true, std::memory_order_release );
		}

	} // namespace

	void on_dll_attach( )
	{
		bool expected = false;

		if ( !g_worker_launched.compare_exchange_strong( expected, true, std::memory_order_acq_rel ) )
		{
			return;
		}

		std::thread( [ ]
			{
				resource_worker( );
			} ).detach( );
	}

	bool try_load_rpak( )
	{
		static bool loaded{};
		if ( loaded )
		{
			return true;
		}

		if ( !g_worker_finished.load( std::memory_order_acquire ) )
		{
			return false;
		}

		if ( !rpack::reader::load( xs( "C:\\resources.rpak" ) ) )
		{
			return false;
		}

		loaded = true;
		return true;
	}

} // namespace bootstrap
