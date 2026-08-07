#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/diag.hpp>
#include <utilities/logging/logging.hpp>
#include <protection/game_addresses.hpp>
#include "../systems.hpp"

namespace systems {

	void input::update( )
	{
		const auto local_controller = memory::read<std::uintptr_t>( addresses::globals::local_player_controller );
		if ( !local_controller )
		{
			this->m_current_cmd = nullptr;
			return;
		}

		this->m_current_cmd = this->get_current_cmd( local_controller );
	}

	void input::apply( )
	{
		const auto local = systems::g_local.get ();

		if ( !this->m_current_cmd ) {
			return;
		}

		const auto base = this->m_current_cmd->csgo_user_cmd.mutable_base( );
		if ( !base ) {
			return;
		}

		auto has_move_subticks = [] (proto::base_usercmd_pb* base_cmd) {
			// just use protobufs atp
			for (size_t i = 0; i < base_cmd->subtick_moves_size (); i++) {
				proto::subtick_move_step* step = base_cmd->mutable_subtick_moves (i);
				if (step->m_has_bits.test (0x8) || step->m_has_bits.test (0x10))
					return true;

				if (step->m_has_bits.test (0x1))
					return true;
			}

			return false;
		};

		// fix movement for ag2
		diag::set_exception_phase( "input apply: subtick movement" );
		if (!has_move_subticks (base)) {
			if (const auto step = systems::g_input.acquire_subtick_step (base->mutable_subtick_moves ())) {

				const auto movement_services = local.pawn ? memory::read<std::uintptr_t> (local.pawn + SCHEMA ("C_BasePlayerPawn", "m_pMovementServices"_hash)) : 0;
				if (movement_services) {
					step->set_button (0);
					step->set_pressed (false);
					step->set_when (0.0f);
					step->set_analog_forward_delta (base->forwardmove () - memory::read<float> (movement_services + SCHEMA ("CPlayer_MovementServices", "m_flCmdForwardMove"_hash)));
					step->set_analog_left_delta (base->leftmove () - memory::read<float> (movement_services + SCHEMA ("CPlayer_MovementServices", "m_flCmdLeftMove"_hash)));
				}
			}
		}

		diag::set_exception_phase( "input apply: buttons" );
		auto buttons = const_cast<proto::in_button_state_pb*>( base->buttons_pb( ) );
		if ( !buttons )
		{
			const auto raw_base =
				reinterpret_cast<std::uintptr_t>( base ) - proto::message_impl_offset;
			const auto arena_bits = memory::read<std::uintptr_t>( raw_base + 0x08 );
			auto arena = arena_bits & ~0x3ull;
			if ( arena_bits & 1 )
			{
				arena = memory::read<std::uintptr_t>( arena );
			}

			const auto raw_buttons =
				memory::call<void*>( PATTERN( patterns::button_state_alloc ), arena );
			if ( raw_buttons )
			{
				base->m_buttons_pb =
					reinterpret_cast<proto::in_button_state_pb*>( raw_buttons );
				buttons = proto::impl_ptr<proto::in_button_state_pb>( raw_buttons );
			}
		}

		if ( buttons )
		{
			base->m_has_bits.set( 0x2u );
			buttons->set_buttonstate1( this->m_current_cmd->buttons.value );
			buttons->set_buttonstate2( this->m_current_cmd->buttons.value_changed );
			buttons->set_buttonstate3( this->m_current_cmd->buttons.value_scroll );
		}

		diag::set_exception_phase( "input apply: crc" );
		this->calculate_crc( base );
	}

	input::usercmd* input::get_current_cmd( std::uintptr_t local_controller ) const
	{
		const auto get_usercmd_base = PATTERN (patterns::get_usercmd_base);
		const auto get_usercmd = PATTERN (patterns::get_usercmd);
		if ( !get_usercmd_base || !get_usercmd )
		{
			return nullptr;
		}

		const auto usercmd_base = memory::call<std::uintptr_t>( get_usercmd_base, local_controller );
		if ( !usercmd_base )
		{
			return nullptr;
		}

		const auto sequence = memory::safe_read<int>( usercmd_base + 0x5910 );
		if ( !sequence )
		{
			return nullptr;
		}

		return memory::call<usercmd*>( get_usercmd, local_controller, *sequence );
	}

	proto::subtick_move_step* input::acquire_subtick_step( proto::repeated_ptr_field<proto::subtick_move_step>* subtick_moves ) const
	{
		if ( !subtick_moves )
		{
			return nullptr;
		}

		proto::subtick_move_step* step{};

		if ( subtick_moves->m_rep )
		{
			if ( subtick_moves->m_current_size < subtick_moves->m_rep->allocated_size )
			{
				const auto element = subtick_moves->m_rep->elements[ subtick_moves->m_current_size ];
				if ( element )
				{
					subtick_moves->m_current_size++;
					step = proto::impl_ptr<proto::subtick_move_step>( element );

					// Reuse the pooled slot without zeroing it (matches
					// the engine's own pool behaviour - zeroing the
					// cached size/has bits made the server parse the
					// step differently and the input felt less smooth).
					// Only the view deltas are cleared explicitly: a
					// stale pitch/yaw delta on a jump/duck step would
					// rotate the view mid-tick.
					step->set_pitch_delta( 0.0f );
					step->set_yaw_delta( 0.0f );
				}
			}
		}

		if ( !step )
		{
			const auto move_step = memory::call<void*>(PATTERN (patterns::subtick_move_alloc), subtick_moves->m_arena );
			if ( move_step )
			{
				memory::call<std::uintptr_t>(PATTERN (patterns::utl_vector_push), reinterpret_cast< std::uintptr_t >( subtick_moves ), reinterpret_cast< std::uintptr_t >( move_step ) );
				step = proto::impl_ptr<proto::subtick_move_step>( move_step );
				step->set_pitch_delta( 0.0f );
				step->set_yaw_delta( 0.0f );
			}
		}

		if ( step )
		{
			// Invalidate the base command's serialization cache: protobuf
			// uses the cached size to skip recomputation, and a stale
			// cache would leave the freshly queued step out of the bytes
			// the server parses - the input then feels rough/janky.
			const auto base = reinterpret_cast< proto::base_usercmd_pb* >(
				reinterpret_cast< std::uintptr_t >( subtick_moves ) - offsetof( proto::base_usercmd_pb, m_subtick_moves ) );
			if ( base )
			{
				base->m_cached_size = 0;
			}
		}

		return step;
	}

	math::vector3 input::get_view_angles( ) const
	{
		return *memory::call<math::vector3*>(PATTERN (patterns::get_view_angles), addresses::globals::csgo_input, 0 );
	}

	// CSGOInputHistoryEntryPB::New fingerprint (FVA-verified): prologue
	// `48 89 5C 24 10 57 48 83` and `mov ecx, 0x78` at +0x14 (120-byte
	// allocation). The E8-relative pattern can drift onto a different
	// T::New after a depot update; calling the wrong allocator corrupts
	// the protobuf Rep and crashes inside client.dll (FVA: REFUSE rather
	// than risk it). POD-only so __try is legal.
	[[nodiscard]] static bool history_alloc_fingerprint( std::uintptr_t candidate )
	{
		if ( !candidate )
		{
			return false;
		}

		constexpr std::uint8_t prologue[ 8 ] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83 };
		constexpr std::uint8_t size_imm[ 5 ] = { 0xB9, 0x78, 0x00, 0x00, 0x00 };

		__try
		{
			const auto* p = reinterpret_cast<const std::uint8_t*>( candidate );
			for ( auto i = 0; i < 8; ++i )
			{
				if ( p[ i ] != prologue[ i ] )
				{
					return false;
				}
			}

			for ( auto i = 0; i < 5; ++i )
			{
				if ( p[ 0x14 + i ] != size_imm[ i ] )
				{
					return false;
				}
			}

			return true;
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			return false;
		}
	}

	[[nodiscard]] static std::uintptr_t verified_history_alloc( )
	{
		static std::atomic<std::uintptr_t> cached{ 1 };

		auto fn = cached.load( std::memory_order_acquire );
		if ( fn != 1 )
		{
			return fn;
		}

		const auto candidate = PATTERN( patterns::history_field_alloc );
		const auto verified = history_alloc_fingerprint( candidate ) ? candidate : std::uintptr_t{ 0 };
		cached.store( verified, std::memory_order_release );
		return verified;
	}

	// SEH guards live in their own POD-only functions: MSVC refuses __try
	// in any function that also holds unwindable C++ locals.
	[[nodiscard]] static void* call_history_alloc( std::uintptr_t fn, std::uintptr_t arena )
	{
		void* raw{ nullptr };
		__try
		{
			raw = memory::call<void*>( fn, arena );
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			return nullptr;
		}
		return raw;
	}

	[[nodiscard]] static bool call_vector_push( std::uintptr_t fn, std::uintptr_t field, std::uintptr_t raw )
	{
		__try
		{
			memory::call<std::uintptr_t>( fn, field, raw );
			return true;
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			return false;
		}
	}

	proto::input_history_entry* input::push_input_history( usercmd* cmd, const input_history_params& params ) const
	{
		auto history_field = cmd->csgo_user_cmd.mutable_input_history( );
		if ( !history_field )
		{
			return nullptr;
		}

		proto::input_history_entry* entry{ nullptr };

		if ( history_field->m_rep )
		{
			if ( history_field->m_current_size < history_field->m_rep->allocated_size )
			{
				auto element = history_field->m_rep->elements[ history_field->m_current_size ];
				if ( element )
				{
					history_field->m_current_size++;
					entry = proto::impl_ptr<proto::input_history_entry>( element );
				}
			}
		}

		if ( !entry )
		{
			// The engine arena field can carry a tag bit (FVA:
			// `and rcx, ~3; test 1; mov rcx, [rcx]`). Handing the raw
			// tagged value to the engine allocator made it dereference a
			// bogus address inside client.dll (read of 0x10), crashing on
			// the vac-bypass tick path. Untag first, then bail out if the
			// arena still looks unusable - a skipped history entry is far
			// safer than an engine-side fault.
			const auto alloc = verified_history_alloc( );
			if ( !alloc )
			{
				logging::console::print_severity( 2, xs( "[push] history entry allocator unresolved (fingerprint mismatch) - entry skipped" ) );
				return nullptr;
			}

			const auto arena_raw = reinterpret_cast<std::uintptr_t>( history_field->m_arena );
			auto arena = arena_raw & ~std::uintptr_t{ 3 };
			if ( arena_raw & 1 )
			{
				const auto indirection = memory::safe_read<std::uintptr_t>( arena );
				if ( !indirection )
				{
					logging::console::print_severity( 2, xs( "[push] arena indirection null (raw={:#x}) - entry skipped" ), arena_raw );
					return nullptr;
				}

				arena = *indirection;
			}

			// A null arena is legal: protobuf's T::New(arena) falls back to
			// the heap allocator when the arena is null - only tagged
			// pointers (handled above) dereference garbage.
			auto raw = call_history_alloc( alloc, arena );
			if ( !raw )
			{
				logging::console::print_severity( 2, xs( "[push] engine allocator call failed - entry skipped" ) );
				return nullptr;
			}

			const auto push_fn = PATTERN( patterns::utl_vector_push );
			if ( !push_fn )
			{
				logging::console::print_severity( 2, xs( "[push] utl_vector_push pattern unresolved - entry skipped" ) );
				return nullptr;
			}

			if ( !call_vector_push( push_fn, reinterpret_cast< std::uintptr_t >( history_field ), reinterpret_cast< std::uintptr_t >( raw ) ) )
			{
				logging::console::print_severity( 2, xs( "[push] vector push call faulted - entry skipped" ) );
				return nullptr;
			}

			entry = proto::impl_ptr<proto::input_history_entry>( raw );
		}

		if ( !entry )
		{
			return nullptr;
		}

		entry->set_render_tick_count( params.render_tick );
		entry->set_render_tick_fraction( params.render_frac );
		entry->set_player_tick_count( params.player_tick );
		entry->set_player_tick_fraction( params.player_frac );
		entry->set_frame_number( params.frame_number );
		entry->set_target_ent_index( params.target_ent_index );

		if ( auto va = entry->mutable_view_angles( ) )
		{
			va->set_x( params.view_angles.x );
			va->set_y( params.view_angles.y );
			va->set_z( params.view_angles.z );
		}

		if ( auto sp = entry->mutable_shoot_position( ) )
		{
			sp->set_x( params.shoot_position.x );
			sp->set_y( params.shoot_position.y );
			sp->set_z( params.shoot_position.z );
		}

		if ( auto ci = entry->mutable_cl_interp( ) )
		{
			ci->set_frac( params.cl_interp_frac );
		}

		if ( auto si0 = entry->mutable_sv_interp0( ) )
		{
			si0->set_src_tick( params.sv_interp0_src );
			si0->set_dst_tick( params.sv_interp0_dst );
			si0->set_frac( params.sv_interp0_frac );
		}

		if ( auto si1 = entry->mutable_sv_interp1( ) )
		{
			si1->set_src_tick( params.sv_interp1_src );
			si1->set_dst_tick( params.sv_interp1_dst );
			si1->set_frac( params.sv_interp1_frac );
		}

		if ( auto pi = entry->mutable_player_interp( ) )
		{
			pi->set_src_tick( params.player_interp_src );
			pi->set_dst_tick( params.player_interp_dst );
			pi->set_frac( params.player_interp_frac );
		}

		if ( params.fill_cheat_check_data )
		{
			if ( auto head = entry->mutable_target_head_pos_check( ) )
			{
				head->set_x( params.target_head_pos.x );
				head->set_y( params.target_head_pos.y );
				head->set_z( params.target_head_pos.z );
			}

			if ( auto abs_pos = entry->mutable_target_abs_pos_check( ) )
			{
				abs_pos->set_x( params.target_abs_pos.x );
				abs_pos->set_y( params.target_abs_pos.y );
				abs_pos->set_z( params.target_abs_pos.z );
			}

			if ( auto abs_ang = entry->mutable_target_abs_ang_check( ) )
			{
				abs_ang->set_x( params.target_abs_ang.x );
				abs_ang->set_y( params.target_abs_ang.y );
				abs_ang->set_z( params.target_abs_ang.z );
			}
		}

		return entry;
	}

	void input::set_view_angles( const math::vector3& angles ) const
	{
		memory::call<void>(PATTERN (patterns::set_view_angles), addresses::globals::csgo_input, 0, &angles );
	}

	void input::desubtick( usercmd* cmd ) const
	{
		if ( const auto base = cmd->csgo_user_cmd.mutable_base( ) )
		{
			if ( auto subtick_moves = base->mutable_subtick_moves( ) )
			{
				subtick_moves->clear( );
			}
		}
	}

	void input::set_weapon_select( usercmd* cmd, std::uintptr_t csgo_input ) const
	{
		const auto frame_data = csgo_input + 552;
		const auto weapon_handle = memory::read<uint32_t>( frame_data + 1132 );

		if ( !weapon_handle )
		{
			return;
		}

		const auto weapon = systems::g_entities.lookup( weapon_handle );
		if ( !weapon )
		{
			return;
		}

		auto entity_index{ -1 };
		memory::call<int*>(PATTERN (patterns::weapon_get_entity_index), weapon, &entity_index );

		if ( entity_index == -1 )
		{
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( base )
		{
			base->set_weaponselect( entity_index );
		}
	}

	input::usercmd* input::get_command_by_sequence( std::uintptr_t local_controller, int sequence ) const
	{
		const auto get_usercmd_base = PATTERN (patterns::get_usercmd_base);
		if ( !get_usercmd_base )
		{
			return nullptr;
		}

		const auto usercmd_base = memory::call<std::uintptr_t>( get_usercmd_base, local_controller );
		if ( !usercmd_base )
		{
			return nullptr;
		}

		const auto index = sequence % 150;
		return reinterpret_cast< usercmd* >( usercmd_base + sizeof( usercmd ) * index );
	}

	bool input::is_subtick_overwrite( usercmd* cmd ) const
	{
		return memory::read<int>( reinterpret_cast< std::uintptr_t >( cmd ) + 148 ) == 2;
	}

	bool input::calculate_crc( proto::base_usercmd_pb* base ) const
	{
		if ( !base )
		{
			return false;
		}

		const auto string_copy = PATTERN( patterns::string_copy );
		const auto serialize_move_crc = PATTERN( patterns::serialize_move_crc );
		if ( !string_copy || !serialize_move_crc )
		{
			diag::write( diag::level::error, "input CRC helpers unavailable; capturing a pre-termination diagnostic dump" );
			diag::capture_snapshot( "input CRC helpers unavailable" );
			return false;
		}

		const auto btns = base->buttons_pb( );
		const auto va = base->viewangles( );

		std::uint8_t buf[ 64 ]{};
		std::uint8_t btn_size{ 0 };

		auto p = buf;

		auto bs1 = btns ? btns->buttonstate1( ) : 0;
		auto bs2 = btns ? btns->buttonstate2( ) : 0;
		auto bs3 = btns ? btns->buttonstate3( ) : 0;

		if ( bs1 ) { btn_size += 9; }
		if ( bs2 ) { btn_size += 9; }
		if ( bs3 ) { btn_size += 9; }

		if ( btn_size > 0 )
		{
			*p++ = 0x1a;
			*p++ = btn_size;

			if ( bs1 ) { *p++ = 0x09; std::memcpy( p, &bs1, 8 ); p += 8; }
			if ( bs2 ) { *p++ = 0x11; std::memcpy( p, &bs2, 8 ); p += 8; }
			if ( bs3 ) { *p++ = 0x19; std::memcpy( p, &bs3, 8 ); p += 8; }
		}

		auto pitch = va ? va->x( ) : 0.0f;
		auto yaw = va ? va->y( ) : 0.0f;
		auto roll = va ? va->z( ) : 0.0f;

		std::uint8_t va_size{ 0 };
		if ( pitch != 0.0f ) { va_size += 5; }
		if ( yaw != 0.0f ) { va_size += 5; }
		if ( roll != 0.0f ) { va_size += 5; }

		if ( va_size > 0 )
		{
			*p++ = 0x22;
			*p++ = va_size;

			if ( pitch != 0.0f ) { *p++ = 0x0d; std::memcpy( p, &pitch, 4 ); p += 4; }
			if ( yaw != 0.0f ) { *p++ = 0x15; std::memcpy( p, &yaw, 4 ); p += 4; }
			if ( roll != 0.0f ) { *p++ = 0x1d; std::memcpy( p, &roll, 4 ); p += 4; }
		}

		auto total = static_cast< int >( p - buf );

		base->m_has_bits.set( 0x1u );

		auto raw_msg = reinterpret_cast< std::uintptr_t >( base ) - proto::message_impl_offset;
		auto arena_raw = memory::read<std::uintptr_t>( raw_msg + 0x08 );
		auto arena = arena_raw & ~0x3ull;

		if ( arena_raw & 1 )
		{
			arena = memory::read<std::uintptr_t>( arena );
		}

		std::uint8_t msg[ 0x18 ]{};
		memory::call<void>( string_copy, reinterpret_cast< std::uintptr_t >( msg ), reinterpret_cast< std::uintptr_t >( buf ), total );
		memory::call<void>( serialize_move_crc, &base->m_move_crc, reinterpret_cast< std::uintptr_t >( msg ), arena );

		return true;
	}

} // namespace systems
