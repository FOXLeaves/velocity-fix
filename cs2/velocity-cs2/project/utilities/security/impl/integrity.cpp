#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <protection/game_addresses.hpp>
#include "../security.hpp"

namespace security::integrity {

	namespace detail {
		std::vector<cached_hash> cached_hashes {};

		static void cache_module (std::uintptr_t module_base) {
			if (!module_base) {
				return;
			}

			struct module_info {
				std::uint8_t pad0 [12];
				std::uint32_t crc32;
				std::uint32_t image_size;
				std::uint32_t entry_point;
				std::uint32_t timestamp;
				std::uint8_t pad1 [4];
				std::uint64_t image_base;
				char filename [260];
			};

			module_info info {};

			const auto result = memory::call<std::uint8_t> (PATTERN (patterns::analyze_pe_module), module_base, reinterpret_cast<std::uintptr_t>(&info), static_cast<char>(1));
			if (!result) {
				return;
			}

			cached_hash entry {};
			entry.module_base = module_base;
			entry.crc32 = info.crc32;
			entry.valid = true;

			cached_hashes.push_back (entry);
		}

	} // namespace detail

	bool initialize () {
		std::vector<std::pair<std::uintptr_t, std::wstring>> modules {};

		const auto peb = reinterpret_cast<PEB*>(__readgsqword (0x60));
		const auto ldr = peb->Ldr;
		const auto list = &ldr->InMemoryOrderModuleList;

		for (auto entry = list->Flink; entry != list; entry = entry->Flink) {
			const auto ldr_entry = CONTAINING_RECORD (entry, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
			const auto base = reinterpret_cast<std::uintptr_t>(ldr_entry->DllBase);

			if (base && ldr_entry->FullDllName.Buffer) {
#if defined ( DEV )
				// was causing issues when i tried injecting
				if (wcsstr (ldr_entry->FullDllName.Buffer, L"velocity-cs2-dev.dll"))
					continue;
#endif

				modules.emplace_back (base, ldr_entry->FullDllName.Buffer);
			}
		}

		if (modules.empty ()) {
			return false;
		}

		detail::cached_hashes.reserve (modules.size ());

		for (const auto& [base, path] : modules) {
			detail::cache_module (base);

			const auto cached = get (base);
			const auto last_slash = path.find_last_of (L'\\');
			const auto filename = (last_slash != std::wstring::npos) ? path.substr (last_slash + 1) : path;

			auto to_narrow = [] (const std::wstring& wide) -> std::string {
				if (wide.empty ()) return {};
				const int size = WideCharToMultiByte (CP_UTF8, 0, wide.data (), -1, nullptr, 0, nullptr, nullptr);
				std::string result (size - 1, '\0');
				WideCharToMultiByte (CP_UTF8, 0, wide.data (), -1, result.data (), size, nullptr, nullptr);
				return result;
			};

			const auto name = to_narrow (filename);
		}

		return !detail::cached_hashes.empty ();
	}

	cached_hash* get (std::uintptr_t module_base) {
		for (auto& entry : detail::cached_hashes) {
			if (entry.module_base == module_base) {
				return &entry;
			}
		}

		return nullptr;
	}

} // namespace security::integrity