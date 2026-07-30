#pragma once

#include <algorithm>
#include <cstdint>

#define PROTECTION_STRINGIFY_IMPL(S) #S
#define PROTECTION_STRINGIFY(S) PROTECTION_STRINGIFY_IMPL(S)

#define PROTECTION_RANDOM_KEY(STR) \
	( ::protection::hash ( STR __FILE__ __TIME__ PROTECTION_STRINGIFY (__LINE__) ) + __COUNTER__ )	


namespace protection {
	using hash_t = std::uint64_t;

	inline constexpr hash_t basis = 0xe5e35c515b1cf64b;
	inline constexpr hash_t prime = 0x100000001b3;

	template <typename T> requires (std::is_same_v <T, char> || std::is_same_v <T, wchar_t>)
		consteval hash_t hash (const T* str, const hash_t key = basis) {
		return (*str == '\0') ? key : hash (&str [1], (key ^ static_cast <unsigned long long> (*str)) * 0x100000001b3);
	}

	template <typename T> requires (std::is_same_v <T, char> || std::is_same_v <T, wchar_t>)
		inline __forceinline hash_t hash_rt (const T* str) {

		hash_t key = basis;

		while (*str != T ('\0')) {
			key ^= static_cast <unsigned long long> (*str);
			key *= 0x100000001b3;

			++str;
		}

		return key;
	}
}