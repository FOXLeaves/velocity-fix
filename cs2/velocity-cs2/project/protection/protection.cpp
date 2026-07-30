#include <pch/pch.hpp>
#include "protection.hpp"
#include <VMProtectSDK.h>

namespace protection {
	void protection::attach (bool* result) {
#if !defined (DEV)
		bool ok = true;
		VMProtectSerialNumberData data = {};

		ok &= VMProtectSetSerialNumber (reinterpret_cast <const char*>(SECURITY_LICENSE)) == 0;
		ok &= VMProtectGetSerialNumberData (&data, sizeof (data));

		auto protectionData =
			*reinterpret_cast <void**>(data.bUserData);

		safe_memory_copy (&m_data_, protectionData, sizeof (protection_data_t));
		INLINE_SYSCALL_MANUAL (NtUnmapViewOfSection, 0x002a) (NtCurrentProcess (), protectionData);

		*result = ok;
#endif
	}
}