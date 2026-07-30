#pragma once

#include <vector>
#include <span>
#include <fstream>
#include <unordered_map>

namespace rpack {

	namespace detail
	{
		inline constexpr std::uint32_t counter_base{ __COUNTER__ };
	}

#define RPACK_ID ( __COUNTER__ - ::rpack::detail::counter_base - 1 )

	namespace fonts::font7
	{
		inline constexpr std::uint32_t regular{ RPACK_ID };
	}

	namespace fonts::inter
	{
		inline constexpr std::uint32_t medium{ RPACK_ID };
		inline constexpr std::uint32_t bold{ RPACK_ID };
	}

	namespace fonts::pixel7
	{
		inline constexpr std::uint32_t smallest{ RPACK_ID };
	}

	namespace images::femboys
	{
		inline constexpr std::uint32_t furry_gif{ RPACK_ID };
	}

	namespace images::icons
	{
		inline constexpr std::uint32_t logo{ RPACK_ID };
		inline constexpr std::uint32_t user{ RPACK_ID };
		inline constexpr std::uint32_t users{ RPACK_ID };
		inline constexpr std::uint32_t cog{ RPACK_ID };
		inline constexpr std::uint32_t file{ RPACK_ID };
		inline constexpr std::uint32_t skull{ RPACK_ID };
		inline constexpr std::uint32_t weather{ RPACK_ID };
		inline constexpr std::uint32_t brush{ RPACK_ID };
		inline constexpr std::uint32_t crosshair{ RPACK_ID };
		inline constexpr std::uint32_t bars{ RPACK_ID };
		inline constexpr std::uint32_t fih{ RPACK_ID };
	}

	namespace particles::effects
	{
		inline constexpr std::uint32_t killstars{ RPACK_ID };
		inline constexpr std::uint32_t tracer{ RPACK_ID };
		inline constexpr std::uint32_t sparks{ RPACK_ID };
		inline constexpr std::uint32_t fade{ RPACK_ID };
		inline constexpr std::uint32_t halo{ RPACK_ID };
	}

	namespace particles::weather
	{
		inline constexpr std::uint32_t rain{ RPACK_ID };
		inline constexpr std::uint32_t snow{ RPACK_ID };
		inline constexpr std::uint32_t stars{ RPACK_ID };
	}

	namespace images::menu_decor
	{
		inline constexpr std::uint32_t konata_peek{ RPACK_ID };
	}

#undef RPACK_ID

	inline constexpr std::uint32_t k_magic{ 0x5250414B };
	inline constexpr std::uint8_t k_key[ ]{ 0x7A, 0x3F, 0x9E, 0x2D, 0x1C, 0x8B, 0x4A, 0x05, 0xE6, 0xC2, 0xD8, 0x49, 0xF1, 0xB7, 0x3A, 0x90, 0x5D, 0x2E, 0x8F, 0x6A, 0x3C, 0x1B, 0x90, 0x47, 0xA8, 0xF4, 0xE2, 0xC6, 0xD0, 0xB3, 0x91, 0x75 };

	namespace detail {

		class rc4
		{
		public:
			rc4( const std::uint8_t* key, std::size_t key_size )
			{
				for ( std::size_t i = 0; i < 256; ++i )
				{
					this->m_s[ i ] = static_cast< std::uint8_t >( i );
				}

				std::uint8_t j{ 0 };
				for ( std::size_t i = 0; i < 256; ++i )
				{
					j = j + this->m_s[ i ] + key[ i % key_size ];
					std::swap( this->m_s[ i ], this->m_s[ j ] );
				}

				this->m_i = 0;
				this->m_j = 0;
			}

			void crypt( std::uint8_t* data, std::size_t size )
			{
				for ( std::size_t n = 0; n < size; ++n )
				{
					this->m_i = this->m_i + 1;
					this->m_j = this->m_j + this->m_s[ this->m_i ];
					std::swap( this->m_s[ this->m_i ], this->m_s[ this->m_j ] );

					const auto k = this->m_s[ static_cast< std::uint8_t >( this->m_s[ this->m_i ] + this->m_s[ this->m_j ] ) ];
					data[ n ] ^= k;
				}
			}

		private:
			std::uint8_t m_s[ 256 ]{};
			std::uint8_t m_i{ 0 };
			std::uint8_t m_j{ 0 };
		};

	} // namespace detail

	class reader
	{
	public:
		reader( ) = delete;

		static bool load( const std::string& path )
		{
			std::ifstream file( path, std::ios::binary | std::ios::ate );
			if ( !file )
			{
				return false;
			}

			const auto file_size = static_cast< std::size_t >( file.tellg( ) );
			file.seekg( 0 );

			m_data.resize( file_size );
			if ( !file.read( reinterpret_cast< char* >( m_data.data( ) ), file_size ) )
			{
				return false;
			}

			return parse( );
		}

		static bool load( const void* data, std::size_t size )
		{
			m_data.assign( reinterpret_cast< const std::uint8_t* >( data ), reinterpret_cast< const std::uint8_t* >( data ) + size );
			return parse( );
		}

		static std::span<const std::uint8_t> get( std::uint32_t resource_id )
		{
			const auto it = m_entries.find( resource_id );
			if ( it == m_entries.end( ) )
			{
				return { };
			}

			return { m_data.data( ) + it->second.offset, it->second.size };
		}

		static const std::uint8_t* data( std::uint32_t resource_id )
		{
			const auto span = get( resource_id );
			return span.empty( ) ? nullptr : span.data( );
		}

		static std::size_t size( std::uint32_t resource_id )
		{
			return get( resource_id ).size( );
		}

		static bool has( std::uint32_t resource_id )
		{
			return m_entries.contains( resource_id );
		}

	private:
		static bool parse( )
		{
			if ( m_data.size( ) < 8 )
			{
				return false;
			}

			if ( *reinterpret_cast< std::uint32_t* >( m_data.data( ) ) != k_magic )
			{
				return false;
			}

			const auto count = *reinterpret_cast< std::uint32_t* >( m_data.data( ) + 4 );

			m_entries.clear( );
			m_entries.reserve( count );

			for ( std::uint32_t i = 0; i < count; ++i )
			{
				const auto toc_offset = 8 + i * 12;
				const auto id_val = *reinterpret_cast< std::uint32_t* >( m_data.data( ) + toc_offset );
				const auto offset = *reinterpret_cast< std::uint32_t* >( m_data.data( ) + toc_offset + 4 );
				const auto size = *reinterpret_cast< std::uint32_t* >( m_data.data( ) + toc_offset + 8 );

				m_entries[ id_val ] = { offset, size };

				detail::rc4 cipher( k_key, sizeof( k_key ) );
				cipher.crypt( m_data.data( ) + offset, size );
			}

			return true;
		}

		struct entry
		{
			std::uint32_t offset{ 0 };
			std::uint32_t size{ 0 };
		};

		inline static std::vector<std::uint8_t> m_data{};
		inline static std::unordered_map<std::uint32_t, entry> m_entries{};
	};

} // namespace rpack
