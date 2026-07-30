#pragma once

#include <algorithm>
#include <memory>

#include "protection_hash.hpp"

#ifndef PROTECTION_INLINE
#define PROTECTION_INLINE inline __forceinline
#endif // !PROTECTION_INLINE

#ifndef PROTECTION_CHECK
/* !!! INFO: Use protection checks only in virtualized code (code which doesnt affect performance) !!! */
#if !defined (DEV)
#define PROTECTION_CHECK() \
	( ::g_protection.check <PROTECTION_RANDOM_KEY ("CHECK") % ::protection::NUMBER_OF_CHECKS> () )
#else
#define PROTECTION_CHECK() (true)
#endif
#endif // !PROTECTION_CHECK


namespace protection {
	constexpr auto MAX_LICENSE_SIZE = 0x1000;
	constexpr auto NUMBER_OF_CHECKS = 3;

#pragma data_seg (".license")
	inline __declspec(allocate(".license")) unsigned char SECURITY_LICENSE [MAX_LICENSE_SIZE] = {
		0x13, 0x37
	};
#pragma data_seg ()

	enum syscall_ids {
		NtQueryInformationProcess_ = 0x0019
	};

	struct kusd_data_t {
		std::uint32_t nt_build_number;
		std::uint32_t boot_id;
		std::uint32_t cookie;
	};

	struct teb_data_t {
		std::uint32_t process_id;
		std::uintptr_t process_environment_block;
		std::uint32_t current_locale;
	};

	struct module_data_t {
		std::uintptr_t dll_base;
		std::uintptr_t entrypoint;
		std::uint64_t load_time;
	};

	struct peb_data_t {
		std::uintptr_t image_base_address;
		std::uint16_t os_build_number;

		/* @info: first 3 modules in process (ntdll, kernel32, kernelbase) */
		module_data_t modules [3];
	};

	struct window_data_t {
		std::uint32_t hwnd;
	};

	struct protection_data_t {
		kusd_data_t kusd;
		teb_data_t teb;
		peb_data_t peb;
		window_data_t window;

		PROTECTION_INLINE ~protection_data_t () {
			memset (this, 0x00, sizeof (*this));
		}

		template <typename T> requires (std::is_arithmetic_v <T>)
		PROTECTION_INLINE T decrypt_value (const T value) {
			// @todo: change to dynamic
			return value ^ 0xaaaaaaaaa;
		}
	};

	class protection {
	private:
		PROTECTION_INLINE void safe_memory_copy (void* dst, const void* src, std::size_t size);

	public:
		void attach (bool*);
		PROTECTION_INLINE void crash ();
		
		template <std::size_t N> requires (N >= 0 && N < NUMBER_OF_CHECKS)
			PROTECTION_INLINE bool check ();

		PROTECTION_INLINE bool check_debugger ();
		PROTECTION_INLINE bool check_console ();

	private:
		protection_data_t m_data_ {};
	};
}

inline auto g_protection = protection::protection {};

#include "impl/protection.inl"