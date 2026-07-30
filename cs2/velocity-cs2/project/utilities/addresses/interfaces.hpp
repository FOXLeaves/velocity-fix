#pragma once

union cvvalue_t
{
	bool i1;
	short i16;
	int i32;
	int64_t i64;
	float fl;
	double db;
	const char* sz;
};

class c_convar
{
public:
	const char* m_name; // 0x0000
	c_convar* m_next; // 0x0008
	char _pad_01 [0x10]; // 0x0010
	const char* m_description; // 0x0020
	uint32_t m_type; // 0x28
	uint32_t m_registered; // 0x2C
	uint32_t m_flags; // 0x30
	char _pad_02 [0x24]; // 0x34
	cvvalue_t m_value; // 0x58
	cvvalue_t m_default_value; // 0x60

	template<typename T>
	T get() {
		if constexpr (std::is_same_v<T, bool>)
			return m_value.i1;
		else if constexpr (std::is_same_v<T, short>)
			return m_value.i16;
		else if constexpr (std::is_same_v<T, int>)
			return m_value.i32;
		else if constexpr (std::is_same_v<T, int64_t>)
			return m_value.i64;
		else if constexpr (std::is_same_v<T, float>)
			return m_value.fl;
		else
			return T {};
	}
};

namespace interfaces {

	class c_engine_cvar
	{
	public:
		struct cvar_container_t
		{
			c_convar* m_cvar;		// 0x000
			int16_t m_unknown;		// 0x008
			int16_t m_next_index;	// 0x00A
			int32_t m_unknown2;		// 0x00C
		};

		char _pad0 [0x48]; // 0x0000
		cvar_container_t* m_container; // 0x0048

		[[nodiscard]] c_convar* find (std::uint32_t name_hash);

		bool unlock_all ();
	};

}