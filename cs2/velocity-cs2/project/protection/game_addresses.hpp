#ifndef GAME_ADDRESSES_HPP
#define GAME_ADDRESSES_HPP

#include <cstdint>
#include <cstddef>

/*
* @info
* All addresses have meta data like
* All patterns live in protection/patterns.hpp — update signatures there 1:1.
* PATTERN ( patterns::name )
* MODULE ( "MODULE" )
* INTERFACE_ ( "MODULE:INTERFACE_NAME" )
* CONVAR ( "CONVAR_NAME" )
*
* We dont need to store decrypted address, we need to use it in code
*
*
* for example:
*
* bad:
* detail::address_1 = PATTERN (patterns::name); then in another function call detail::address_1
* good:
* call (PATTERN (patterns::name))
*/

namespace protection::addresses {
	constexpr std::uint32_t hash_const (const char* str, std::uint32_t value = 0x811C9DC5u) {
		return *str ? hash_const (str + 1, (value ^ std::uint32_t (*str)) * 0x01000193u) : value;
	}

	template <std::size_t N>
	consteval std::uint32_t hash (const char (&str) [N]) {
		return hash_const (str);
	}

	template <std::size_t N>
	struct fixed_string {
		char value [N] {};

		constexpr fixed_string (const char (&str) [N]) {
			for (std::size_t i = 0; i < N; ++i)
				value [i] = str [i];
		}

		constexpr operator const char* () const {
			return value;
		}
	};

	template <std::size_t N>
	fixed_string (const char (&) [N]) -> fixed_string<N>;

	enum class address_type : std::uint32_t {
		pattern,
		interface_,
		convar,
		module_base,
		module_export
	};

	struct address_data_t {
		address_type type;
		const char* data;
	};

	union address_t {
		address_data_t data;
		struct {
			volatile std::uintptr_t encoded;
			volatile std::uint64_t  key;
		};

		constexpr address_t (std::uintptr_t addr = 0) : encoded (addr), key (0) {}
		constexpr address_t (address_type type, const char* str) : data {type, str} {}
	};

	__forceinline std::uintptr_t decode (const address_t& e) noexcept {
		return static_cast<std::uintptr_t>(e.encoded ^ e.key);
	}

	template <std::uint32_t Hash, address_type Type, fixed_string Str>
	struct address_holder {
		[[gnu::section("_addr"), gnu::used, gnu::retain]]
		inline static constexpr address_t entry {Type, Str.value};
	};

	struct sentinel_holder {
		[[gnu::section("_addr"), gnu::used, gnu::retain]]
		inline static constexpr address_t entry {};
	};

}

#define ADDRESS_IMPL(hash_value, addr_type, str_value) \
    (::protection::addresses ::address_holder<hash_value, addr_type, ::protection::addresses ::fixed_string{ str_value }>::entry)

#ifndef PATTERN
#if !defined(DEV)
#define PATTERN(entry)     ::protection::addresses::decode(entry)
#else
#define PATTERN(entry) \
    ([]() -> std::uintptr_t { \
        static const auto val = memory::resolve_pattern((entry).data.data); \
        return val; \
    }())
#endif
#endif

#ifndef INTERFACE_
#if !defined(DEV)
#define INTERFACE_(str)  ::protection::addresses::decode(ADDRESS_IMPL(::protection::addresses::hash(str), ::protection::addresses::address_type::interface_,  str))
#else
#define INTERFACE_(str) \
    []() -> std::uintptr_t { \
        static const auto val = memory::get_module_interface(str); \
        return val; \
    }()
#endif
#endif

#ifndef CONVAR
#if !defined (DEV)
#define CONVAR(str)       ::protection::addresses ::decode(ADDRESS_IMPL(::protection::addresses ::hash(str), ::protection::addresses ::address_type::convar,        str))
#else
#define CONVAR(str) \
    []() -> c_convar* { \
        static const auto val = addresses::globals::cvar->find(::protection::addresses::hash(str)); \
        return val; \
    }()
#endif
#endif

#ifndef MODULE_BASE
#if !defined(DEV)
#define MODULE_BASE(str)  ::protection::addresses::decode(ADDRESS_IMPL(::protection::addresses::hash(str), ::protection::addresses::address_type::module_base,   str))
#else
#define MODULE_BASE(str) \
    []() -> std::uintptr_t { \
        static const auto val = memory::get_module_base(str); \
        return val; \
    }()
#endif
#endif

#ifndef MODULE_EXPORT
#if !defined(DEV)
#define MODULE_EXPORT(str) ::protection::addresses::decode(ADDRESS_IMPL(::protection::addresses::hash(str), ::protection::addresses::address_type::module_export, str))
#else
#define MODULE_EXPORT(str) \
    ([]() -> std::uintptr_t { \
        static std::uintptr_t val{}; \
        if ( !val ) { \
            val = memory::get_module_export( str ); \
        } \
        return val; \
    }())
#endif
#endif

#include <protection/patterns.hpp>

#endif // !GAME_ADDRESSES_HPP