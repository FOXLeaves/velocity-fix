#ifndef PROTECTION_INL
#define PROTECTION_INL

#include "../protection.hpp"

namespace protection {
	PROTECTION_INLINE void protection::safe_memory_copy (void* dst, const void* src, std::size_t size) {
		const auto dst_ =
			reinterpret_cast <std::uint8_t*> (dst);
		const auto src_ =
			reinterpret_cast <const std::uint8_t*> (src);
		for (std::size_t i = 0; i < size; i++) {
			dst_ [i] = src_ [i];
		}
	}

	PROTECTION_INLINE void protection::crash () {
		*(int*) 0xdeadc0de = 0;
		// @todo: randomized context crash
	}

	template <std::size_t N> requires (N >= 0 && N < NUMBER_OF_CHECKS)
		PROTECTION_INLINE bool protection::check () {
		/* @todo: remove this just for test */
		return true;

		if constexpr (N == 0) {

			/* N == 0: KUSER_SHARED_DATA checks */
			constexpr unsigned char subtype =
				PROTECTION_RANDOM_KEY () % 3;

			if constexpr (subtype == 0) {
				return USER_SHARED_DATA->NtBuildNumber == m_data_.decrypt_value (m_data_.kusd.nt_build_number);
			} else if constexpr (subtype == 1) {
				return USER_SHARED_DATA->BootId == m_data_.decrypt_value (m_data_.kusd.boot_id);
			} else if constexpr (subtype == 2) {
				return USER_SHARED_DATA->Cookie == m_data_.decrypt_value (m_data_.kusd.cookie);
			}

		} else if constexpr (N == 1) {

			/* N == 1: TEB checks */
			constexpr unsigned char subtype =
				PROTECTION_RANDOM_KEY () % 3;

			if constexpr (subtype == 0) {
				return static_cast <std::uint32_t> (reinterpret_cast <std::uintptr_t>(NtCurrentTeb ()->ClientId.UniqueProcess))
					== m_data_.decrypt_value (m_data_.teb.process_id);
			} else if constexpr (subtype == 1) {
				return reinterpret_cast <std::uintptr_t>(NtCurrentTeb ()->ProcessEnvironmentBlock)
					== m_data_.decrypt_value (m_data_.teb.process_environment_block);
			} else if constexpr (subtype == 2) {
				return static_cast <std::uint32_t> (NtCurrentTeb ()->CurrentLocale) == m_data_.decrypt_value (m_data_.teb.current_locale);
			}

			return true;

		} else if constexpr (N == 2) {

			/* N == 2: PEB checks */
			constexpr unsigned char subtype =
				PROTECTION_RANDOM_KEY () % 3;

			if constexpr (subtype == 0) {
				return reinterpret_cast <std::uintptr_t>(NtCurrentPeb ()->ImageBaseAddress)
					== m_data_.decrypt_value (m_data_.peb.image_base_address);
			} else if constexpr (subtype == 1) {
				return NtCurrentPeb ()->OSBuildNumber
					== m_data_.decrypt_value (m_data_.peb.os_build_number);
			} else if constexpr (subtype == 2) {

				/* @todo: modules validation */
				// constexpr unsigned char random_module =
				// 	PROTECTION_RANDOM_KEY () % 3;
				// 
				// LDR_DATA_TABLE_ENTRY* entry = nullptr;
				// const auto ldr =
				// 	NtCurrentPeb ()->Ldr;
				// ldr->InLoadOrderModuleList.Flink;

				return true;

			}

		} else {
			static_assert(false, "unimplemented protection check");
			return false;
		}
	}

	PROTECTION_INLINE bool protection::check_debugger () {
		/* @info: bypass ScyllaHide instrumentation callback */
		const auto backup =
			NtCurrentTeb ()->InstrumentationCallbackDisabled;
		auto found = false;

		NtCurrentTeb ()->InstrumentationCallbackDisabled = TRUE;

		HANDLE debug_object {};
		const auto st =
			INLINE_SYSCALL_MANUAL (NtQueryInformationProcess, ::protection::NtQueryInformationProcess_) (
				NtCurrentProcess (),
				ProcessDebugObjectHandle,
				&debug_object,
				sizeof (debug_object),
				nullptr
			);
		if (NT_SUCCESS (st) && debug_object) {
			found = true;
		}

		NtCurrentTeb ()->InstrumentationCallbackDisabled = backup;

		return !found;
	}

	PROTECTION_INLINE bool protection::check_console () {
		return NtCurrentPeb ()->
			ProcessParameters->
			ConsoleHandle != nullptr;
	}
}

#endif // !PROTECTION_INL
