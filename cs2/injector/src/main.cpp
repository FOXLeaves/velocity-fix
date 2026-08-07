// velocity injector - native loader for the velocity-cs2 module.
// Borderless, per-pixel-alpha layered window rendered with GDI+ (anti-aliased
// rounded corners, matching the in-game xui style). Auto-targets cs2.exe.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <objidl.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <fstream>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdiplus.lib")

using Gdiplus::Color;
using Gdiplus::Graphics;
using Gdiplus::GraphicsPath;
using Gdiplus::RectF;
using Gdiplus::SolidBrush;
using Gdiplus::Pen;

namespace
{

	struct process_entry
	{
		DWORD pid{};
		std::wstring name{};
		bool is_64bit{};
	};

	// xui theme palette.
	constexpr COLORREF k_bg{ RGB( 35, 35, 40 ) };
	constexpr COLORREF k_bg_panel{ RGB( 25, 25, 30 ) };
	constexpr COLORREF k_bg_hover{ RGB( 45, 45, 52 ) };
	constexpr COLORREF k_border{ RGB( 62, 62, 72 ) };
	constexpr COLORREF k_border_focus{ RGB( 173, 192, 255 ) };
	constexpr COLORREF k_accent{ RGB( 173, 192, 255 ) };
	constexpr COLORREF k_text{ RGB( 220, 220, 228 ) };
	constexpr COLORREF k_text_dim{ RGB( 150, 150, 160 ) };
	constexpr COLORREF k_text_dark{ RGB( 20, 20, 25 ) };
	constexpr COLORREF k_close_hover{ RGB( 200, 60, 60 ) };

	constexpr int k_width{ 540 };
	constexpr int k_height{ 240 };
	constexpr int k_title_height{ 44 };
	constexpr int k_close_size{ 36 };

	HINSTANCE g_instance{ nullptr };
	HWND g_window{ nullptr };
	ULONG_PTR g_gdiplus_token{};

	process_entry g_target{};
	bool g_target_valid{ false };
	std::wstring g_dll_path{};
	std::wstring g_status_text{};
	bool g_dll_focused{ false };
	bool g_btn_hover[ 3 ]{ false, false, false };
	bool g_close_hover{ false };
	bool g_status_ok{ true };

	RECT g_title_rect{};
	RECT g_close_rect{};
	RECT g_target_rect{};
	RECT g_dll_rect{};
	RECT g_btn_rect[ 3 ]{};
	RECT g_inject_rect{};
	RECT g_status_rect{};

	Color make_color( COLORREF rgb, BYTE alpha = 255 )
	{
		return Color( alpha, GetRValue( rgb ), GetGValue( rgb ), GetBValue( rgb ) );
	}

	std::wstring last_error_message( DWORD code )
	{
		wchar_t* buffer{ nullptr };
		const auto length = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, code, 0, reinterpret_cast< wchar_t* >( &buffer ), 0, nullptr );

		std::wstring out = buffer ? std::wstring( buffer, length ) : L"未知错误";
		LocalFree( buffer );

		while ( !out.empty( ) && ( out.back( ) == L'\r' || out.back( ) == L'\n' || out.back( ) == L' ' ) )
		{
			out.pop_back( );
		}

		return out;
	}

	void set_status( const std::wstring& message, bool ok = true )
	{
		g_status_text = message;
		g_status_ok = ok;
		InvalidateRect( g_window, nullptr, TRUE );
	}

	bool enable_debug_privilege( )
	{
		HANDLE token{ nullptr };
		if ( !OpenProcessToken( GetCurrentProcess( ), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token ) )
		{
			return false;
		}

		LUID luid{};
		LookupPrivilegeValueW( nullptr, L"SeDebugPrivilege", &luid );

		TOKEN_PRIVILEGES privileges{};
		privileges.PrivilegeCount = 1;
		privileges.Privileges[ 0 ].Luid = luid;
		privileges.Privileges[ 0 ].Attributes = SE_PRIVILEGE_ENABLED;

		const auto ok = AdjustTokenPrivileges( token, FALSE, &privileges, 0, nullptr, nullptr )
			&& GetLastError( ) == ERROR_SUCCESS;

		CloseHandle( token );
		return ok;
	}

	bool process_is_64bit( HANDLE process )
	{
		USHORT machine{};
		USHORT native{};
		if ( IsWow64Process2( process, &machine, &native ) )
		{
			return native == IMAGE_FILE_MACHINE_AMD64;
		}

		BOOL wow64{ FALSE };
		IsWow64Process( process, &wow64 );
		return !wow64;
	}

	void refresh_processes( )
	{
		g_target_valid = false;

		const auto snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
		if ( snapshot == INVALID_HANDLE_VALUE )
		{
			set_status( L"进程快照失败", false );
			return;
		}

		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof( entry );

		if ( Process32FirstW( snapshot, &entry ) )
		{
			do
			{
				if ( _wcsicmp( entry.szExeFile, L"cs2.exe" ) == 0 )
				{
					g_target.pid = entry.th32ProcessID;
					g_target.name = entry.szExeFile;
					g_target_valid = true;

					const auto handle = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID );
					if ( handle )
					{
						g_target.is_64bit = process_is_64bit( handle );
						CloseHandle( handle );
					}

					break;
				}
			}
			while ( Process32NextW( snapshot, &entry ) );
		}

		CloseHandle( snapshot );

		if ( g_target_valid )
		{
			wchar_t count_buf[ 64 ]{};
			swprintf_s( count_buf, L"目标：%s（PID %lu）", g_target.name.c_str( ), g_target.pid );
			set_status( count_buf );
		}
		else
		{
			set_status( L"未检测到 cs2.exe", false );
		}
	}

	// ---- anti-detection helpers (cs2inject-bypassVAC style) ----

	constexpr ULONG k_thread_hide_from_debugger{ 0x4 };

	using pfn_nt_query_information_process = LONG( NTAPI* )( HANDLE, ULONG, PVOID, ULONG, PULONG );
	using pfn_nt_create_thread_ex = LONG( NTAPI* )( PHANDLE, ACCESS_MASK, PVOID, HANDLE, PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID );
	using pfn_nt_allocate_virtual_memory = LONG( NTAPI* )( HANDLE, PVOID*, ULONG_PTR, PSIZE_T, ULONG, ULONG );
	using pfn_nt_write_virtual_memory = LONG( NTAPI* )( HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T );
	using pfn_nt_free_virtual_memory = LONG( NTAPI* )( HANDLE, PVOID*, PSIZE_T, ULONG );
	using pfn_nt_protect_virtual_memory = LONG( NTAPI* )( HANDLE, PVOID*, PSIZE_T, ULONG, PULONG );

	struct process_basic_info
	{
		PVOID reserved1;
		PVOID peb_base;
		PVOID reserved2[ 2 ];
		ULONG_PTR unique_process_id;
		PVOID reserved3;
	};

	struct syscall_stub
	{
		BYTE code[ 12 ];
		FARPROC fn;
	};

	DWORD get_syscall_number( const char* name )
	{
		const auto ntdll = GetModuleHandleA( "ntdll.dll" );
		auto* p = reinterpret_cast< BYTE* >( GetProcAddress( ntdll, name ) );
		if ( !p )
		{
			return 0;
		}

		for ( auto i = 0; i < 64; ++i )
		{
			if ( p[ i ] == 0xB8 )
			{
				const auto ssn = *reinterpret_cast< DWORD* >( &p[ i + 1 ] );
				for ( auto j = i + 5; j < i + 24 && j < 64; ++j )
				{
					if ( p[ j ] == 0x0F && p[ j + 1 ] == 0x05 )
					{
						return ssn;
					}
				}
			}
		}

		return 0;
	}

	bool make_syscall_stub( DWORD ssn, syscall_stub& out )
	{
		if ( !ssn )
		{
			return false;
		}

		out.code[ 0 ] = 0x4C; out.code[ 1 ] = 0x8B; out.code[ 2 ] = 0xD1; // mov r10, rcx
		out.code[ 3 ] = 0xB8;                                            // mov eax, imm32
		std::memcpy( out.code + 4, &ssn, 4 );
		out.code[ 8 ] = 0x0F; out.code[ 9 ] = 0x05;                      // syscall
		out.code[ 10 ] = 0xC3;                                           // ret

		out.fn = reinterpret_cast< FARPROC >( VirtualAlloc( nullptr, sizeof( out.code ), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) );
		if ( !out.fn )
		{
			return false;
		}

		std::memcpy( out.fn, out.code, sizeof( out.code ) );
		FlushInstructionCache( GetCurrentProcess( ), out.fn, sizeof( out.code ) );
		return true;
	}

	ULONG_PTR find_module_base_ldr( HANDLE process, ULONG_PTR target_base )
	{
		const auto nt_qip = reinterpret_cast< pfn_nt_query_information_process >( GetProcAddress( GetModuleHandleA( "ntdll.dll" ), "NtQueryInformationProcess" ) );
		if ( !nt_qip )
		{
			return 0;
		}

		process_basic_info pbi{};
		ULONG ret_len = 0;
		if ( nt_qip( process, 0, &pbi, sizeof( pbi ), &ret_len ) != 0 || !pbi.peb_base )
		{
			return 0;
		}

		const auto peb = reinterpret_cast< ULONG_PTR >( pbi.peb_base );
		ULONG_PTR ldr = 0;
		SIZE_T rd = 0;
		if ( !ReadProcessMemory( process, reinterpret_cast< LPCVOID >( peb + 0x18 ), &ldr, sizeof( ldr ), &rd ) || !ldr )
		{
			return 0;
		}

		ULONG_PTR head = 0;
		if ( !ReadProcessMemory( process, reinterpret_cast< LPCVOID >( ldr + 0x10 ), &head, sizeof( head ), &rd ) )
		{
			return 0;
		}

		auto entry = head;
		for ( auto guard = 0; guard < 4096; ++guard )
		{
			ULONG_PTR base = 0;
			if ( !ReadProcessMemory( process, reinterpret_cast< LPCVOID >( entry + 0x30 ), &base, sizeof( base ), &rd ) )
			{
				break;
			}

			if ( base == target_base )
			{
				return base;
			}

			ULONG_PTR next = 0;
			if ( !ReadProcessMemory( process, reinterpret_cast< LPCVOID >( entry ), &next, sizeof( next ), &rd ) )
			{
				break;
			}

			if ( next == head || next == 0 )
			{
				break;
			}

			entry = next;
		}

		return 0;
	}

	ULONG_PTR find_module_base_toolhelp( DWORD pid, const std::wstring& file_name )
	{
		const auto snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid );
		if ( snapshot == INVALID_HANDLE_VALUE )
		{
			return 0;
		}

		MODULEENTRY32W me{};
		me.dwSize = sizeof( me );
		ULONG_PTR result = 0;

		std::wstring target = file_name;
		for ( auto& c : target )
		{
			c = static_cast< wchar_t >( towlower( c ) );
		}

		if ( Module32FirstW( snapshot, &me ) )
		{
			do
			{
				std::wstring name = me.szModule;
				for ( auto& c : name )
				{
					c = static_cast< wchar_t >( towlower( c ) );
				}

				if ( name == target )
				{
					result = reinterpret_cast< ULONG_PTR >( me.modBaseAddr );
					break;
				}
			}
			while ( Module32NextW( snapshot, &me ) );
		}

		CloseHandle( snapshot );
		return result;
	}

	void hide_module_ldr( HANDLE process, ULONG_PTR target_base )
	{
		const auto nt_qip = reinterpret_cast< pfn_nt_query_information_process >( GetProcAddress( GetModuleHandleA( "ntdll.dll" ), "NtQueryInformationProcess" ) );
		if ( !nt_qip )
		{
			return;
		}

		process_basic_info pbi{};
		ULONG ret_len = 0;
		if ( nt_qip( process, 0, &pbi, sizeof( pbi ), &ret_len ) != 0 || !pbi.peb_base )
		{
			return;
		}

		const auto peb = reinterpret_cast< ULONG_PTR >( pbi.peb_base );
		ULONG_PTR ldr = 0;
		SIZE_T rd = 0;
		if ( !ReadProcessMemory( process, reinterpret_cast< LPCVOID >( peb + 0x18 ), &ldr, sizeof( ldr ), &rd ) || !ldr )
		{
			return;
		}

		ULONG_PTR head = 0;
		if ( !ReadProcessMemory( process, reinterpret_cast< LPCVOID >( ldr + 0x10 ), &head, sizeof( head ), &rd ) )
		{
			return;
		}

		auto entry = head;
		auto found = false;
		for ( auto guard = 0; guard < 4096; ++guard )
		{
			ULONG_PTR base = 0;
			if ( !ReadProcessMemory( process, reinterpret_cast< LPCVOID >( entry + 0x30 ), &base, sizeof( base ), &rd ) )
			{
				break;
			}

			if ( base == target_base )
			{
				found = true;
				break;
			}

			ULONG_PTR next = 0;
			if ( !ReadProcessMemory( process, reinterpret_cast< LPCVOID >( entry ), &next, sizeof( next ), &rd ) )
			{
				break;
			}

			if ( next == head || next == 0 )
			{
				break;
			}

			entry = next;
		}

		if ( !found )
		{
			return;
		}

		// Unlink from all three LDR lists.
		for ( auto list = 0; list < 3; ++list )
		{
			const auto flink_off = list * 0x10;
			const auto blink_off = flink_off + 0x08;

			ULONG_PTR next = 0, prev = 0;
			if ( !ReadProcessMemory( process, reinterpret_cast< LPCVOID >( entry + flink_off ), &next, sizeof( next ), &rd ) )
			{
				continue;
			}

			if ( !ReadProcessMemory( process, reinterpret_cast< LPCVOID >( entry + blink_off ), &prev, sizeof( prev ), &rd ) )
			{
				continue;
			}

			if ( !next || !prev || next == entry || prev == entry )
			{
				continue;
			}

			WriteProcessMemory( process, reinterpret_cast< LPVOID >( next + blink_off ), &prev, sizeof( prev ), nullptr );
			WriteProcessMemory( process, reinterpret_cast< LPVOID >( prev + flink_off ), &next, sizeof( next ), nullptr );
		}

		ULONG_PTR zero = 0;
		for ( auto list = 0; list < 3; ++list )
		{
			WriteProcessMemory( process, reinterpret_cast< LPVOID >( entry + list * 0x10 ), &zero, sizeof( zero ), nullptr );
			WriteProcessMemory( process, reinterpret_cast< LPVOID >( entry + list * 0x10 + 0x08 ), &zero, sizeof( zero ), nullptr );
		}
	}

	void fake_file_timestamp( const wchar_t* path )
	{
		WIN32_FILE_ATTRIBUTE_DATA reference{};
		if ( !GetFileAttributesExW( L"C:\\Windows\\System32\\kernel32.dll", GetFileExInfoStandard, &reference ) )
		{
			return;
		}

		const auto file = CreateFileW( path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
		if ( file == INVALID_HANDLE_VALUE )
		{
			return;
		}

		SetFileTime( file, &reference.ftCreationTime, &reference.ftLastAccessTime, &reference.ftLastWriteTime );
		CloseHandle( file );
	}

	void random_sleep( int base_ms, int jitter_ms )
	{
		const auto delay = base_ms + ( GetTickCount( ) % ( jitter_ms + 1 ) );
		Sleep( static_cast< DWORD >( delay ) );
	}

	bool inject_dll( DWORD pid, const std::wstring& dll_path )
	{
		enable_debug_privilege( );

		// ---- prepare syscall stubs (fall back to ntdll exports) ----
		syscall_stub s_thread{}, s_alloc{}, s_write{}, s_free{}, s_protect{};
		const auto ssn_thread = get_syscall_number( "NtCreateThreadEx" );
		const auto ssn_alloc = get_syscall_number( "NtAllocateVirtualMemory" );
		const auto ssn_write = get_syscall_number( "NtWriteVirtualMemory" );
		const auto ssn_free = get_syscall_number( "NtFreeVirtualMemory" );
		const auto ssn_protect = get_syscall_number( "NtProtectVirtualMemory" );

		auto use_syscall = make_syscall_stub( ssn_thread, s_thread )
			&& make_syscall_stub( ssn_alloc, s_alloc )
			&& make_syscall_stub( ssn_write, s_write )
			&& make_syscall_stub( ssn_free, s_free )
			&& make_syscall_stub( ssn_protect, s_protect );

		const auto ntdll = GetModuleHandleA( "ntdll.dll" );
		const auto nt_thread = reinterpret_cast< pfn_nt_create_thread_ex >( GetProcAddress( ntdll, "NtCreateThreadEx" ) );
		const auto nt_alloc = reinterpret_cast< pfn_nt_allocate_virtual_memory >( GetProcAddress( ntdll, "NtAllocateVirtualMemory" ) );
		const auto nt_write = reinterpret_cast< pfn_nt_write_virtual_memory >( GetProcAddress( ntdll, "NtWriteVirtualMemory" ) );
		const auto nt_free = reinterpret_cast< pfn_nt_free_virtual_memory >( GetProcAddress( ntdll, "NtFreeVirtualMemory" ) );
		const auto nt_protect = reinterpret_cast< pfn_nt_protect_virtual_memory >( GetProcAddress( ntdll, "NtProtectVirtualMemory" ) );

		// ---- copy the DLL to a randomized temp path (path/name disguise) ----
		wchar_t temp_dir[ MAX_PATH ]{};
		if ( !GetTempPathW( MAX_PATH, temp_dir ) )
		{
			wcscpy_s( temp_dir, L"C:\\Windows\\Temp\\" );
		}

		wchar_t disguised[MAX_PATH]{};
		swprintf_s( disguised, L"%s%lu%lu.dll", temp_dir, GetTickCount( ) % 90000 + 10000, GetCurrentProcessId( ) % 1000 );

		{
			std::ifstream in( dll_path, std::ios::binary | std::ios::ate );
			if ( in.is_open( ) )
			{
				const auto size = static_cast< std::size_t >( in.tellg( ) );
				std::vector< BYTE > buffer( size );
				in.seekg( 0 );
				in.read( reinterpret_cast< char* >( buffer.data( ) ), size );
				in.close( );

				std::ofstream out( disguised, std::ios::binary | std::ios::trunc );
				if ( out.is_open( ) )
				{
					out.write( reinterpret_cast< const char* >( buffer.data( ) ), size );
					out.close( );
					fake_file_timestamp( disguised );
					RtlSecureZeroMemory( buffer.data( ), buffer.size( ) );
				}
			}
		}

		random_sleep( 100, 300 );

		// ---- open the process with minimal rights ----
		const auto process = OpenProcess( PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid );
		if ( !process )
		{
			set_status( L"打开进程失败：" + last_error_message( GetLastError( ) ), false );
			DeleteFileW( disguised );
			return false;
		}

		const auto remote_size = static_cast< SIZE_T >( ( wcslen( disguised ) + 1 ) * sizeof( wchar_t ) );

		PVOID remote_buffer = nullptr;
		SIZE_T region_size = remote_size;
		auto status = use_syscall
			? reinterpret_cast< pfn_nt_allocate_virtual_memory >( s_alloc.fn )( process, &remote_buffer, 0, &region_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE )
			: nt_alloc( process, &remote_buffer, 0, &region_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );

		if ( status != 0 || !remote_buffer )
		{
			set_status( L"远程内存分配失败", false );
			CloseHandle( process );
			DeleteFileW( disguised );
			return false;
		}

		SIZE_T written = 0;
		status = use_syscall
			? reinterpret_cast< pfn_nt_write_virtual_memory >( s_write.fn )( process, remote_buffer, const_cast< wchar_t* >( disguised ), remote_size, &written )
			: nt_write( process, remote_buffer, const_cast< wchar_t* >( disguised ), remote_size, &written );

		if ( status != 0 )
		{
			set_status( L"写入远程内存失败", false );
			( use_syscall ? reinterpret_cast< pfn_nt_free_virtual_memory >( s_free.fn ) : nt_free )( process, &remote_buffer, &region_size, MEM_RELEASE );
			CloseHandle( process );
			DeleteFileW( disguised );
			return false;
		}

		const auto kernel32 = GetModuleHandleW( L"kernel32.dll" );
		const auto load_library = reinterpret_cast< LPTHREAD_START_ROUTINE >( GetProcAddress( kernel32, "LoadLibraryW" ) );

		// ---- create the remote thread hidden from debuggers ----
		HANDLE thread = nullptr;
		status = use_syscall
			? reinterpret_cast< pfn_nt_create_thread_ex >( s_thread.fn )( &thread, THREAD_ALL_ACCESS, nullptr, process, load_library, remote_buffer, k_thread_hide_from_debugger, 0, 0, 0, nullptr )
			: nt_thread( &thread, THREAD_ALL_ACCESS, nullptr, process, load_library, remote_buffer, k_thread_hide_from_debugger, 0, 0, 0, nullptr );

		if ( status != 0 || !thread )
		{
			thread = CreateRemoteThread( process, nullptr, 0, load_library, remote_buffer, 0, nullptr );
		}

		if ( !thread )
		{
			set_status( L"创建远程线程失败：" + last_error_message( GetLastError( ) ), false );
			( use_syscall ? reinterpret_cast< pfn_nt_free_virtual_memory >( s_free.fn ) : nt_free )( process, &remote_buffer, &region_size, MEM_RELEASE );
			CloseHandle( process );
			DeleteFileW( disguised );
			return false;
		}

		WaitForSingleObject( thread, 15000 );

		DWORD module{ 0 };
		GetExitCodeThread( thread, &module );

		CloseHandle( thread );
		( use_syscall ? reinterpret_cast< pfn_nt_free_virtual_memory >( s_free.fn ) : nt_free )( process, &remote_buffer, &region_size, MEM_RELEASE );

		if ( module == 0 )
		{
			set_status( L"注入失败：LoadLibraryW 返回空", false );
			CloseHandle( process );
			DeleteFileW( disguised );
			return false;
		}

		// ---- resolve the loaded base (LDR first, Toolhelp fallback) ----
		std::wstring file_name = disguised;
		const auto slash = file_name.find_last_of( L"\\/" );
		if ( slash != std::wstring::npos )
		{
			file_name = file_name.substr( slash + 1 );
		}

		// LoadLibraryW returned the module base directly; the LDR walk is a
		// fallback for shells that relocate themselves.
		auto base = static_cast< ULONG_PTR >( module );
		if ( !base )
		{
			base = find_module_base_ldr( process, base );
		}
		if ( !base )
		{
			base = find_module_base_toolhelp( pid, file_name );
		}

		// ---- wait for the module to finish initializing, then clean up ----
		Sleep( 3000 );

		if ( base )
		{
			hide_module_ldr( process, base );
		}

		// Conservative memory hygiene: flip explicit RWX code sections to RX.
		if ( base )
		{
			std::ifstream in( disguised, std::ios::binary | std::ios::ate );
			if ( in.is_open( ) )
			{
				const auto size = static_cast< std::size_t >( in.tellg( ) );
				std::vector< BYTE > pe( size );
				in.seekg( 0 );
				in.read( reinterpret_cast< char* >( pe.data( ) ), size );
				in.close( );

				if ( pe.size( ) > sizeof( IMAGE_DOS_HEADER ) )
				{
					const auto dos = reinterpret_cast< IMAGE_DOS_HEADER* >( pe.data( ) );
					const auto nt = reinterpret_cast< IMAGE_NT_HEADERS* >( pe.data( ) + dos->e_lfanew );
					if ( dos->e_magic == IMAGE_DOS_SIGNATURE && nt->Signature == IMAGE_NT_SIGNATURE )
					{
						auto section = IMAGE_FIRST_SECTION( nt );
						for ( auto i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section )
						{
							const auto chars = section->Characteristics;
							if ( ( chars & IMAGE_SCN_MEM_EXECUTE ) && ( chars & IMAGE_SCN_MEM_WRITE ) )
							{
								auto addr = reinterpret_cast< BYTE* >( base ) + section->VirtualAddress;
								auto size_section = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
								PVOID target = addr;
								SIZE_T target_size = size_section;
								DWORD old_protect = 0;
								( use_syscall ? reinterpret_cast< pfn_nt_protect_virtual_memory >( s_protect.fn ) : nt_protect )( process, &target, &target_size, PAGE_EXECUTE_READ, &old_protect );
							}
						}
					}
				}

				RtlSecureZeroMemory( pe.data( ), pe.size( ) );
			}
		}

		CloseHandle( process );

		Sleep( 1000 );
		DeleteFileW( disguised );

		wchar_t message[ 128 ]{};
		swprintf_s( message, L"注入成功 0x%p（PID %lu）", reinterpret_cast< void* >( module ), pid );
		set_status( message );

		// Auto-close shortly after a successful injection.
		SetTimer( g_window, 1, 1500, nullptr );
		return true;
	}

	std::wstring get_default_dll_path( )
	{
		wchar_t module_path[ MAX_PATH ]{};
		GetModuleFileNameW( nullptr, module_path, MAX_PATH );

		std::wstring path = module_path;
		const auto slash = path.find_last_of( L"\\/" );
		if ( slash != std::wstring::npos )
		{
			path = path.substr( 0, slash + 1 );
		}

#if defined( DEV )
		path += L"velocity-cs2-dev.dll";
#else
		path += L"cs2.dll";
#endif
		return path;
	}

	void browse_for_dll( )
	{
		wchar_t buffer[ MAX_PATH ]{};
		wcscpy_s( buffer, g_dll_path.c_str( ) );

		OPENFILENAMEW dialog{};
		dialog.lStructSize = sizeof( dialog );
		dialog.hwndOwner = g_window;
		dialog.lpstrFilter = L"velocity DLL (*.dll)\0*.dll\0所有文件 (*.*)\0*.*\0";
		dialog.lpstrFile = buffer;
		dialog.nMaxFile = MAX_PATH;
		dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

		if ( GetOpenFileNameW( &dialog ) )
		{
			g_dll_path = buffer;
			InvalidateRect( g_window, &g_dll_rect, TRUE );
		}
	}

	bool g_injecting{};

	bool perform_inject( )
	{
		if ( !g_target_valid )
		{
			set_status( L"未检测到 cs2.exe", false );
			g_injecting = false;
			return false;
		}

		if ( g_dll_path.empty( ) )
		{
			set_status( L"未设置 DLL 路径", false );
			g_injecting = false;
			return false;
		}

		if ( GetFileAttributesW( g_dll_path.c_str( ) ) == INVALID_FILE_ATTRIBUTES )
		{
			set_status( L"DLL 文件不存在：" + g_dll_path, false );
			g_injecting = false;
			return false;
		}

		if ( !inject_dll( g_target.pid, g_dll_path ) )
		{
			g_injecting = false;
			return false;
		}

		return true;
	}

	void on_inject( )
	{
		if ( g_injecting )
		{
			return;
		}

		g_injecting = true;

		if ( g_target_valid )
		{
			perform_inject( );
			return;
		}

		// No CS2 running: launch it through Steam and wait for it to show up,
		// then auto-inject a few seconds later.
		ShellExecuteW( nullptr, L"open", L"steam://rungameid/730", nullptr, nullptr, SW_SHOWNORMAL );
		set_status( L"正在启动 CS2..." );
		SetTimer( g_window, 2, 1000, nullptr );
	}

	// --- GDI+ drawing helpers -------------------------------------------------

	void add_round_rect( GraphicsPath& path, float x, float y, float w, float h, float r )
	{
		if ( r > w / 2.0f )
		{
			r = w / 2.0f;
		}
		if ( r > h / 2.0f )
		{
			r = h / 2.0f;
		}

		path.AddArc( x, y, r * 2.0f, r * 2.0f, 180.0f, 90.0f );
		path.AddArc( x + w - r * 2.0f, y, r * 2.0f, r * 2.0f, 270.0f, 90.0f );
		path.AddArc( x + w - r * 2.0f, y + h - r * 2.0f, r * 2.0f, r * 2.0f, 0.0f, 90.0f );
		path.AddArc( x, y + h - r * 2.0f, r * 2.0f, r * 2.0f, 90.0f, 90.0f );
		path.CloseFigure( );
	}

	void fill_round( Graphics& g, const RECT& rect, COLORREF color, float radius )
	{
		GraphicsPath path;
		add_round_rect( path, static_cast< float >( rect.left ), static_cast< float >( rect.top ),
			static_cast< float >( rect.right - rect.left ), static_cast< float >( rect.bottom - rect.top ), radius );
		SolidBrush brush( make_color( color ) );
		g.FillPath( &brush, &path );
	}

	void frame_round( Graphics& g, const RECT& rect, COLORREF color, float radius )
	{
		GraphicsPath path;
		add_round_rect( path, static_cast< float >( rect.left ) + 0.5f, static_cast< float >( rect.top ) + 0.5f,
			static_cast< float >( rect.right - rect.left ) - 1.0f, static_cast< float >( rect.bottom - rect.top ) - 1.0f, radius );
		Pen pen( make_color( color ), 1.0f );
		g.DrawPath( &pen, &path );
	}

	void draw_text( Graphics& g, const RECT& rect, const std::wstring& text, COLORREF color, float size, bool bold = false, bool center = false )
	{
		if ( text.empty( ) )
		{
			return;
		}

		Gdiplus::FontFamily family( L"Microsoft YaHei UI" );
		Gdiplus::Font font( &family, size, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPixel );

		Gdiplus::StringFormat format;
		format.SetAlignment( center ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear );
		format.SetLineAlignment( Gdiplus::StringAlignmentCenter );
		format.SetTrimming( Gdiplus::StringTrimmingEllipsisCharacter );
		format.SetFormatFlags( Gdiplus::StringFormatFlagsNoWrap );

		SolidBrush brush( make_color( color ) );
		const RectF area(
			static_cast< float >( rect.left ), static_cast< float >( rect.top ),
			static_cast< float >( rect.right - rect.left ), static_cast< float >( rect.bottom - rect.top ) );
		g.DrawString( text.c_str( ), static_cast< INT >( text.size( ) ), &font, area, &format, &brush );
	}

	void draw_button( Graphics& g, const RECT& rect, const wchar_t* label, int index )
	{
		const auto hovered = g_btn_hover[ index ];
		fill_round( g, rect, hovered ? k_bg_hover : k_bg_panel, 8.0f );
		frame_round( g, rect, hovered ? k_border_focus : k_border, 8.0f );
		draw_text( g, rect, label, hovered ? k_accent : k_text, 15.0f, false, true );
	}

	void draw_inject_button( Graphics& g )
	{
		const auto hovered = g_btn_hover[ 2 ];
		fill_round( g, g_inject_rect, hovered ? RGB( 190, 205, 255 ) : k_accent, 10.0f );
		draw_text( g, g_inject_rect, L"注 入", k_text_dark, 15.0f, true, true );
	}

	void draw_title_bar( Graphics& g )
	{
		draw_text( g, { 16, 0, g_close_rect.left - 8, k_title_height }, L"VELOCITY 注入器", k_accent, 19.0f, true );

		fill_round( g, g_close_rect, g_close_hover ? k_close_hover : RGB( 30, 30, 35 ), 8.0f );
		draw_text( g, g_close_rect, L"×", g_close_hover ? RGB( 255, 255, 255 ) : k_text_dim, 17.0f, false, true );

		Pen line_pen( make_color( k_border ), 1.0f );
		g.DrawLine( &line_pen, 16.0f, static_cast< float >( k_title_height ), static_cast< float >( k_width - 16 ), static_cast< float >( k_title_height ) );
	}

	void draw_target_panel( Graphics& g )
	{
		fill_round( g, g_target_rect, k_bg_panel, 10.0f );
		frame_round( g, g_target_rect, k_border, 10.0f );

		RECT left_rect{ g_target_rect.left + 14, g_target_rect.top, g_target_rect.left + 120, g_target_rect.bottom };
		RECT right_rect{ g_target_rect.left + 130, g_target_rect.top, g_target_rect.right - 14, g_target_rect.bottom };

		draw_text( g, left_rect, L"目标进程", k_text_dim, 13.0f );

		if ( g_target_valid )
		{
			wchar_t target[ 64 ]{};
			swprintf_s( target, L"%s  ·  PID %lu  ·  %s", g_target.name.c_str( ), g_target.pid, g_target.is_64bit ? L"x64" : L"x86" );
			draw_text( g, right_rect, target, k_accent, 14.0f, false, true );
		}
		else
		{
			draw_text( g, right_rect, L"未运行", RGB( 255, 120, 120 ), 14.0f, false, true );
		}
	}

	void draw_dll_input( Graphics& g )
	{
		fill_round( g, g_dll_rect, k_bg_panel, 8.0f );
		frame_round( g, g_dll_rect, g_dll_focused ? k_border_focus : k_border, 8.0f );

		RECT text_rect{ g_dll_rect.left + 10, g_dll_rect.top, g_dll_rect.right - 10, g_dll_rect.bottom };
		draw_text( g, text_rect, g_dll_path.empty( ) ? L"DLL 路径..." : g_dll_path, g_dll_path.empty( ) ? k_text_dim : k_text, 13.0f );
	}

	// --- Rendering -------------------------------------------------------------

	void render( )
	{
		Gdiplus::Bitmap bitmap( k_width, k_height, PixelFormat32bppPARGB );
		Graphics g( &bitmap );
		g.SetSmoothingMode( Gdiplus::SmoothingModeAntiAlias );
		g.SetTextRenderingHint( Gdiplus::TextRenderingHintAntiAliasGridFit );
		g.SetPixelOffsetMode( Gdiplus::PixelOffsetModeHalf );

		// Window background with rounded corners; corners stay transparent.
		GraphicsPath window_path;
		add_round_rect( window_path, 0, 0, static_cast< float >( k_width ), static_cast< float >( k_height ), 12.0f );
		SolidBrush bg_brush( make_color( k_bg ) );
		g.FillPath( &bg_brush, &window_path );

		draw_title_bar( g );
		draw_target_panel( g );
		draw_dll_input( g );
		draw_button( g, g_btn_rect[ 0 ], L"浏览", 0 );
		draw_button( g, g_btn_rect[ 1 ], L"刷新", 1 );
		draw_inject_button( g );

		if ( !g_status_text.empty( ) )
		{
			draw_text( g, g_status_rect, g_status_text, g_status_ok ? k_text_dim : RGB( 255, 120, 120 ), 13.0f, false, true );
		}

		HBITMAP hbitmap{ nullptr };
		if ( bitmap.GetHBITMAP( Color( 255, 0, 0, 0 ), &hbitmap ) != Gdiplus::Ok || !hbitmap )
		{
			return;
		}

		const auto mem_dc = CreateCompatibleDC( nullptr );
		const auto old_bitmap = static_cast< HBITMAP >( SelectObject( mem_dc, hbitmap ) );

		BLENDFUNCTION blend{};
		blend.BlendOp = AC_SRC_OVER;
		blend.SourceConstantAlpha = 255;
		blend.AlphaFormat = AC_SRC_ALPHA;

		const SIZE size{ k_width, k_height };
		const POINT source{ 0, 0 };
		UpdateLayeredWindow( g_window, nullptr, nullptr, const_cast< SIZE* >( &size ), mem_dc, const_cast< POINT* >( &source ), 0, &blend, ULW_ALPHA );

		SelectObject( mem_dc, old_bitmap );
		DeleteDC( mem_dc );
		DeleteObject( hbitmap );
	}

	// --- Layout / input ----------------------------------------------------------

	void layout( )
	{
		const auto w = k_width;

		g_title_rect = { 0, 0, w, k_title_height };
		g_close_rect = { w - k_close_size - 4, 4, w - 4, k_close_size + 4 };
		g_target_rect = { 14, k_title_height + 10, w - 14, k_title_height + 50 };
		g_dll_rect = { 14, k_title_height + 64, w - 200, k_title_height + 96 };
		g_btn_rect[ 0 ] = { w - 186, k_title_height + 64, w - 118, k_title_height + 96 };
		g_btn_rect[ 1 ] = { w - 108, k_title_height + 64, w - 14, k_title_height + 96 };
		g_inject_rect = { 14, k_title_height + 110, w - 14, k_title_height + 146 };
		g_status_rect = { 14, k_title_height + 154, w - 14, k_title_height + 186 };
	}

	void update_mouse( POINT pt, bool down )
	{
		POINT client_pt = pt;
		ScreenToClient( g_window, &client_pt );

		if ( PtInRect( &g_dll_rect, client_pt ) && down )
		{
			g_dll_focused = true;
			SetFocus( g_window );
			InvalidateRect( g_window, nullptr, TRUE );
		}
		else if ( down )
		{
			g_dll_focused = false;
			InvalidateRect( g_window, nullptr, TRUE );
		}

		bool hover[ 3 ]{ false, false, false };
		for ( auto i = 0; i < 3; ++i )
		{
			hover[ i ] = PtInRect( &g_btn_rect[ i ], client_pt );
		}

		const auto close_hover = PtInRect( &g_close_rect, client_pt );

		if ( hover[ 0 ] != g_btn_hover[ 0 ] || hover[ 1 ] != g_btn_hover[ 1 ] || hover[ 2 ] != g_btn_hover[ 2 ] || close_hover != g_close_hover )
		{
			for ( auto i = 0; i < 3; ++i )
			{
				g_btn_hover[ i ] = hover[ i ];
			}
			g_close_hover = close_hover;
			InvalidateRect( g_window, nullptr, TRUE );
		}
	}

	LRESULT CALLBACK window_proc( HWND window, UINT message, WPARAM wparam, LPARAM lparam )
	{
		switch ( message )
		{
		case WM_CREATE:
		{
			g_dll_path = get_default_dll_path( );
			refresh_processes( );
			layout( );
			return 0;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps{};
			BeginPaint( window, &ps );
			EndPaint( window, &ps );
			render( );
			return 0;
		}

		case WM_ERASEBKGND:
			return 1;

		case WM_MOUSEMOVE:
		{
			update_mouse( { GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) }, false );
			break;
		}

		case WM_LBUTTONDOWN:
		{
			const auto pt = POINT{ GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) };

			if ( PtInRect( &g_close_rect, pt ) )
			{
				DestroyWindow( window );
			}
			else if ( PtInRect( &g_btn_rect[ 0 ], pt ) )
			{
				browse_for_dll( );
			}
			else if ( PtInRect( &g_btn_rect[ 1 ], pt ) )
			{
				refresh_processes( );
			}
			else if ( PtInRect( &g_inject_rect, pt ) )
			{
				on_inject( );
			}
			else if ( PtInRect( &g_title_rect, pt ) )
			{
				ReleaseCapture( );
				SendMessageW( window, WM_NCLBUTTONDOWN, HTCAPTION, 0 );
			}
			else
			{
				update_mouse( { GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) }, true );
			}
			return 0;
		}

		case WM_CHAR:
			if ( g_dll_focused )
			{
				if ( wparam == 8 )
				{
					if ( !g_dll_path.empty( ) )
					{
						g_dll_path.pop_back( );
						InvalidateRect( window, nullptr, TRUE );
					}
				}
				else if ( wparam >= 32 )
				{
					g_dll_path.push_back( static_cast< wchar_t >( wparam ) );
					InvalidateRect( window, nullptr, TRUE );
				}
				return 0;
			}
			break;

		case WM_KEYDOWN:
			if ( wparam == VK_RETURN )
			{
				on_inject( );
				return 0;
			}
			break;

		case WM_TIMER:
			if ( wparam == 1 )
			{
				KillTimer( window, 1 );
				DestroyWindow( window );
			}
			else if ( wparam == 2 )
			{
				refresh_processes( );
				if ( g_target_valid )
				{
					KillTimer( window, 2 );
					set_status( L"检测到 CS2，10 秒后自动注入..." );
					SetTimer( window, 3, 10000, nullptr );
				}
			}
			else if ( wparam == 3 )
			{
				KillTimer( window, 3 );
				perform_inject( );
			}
			return 0;

		case WM_CLOSE:
			DestroyWindow( window );
			return 0;

		case WM_DESTROY:
			PostQuitMessage( 0 );
			return 0;
		}

		return DefWindowProcW( window, message, wparam, lparam );
	}

} // namespace

int WINAPI wWinMain( HINSTANCE instance, HINSTANCE, PWSTR, int show_command )
{
	g_instance = instance;

	Gdiplus::GdiplusStartupInput gdiplus_input;
	Gdiplus::GdiplusStartup( &g_gdiplus_token, &gdiplus_input, nullptr );

	WNDCLASSEXW window_class{};
	window_class.cbSize = sizeof( window_class );
	window_class.hInstance = instance;
	window_class.lpfnWndProc = window_proc;
	window_class.lpszClassName = L"velocity_injector_window";
	window_class.hIcon = LoadIconW( nullptr, reinterpret_cast< LPCWSTR >( IDI_APPLICATION ) );
	window_class.hCursor = LoadCursorW( nullptr, reinterpret_cast< LPCWSTR >( IDC_ARROW ) );
	window_class.hbrBackground = nullptr;

	if ( !RegisterClassExW( &window_class ) )
	{
		MessageBoxW( nullptr, L"窗口类注册失败", L"velocity 注入器", MB_ICONERROR );
		Gdiplus::GdiplusShutdown( g_gdiplus_token );
		return 1;
	}

	const auto screen_w = GetSystemMetrics( SM_CXSCREEN );
	const auto screen_h = GetSystemMetrics( SM_CYSCREEN );
	const auto window_x = std::max( 0, ( screen_w - k_width ) / 2 );
	const auto window_y = std::max( 0, ( screen_h - k_height ) / 2 );

	const auto window = CreateWindowExW(
		WS_EX_LAYERED | WS_EX_APPWINDOW, window_class.lpszClassName, L"velocity 注入器",
		WS_POPUP,
		window_x, window_y, k_width, k_height, nullptr, nullptr, instance, nullptr );

	if ( !window )
	{
		MessageBoxW( nullptr, L"窗口创建失败", L"velocity 注入器", MB_ICONERROR );
		Gdiplus::GdiplusShutdown( g_gdiplus_token );
		return 1;
	}

	g_window = window;

	render( );
	ShowWindow( window, show_command );
	UpdateWindow( window );

	MSG message{};
	while ( GetMessageW( &message, nullptr, 0, 0 ) > 0 )
	{
		TranslateMessage( &message );
		DispatchMessageW( &message );
	}

	Gdiplus::GdiplusShutdown( g_gdiplus_token );

	return static_cast< int >( message.wParam );
}
