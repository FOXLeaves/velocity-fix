#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>

#include "../../interfaces.hpp"

namespace interfaces {

	c_convar* c_engine_cvar::find(std::uint32_t name_hash)
	{
		if (!m_container)
			return 0;

		for (std::uint16_t current = 0; current != static_cast<std::uint16_t>(-1);)
		{
			const auto entry = m_container[current];

			if (entry.m_cvar)
			{
				if (fnv1a::runtime_hash(entry.m_cvar->m_name) == name_hash)
					return entry.m_cvar;
			}

			current = entry.m_next_index;
		}

		return 0;
	}

	bool c_engine_cvar::unlock_all()
	{
		constexpr std::int32_t FCVAR_HIDDEN = (1 << 4);
		constexpr std::int32_t FCVAR_DEVELOPMENTONLY = (1 << 1);

		for (std::uint16_t current = 0; current != static_cast<std::uint16_t>(-1); )
		{
			const auto entry = m_container[current];

			if (entry.m_cvar)
			{
				entry.m_cvar->m_flags &= ~FCVAR_HIDDEN;
				entry.m_cvar->m_flags &= ~FCVAR_DEVELOPMENTONLY;
			}

			current = entry.m_next_index;
		}

		return true;
	}

} // namespace interfaces