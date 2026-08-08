#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::movement {

	namespace {

		constexpr auto k_move_epsilon{ 1.0e-6f };
		constexpr auto k_when_epsilon{ 1.0e-5f };
		constexpr auto k_quantize_epsilon{ 1.0e-4f };
		constexpr auto k_max_subtick_moves{ 32 };
		constexpr auto k_movement_mask = static_cast<std::uintptr_t>(
			cstypes::command_buttons::in_forward
			| cstypes::command_buttons::in_back
			| cstypes::command_buttons::in_moveleft
			| cstypes::command_buttons::in_moveright );

		[[nodiscard]] math::vector2 limit_move( math::vector2 move )
		{
			if ( !std::isfinite( move.x ) || !std::isfinite( move.y ) )
			{
				return {};
			}

			// Preserve direction and all usable magnitude. Normalizing every
			// diagonal to length 1 loses 29% of the input; component-clamping
			// distorts the direction. Scale only when the rotated vector falls
			// outside the protocol's [-1, 1] square.
			const auto peak = std::fmaxf( std::fabsf( move.x ), std::fabsf( move.y ) );
			if ( peak > 1.0f )
			{
				move /= peak;
			}

			return move;
		}

		// Re-express the same world-space wish vector from source_yaw's
		// forward/left basis in target_yaw's basis.
		[[nodiscard]] math::vector2 reframe_move( const math::vector2& move, float source_yaw, float target_yaw )
		{
			if ( !std::isfinite( source_yaw ) || !std::isfinite( target_yaw ) )
			{
				return {};
			}

			const auto delta = std::remainderf( target_yaw - source_yaw, 360.0f )
				* ( std::numbers::pi_v<float> / 180.0f );
			const auto c = std::cosf( delta );
			const auto s = std::sinf( delta );

			return limit_move( {
				move.x * c + move.y * s,
				move.y * c - move.x * s
			} );
		}

		[[nodiscard]] bool meaningful_delta( const math::vector2& delta )
		{
			return std::fabsf( delta.x ) > k_move_epsilon
				|| std::fabsf( delta.y ) > k_move_epsilon;
		}

		[[nodiscard]] std::optional<math::vector2> decode_persisted_impulses(
			const math::vector2& impulses,
			math::vector3 old_forward,
			math::vector3 old_left,
			float source_yaw )
		{
			old_forward.z = 0.0f;
			old_left.z = 0.0f;
			const auto forward_len = old_forward.length_2d( );
			const auto left_len = old_left.length_2d( );
			if ( !std::isfinite( forward_len ) || !std::isfinite( left_len )
				|| forward_len < 0.5f || left_len < 0.5f )
			{
				return std::nullopt;
			}

			old_forward /= forward_len;
			old_left /= left_len;
			if ( std::fabsf( old_forward.dot( old_left ) ) > 0.25f )
			{
				return std::nullopt;
			}

			// Some schema/helper dumps label the perpendicular basis as right
			// even though m_flCmdLeftMove uses a positive-left convention. Make
			// the handedness explicit before reconstructing the world vector.
			const math::vector3 expected_left{ -old_forward.y, old_forward.x, 0.0f };
			const auto handedness = old_left.dot( expected_left );
			if ( !std::isfinite( handedness ) || std::fabsf( handedness ) < 0.5f )
			{
				return std::nullopt;
			}
			if ( handedness < 0.0f )
			{
				old_left *= -1.0f;
			}

			const auto world = old_forward * impulses.x + old_left * impulses.y;
			const auto yaw = source_yaw * ( std::numbers::pi_v<float> / 180.0f );
			const math::vector3 source_forward{ std::cosf( yaw ), std::sinf( yaw ), 0.0f };
			const math::vector3 source_left{ -std::sinf( yaw ), std::cosf( yaw ), 0.0f };

			return limit_move( {
				source_forward.dot( world ),
				source_left.dot( world )
			} );
		}

		[[nodiscard]] math::vector2 read_analog_delta( const proto::subtick_move_step* step )
		{
			if ( !step )
			{
				return {};
			}

			const auto forward = step->m_has_bits.test( 0x8u ) ? step->analog_forward_delta( ) : 0.0f;
			const auto side = step->m_has_bits.test( 0x10u ) ? step->analog_left_delta( ) : 0.0f;

			return {
				std::isfinite( forward ) ? forward : 0.0f,
				std::isfinite( side ) ? side : 0.0f
			};
		}

		void write_analog_delta( proto::subtick_move_step* step, const math::vector2& delta )
		{
			const auto forward = std::isfinite( delta.x ) ? delta.x : 0.0f;
			const auto left = std::isfinite( delta.y ) ? delta.y : 0.0f;

			step->m_analog_forward_delta = forward;
			step->m_analog_left_delta = left;

			if ( std::fabsf( forward ) > k_move_epsilon )
			{
				step->m_has_bits.set( 0x8u );
			}
			else
			{
				step->m_has_bits.clear( 0x8u );
			}

			if ( std::fabsf( left ) > k_move_epsilon )
			{
				step->m_has_bits.set( 0x10u );
			}
			else
			{
				step->m_has_bits.clear( 0x10u );
			}

			step->m_cached_size = 0;
		}

		void write_yaw_delta( proto::subtick_move_step* step, float delta )
		{
			const auto value = std::isfinite( delta ) ? std::remainderf( delta, 360.0f ) : 0.0f;
			step->m_yaw_delta = value;
			if ( std::fabsf( value ) > k_move_epsilon )
			{
				step->m_has_bits.set( 0x40u );
			}
			else
			{
				step->m_has_bits.clear( 0x40u );
			}
			step->m_cached_size = 0;
		}

		[[nodiscard]] float final_subtick_yaw( proto::base_usercmd_pb* base, float yaw )
		{
			for ( auto i = 0; i < base->subtick_moves_size( ); ++i )
			{
				const auto step = base->mutable_subtick_moves( i );
				if ( step && step->m_has_bits.test( 0x40u ) && std::isfinite( step->yaw_delta( ) ) )
				{
					yaw += step->yaw_delta( );
				}
			}

			return std::remainderf( yaw, 360.0f );
		}

		[[nodiscard]] float step_when( const proto::subtick_move_step* step )
		{
			if ( !step || !step->m_has_bits.test( 0x4u ) || !std::isfinite( step->when( ) ) )
			{
				return 0.0f;
			}

			return std::clamp( step->when( ), 0.0f, 1.0f );
		}

		void stable_sort_subticks( proto::repeated_ptr_field<proto::subtick_move_step>* moves )
		{
			if ( !moves || !moves->m_rep )
			{
				return;
			}

			// The list is tiny (normally <= 32), so an allocation-free stable
			// insertion sort is cheaper and keeps equal-time button/yaw events
			// in the order their owning feature emitted them.
			auto elements = moves->m_rep->elements;
			for ( auto i = 1; i < moves->size( ); ++i )
			{
				const auto current = elements[ i ];
				const auto current_when = step_when( proto::impl_ptr<proto::subtick_move_step>( current ) );
				auto j = i;
				while ( j > 0
					&& step_when( proto::impl_ptr<proto::subtick_move_step>( elements[ j - 1 ] ) ) > current_when )
				{
					elements[ j ] = elements[ j - 1 ];
					--j;
				}
				elements[ j ] = current;
			}
		}

		[[nodiscard]] proto::subtick_move_step* prepend_anchor( proto::repeated_ptr_field<proto::subtick_move_step>* moves )
		{
			if ( !moves || moves->size( ) >= k_max_subtick_moves )
			{
				return nullptr;
			}

			const auto old_size = moves->size( );
			auto anchor = systems::g_input.acquire_subtick_step( moves );
			if ( !anchor || !moves->m_rep || moves->size( ) != old_size + 1 )
			{
				return nullptr;
			}

			// acquire_subtick_step appends. Move only the protobuf pointers so
			// the time-zero rebase is serialized before every existing event;
			// the arena-owned messages themselves never move in memory.
			auto elements = moves->m_rep->elements;
			const auto raw_anchor = elements[ old_size ];
			std::move_backward( elements, elements + old_size, elements + old_size + 1 );
			elements[ 0 ] = raw_anchor;

			anchor->set_when( 0.0f );
			return anchor;
		}

		[[nodiscard]] bool install_quantized_yaw_frame(
			proto::base_usercmd_pb* base,
			proto::repeated_ptr_field<proto::subtick_move_step>* moves,
			float source_yaw,
			float target_yaw,
			float source_terminal_yaw,
			proto::subtick_move_step* preferred_anchor,
			bool reverse )
		{
			if ( !base || !moves )
			{
				return false;
			}

			// Fractional forward/left components are rounded when movement input
			// quantization is enabled. Keep the original (usually -1/0/1) axes
			// and temporarily put the server's movement frame back on source_yaw.
			const auto restore_delta = std::remainderf( target_yaw - source_terminal_yaw, 360.0f ) * ( reverse ? -1.0f : 1.0f );

			proto::subtick_move_step* anchor{ preferred_anchor };
			if ( anchor )
			{
				auto found{ false };
				for ( auto i = 0; i < moves->size( ); ++i )
				{
					if ( moves->mutable_at( i ) == anchor )
					{
						found = true;
						break;
					}
				}

				const auto when = anchor->m_has_bits.test( 0x4u ) ? anchor->when( ) : 1.0f;
				if ( !found || !std::isfinite( when ) || when > k_when_epsilon )
				{
					return false;
				}
			}
			else
			{
				for ( auto i = 0; i < moves->size( ); ++i )
				{
					auto step = moves->mutable_at( i );
					const auto when = step && step->m_has_bits.test( 0x4u ) ? step->when( ) : 0.0f;
					if ( step && ( !std::isfinite( when ) || when <= k_when_epsilon ) )
					{
						anchor = step;
						break;
					}
				}
			}

			const auto needs_restore = std::fabsf( restore_delta ) > k_move_epsilon;
			const auto required_slots = ( anchor ? 0 : 1 ) + ( needs_restore ? 1 : 0 );
			if ( moves->size( ) > k_max_subtick_moves - required_slots )
			{
				return false;
			}

			if ( !anchor )
			{
				anchor = prepend_anchor( moves );
			}
			if ( !anchor )
			{
				return false;
			}

			proto::subtick_move_step* restore{};
			if ( needs_restore )
			{
				const auto old_size = moves->size( );
				restore = systems::g_input.acquire_subtick_step( moves );
				if ( !restore || moves->size( ) != old_size + 1
					|| moves->mutable_at( old_size ) != restore )
				{
					// The newly created anchor is still a harmless time-zero no-op;
					// the caller can safely fall back to analog rotation.
					return false;
				}
				restore->set_when( std::nextafter( 1.0f, 0.0f ) );
			}

			const auto original_anchor_yaw = anchor->m_has_bits.test( 0x40u )
				&& std::isfinite( anchor->yaw_delta( ) )
				? anchor->yaw_delta( )
				: 0.0f;
			write_yaw_delta(
				anchor,
				original_anchor_yaw + std::remainderf( source_yaw - target_yaw, 360.0f ) * ( reverse ? -1.0f : 1.0f ) );
			if ( restore )
			{
				write_yaw_delta( restore, restore_delta );
			}

			base->m_cached_size = 0;
			return true;
		}

	} // namespace

	bool movement_fix::claim_quantized_trajectory(
		proto::base_usercmd_pb* base,
		proto::subtick_move_step* const* steps,
		int step_count )
	{
		if ( !this->m_active
			|| this->m_quantized_trajectory_claimed
			|| !base
			|| !steps
			|| step_count <= 0
			|| step_count > k_max_quantized_trajectory_steps )
		{
			return false;
		}

		const auto moves = base->mutable_subtick_moves( );
		if ( !moves || moves->size( ) < step_count )
		{
			return false;
		}

		auto trajectory_state = this->m_source_impulses;
		const auto discrete_axis = []( float value )
		{
			const auto rounded = std::roundf( value );
			return std::isfinite( value )
				&& std::fabsf( value - rounded ) <= k_quantize_epsilon
				&& std::fabsf( rounded ) <= 1.0f;
		};

		for ( auto i = 0; i < step_count; ++i )
		{
			const auto step = steps[ i ];
			if ( !step )
			{
				return false;
			}

			for ( auto previous = 0; previous < i; ++previous )
			{
				if ( steps[ previous ] == step )
				{
					return false;
				}
			}

			auto found{ false };
			for ( auto move_index = 0; move_index < moves->size( ); ++move_index )
			{
				if ( moves->mutable_at( move_index ) == step )
				{
					found = true;
					break;
				}
			}
			if ( !found )
			{
				return false;
			}

			const auto presence = step->m_has_bits.bits[ 0 ] & 0x7fu;
			const auto allowed_presence = 0x4u | 0x8u | 0x10u | 0x40u;
			const auto expected_when = static_cast<float>( i ) / static_cast<float>( step_count );
			if ( ( presence & 0x4u ) == 0
				|| ( presence & ~allowed_presence ) != 0
				|| !std::isfinite( step->when( ) )
				|| std::fabsf( step->when( ) - expected_when ) > k_when_epsilon
				|| ( ( presence & 0x8u ) != 0 && !std::isfinite( step->analog_forward_delta( ) ) )
				|| ( ( presence & 0x10u ) != 0 && !std::isfinite( step->analog_left_delta( ) ) )
				|| ( ( presence & 0x40u ) != 0 && !std::isfinite( step->yaw_delta( ) ) ) )
			{
				return false;
			}

			const auto analog_delta = read_analog_delta( step );
			trajectory_state += analog_delta;
			if ( !discrete_axis( trajectory_state.x ) || !discrete_axis( trajectory_state.y ) )
			{
				return false;
			}

			auto& claim = this->m_quantized_trajectory[ i ];
			claim.step = step;
			claim.presence = presence;
			claim.when = step->when( );
			claim.forward_delta = analog_delta.x;
			claim.left_delta = analog_delta.y;
			claim.yaw_delta = ( presence & 0x40u ) != 0 ? step->yaw_delta( ) : 0.0f;
		}

		this->m_quantized_trajectory_base = base;
		this->m_quantized_trajectory_count = step_count;
		this->m_quantized_trajectory_claimed = true;
		return true;
	}

	void movement_fix::begin( systems::input::usercmd* cmd )
	{
		this->m_active = false;
		this->m_previous_buttons_valid = false;
		this->m_quantized_trajectory_base = nullptr;
		this->m_quantized_trajectory_count = 0;
		this->m_quantized_trajectory_claimed = false;
		for ( auto& claim : this->m_quantized_trajectory )
		{
			claim = {};
		}

		const auto base = cmd ? cmd->csgo_user_cmd.mutable_base( ) : nullptr;
		const auto angles = base ? base->viewangles( ) : nullptr;
		if ( !base || !angles || !std::isfinite( angles->y( ) ) )
		{
			return;
		}

		const auto& prestate = systems::g_prediction.pre( );
		this->m_wire_impulses = {
			std::isfinite( prestate.last_movement_impulses.x ) ? prestate.last_movement_impulses.x : 0.0f,
			std::isfinite( prestate.last_movement_impulses.y ) ? prestate.last_movement_impulses.y : 0.0f
		};
		this->m_source_impulses = limit_move( this->m_wire_impulses );
		this->m_source_yaw = std::remainderf( angles->y( ), 360.0f );

		const auto pawn = systems::g_local.get( ).pawn;
		const auto command_number = cmd->command_number;
		const auto load_previous_buttons = [ & ](
			const command_frame* frames,
			std::intptr_t previous_number )
		{
			if ( !frames || pawn == 0 || previous_number < 0 )
			{
				return false;
			}

			const auto& previous = frames[ previous_number % k_command_history_size ];
			if ( previous.command_number == previous_number && previous.pawn == pawn )
			{
				this->m_previous_buttons = previous.movement_buttons;
				this->m_previous_buttons_valid = true;
				return true;
			}
			return false;
		};

		const auto has_legacy_number = base->m_has_bits.test( 0x10u );
		if ( has_legacy_number && base->legacy_command_number( ) > 0 )
		{
			load_previous_buttons(
				this->m_legacy_command_frames,
				static_cast<std::intptr_t>( base->legacy_command_number( ) ) - 1 );
		}
		if ( !this->m_previous_buttons_valid && command_number > 0 )
		{
			load_previous_buttons( this->m_command_frames, command_number - 1 );
		}

		// m_flCmd* and m_vecForward/m_vecLeft are captured from the same
		// MovementServices state. Never reject that processed basis merely
		// because local command generation is several ticks ahead during a
		// fast spin.
		const command_frame* processed_frame{};
		if ( prestate.last_command_number_processed > 0 )
		{
			const auto processed = static_cast<std::intptr_t>( prestate.last_command_number_processed );
			const auto find_processed = [ & ]( const command_frame* frames ) -> const command_frame*
			{
				const auto& frame = frames[ processed % k_command_history_size ];
				return frame.command_number == processed && frame.pawn == pawn ? &frame : nullptr;
			};
			processed_frame = find_processed( this->m_legacy_command_frames );
			if ( !processed_frame )
			{
				processed_frame = find_processed( this->m_command_frames );
			}
		}

		const auto decoded = decode_persisted_impulses(
			this->m_wire_impulses,
			prestate.last_movement_forward,
			prestate.last_movement_left,
			this->m_source_yaw );

		if ( processed_frame && processed_frame->quantized_payload )
		{
			// A quantized command keeps its scalar payload in the terminal
			// movement frame, then restores anti-aim yaw at the end of the tick.
			// MovementServices may expose that final view basis, so the tagged
			// command history is authoritative for this special representation.
			this->m_source_impulses = reframe_move(
				this->m_wire_impulses,
				processed_frame->yaw,
				this->m_source_yaw );
		}
		else if ( decoded )
		{
			// MovementServices exposes the exact basis paired with m_flCmd*;
			// use it instead of guessing that the persisted scalars already
			// belong to this command's view frame.
			this->m_source_impulses = *decoded;
		}
		else if ( processed_frame )
		{
			// Only fall back to a yaw that belongs to the command the movement
			// service actually processed. A single "last generated yaw" is wrong
			// whenever prediction/choke lags behind high-speed rotation.
			this->m_source_impulses = reframe_move(
				this->m_wire_impulses,
				processed_frame->yaw,
				this->m_source_yaw );
		}

		this->m_active = true;
	}

	void movement_fix::finish( systems::input::usercmd* cmd )
	{
		const auto base = cmd ? cmd->csgo_user_cmd.mutable_base( ) : nullptr;
		const auto angles = base ? base->viewangles( ) : nullptr;
		if ( !this->m_active || !base || !angles || !std::isfinite( angles->y( ) ) )
		{
			this->m_active = false;
			this->m_previous_buttons_valid = false;
			return;
		}

		auto moves = base->mutable_subtick_moves( );
		stable_sort_subticks( moves );

		const auto clear_quantized_claim = [ & ]
		{
			this->m_quantized_trajectory_base = nullptr;
			this->m_quantized_trajectory_count = 0;
			this->m_quantized_trajectory_claimed = false;
		};
		const auto is_claimed_step = [ & ]( const proto::subtick_move_step* step )
		{
			if ( !this->m_quantized_trajectory_claimed || !step )
			{
				return false;
			}

			for ( auto i = 0; i < this->m_quantized_trajectory_count; ++i )
			{
				if ( this->m_quantized_trajectory[ i ].step == step )
				{
					return true;
				}
			}
			return false;
		};
		const auto remove_claimed_trajectory = [ & ]
		{
			if ( moves && moves->m_rep && this->m_quantized_trajectory_claimed )
			{
				auto elements = moves->m_rep->elements;
				auto write_index{ 0 };
				for ( auto read_index = 0; read_index < moves->size( ); ++read_index )
				{
					const auto step = moves->mutable_at( read_index );
					if ( !is_claimed_step( step ) )
					{
						elements[ write_index++ ] = elements[ read_index ];
					}
				}
				moves->m_current_size = write_index;
				base->m_cached_size = 0;
			}
			clear_quantized_claim( );
			stable_sort_subticks( moves );
		};

		const auto target_yaw = std::remainderf( angles->y( ), 360.0f );
		const auto yaw_delta_raw = std::remainderf( target_yaw - this->m_source_yaw, 360.0f );
		const auto source_base = limit_move( { base->forwardmove( ), base->leftmove( ) } );
		auto source_terminal_yaw = final_subtick_yaw( base, this->m_source_yaw );

		const auto pawn = systems::g_local.get( ).pawn;
		const auto move_type = pawn
			? memory::read<std::uint8_t>( pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) )
			: cstypes::move_type::none;
		const auto uses_3d_movement = move_type == cstypes::move_type::ladder
			|| move_type == cstypes::move_type::noclip
			|| move_type == cstypes::move_type::observer;

		// Grounded moves resolve against the opposite rotation direction:
		// the field-tested result is a full reversal on the ground only
		// (air movement is unaffected). Flip the frame pair for grounded
		// commands so the world-space wish comes out on the player's
		// intent instead of mirrored.
		const auto on_ground = ( systems::g_prediction.pre( ).flags & cstypes::entity_flags::on_ground ) != 0;
		const auto ground_flip = on_ground && !uses_3d_movement;
		const auto yaw_delta = ground_flip ? -yaw_delta_raw : yaw_delta_raw;
		const auto frame_changed = !uses_3d_movement && std::fabsf( yaw_delta_raw ) > k_move_epsilon;
		auto movement_source_yaw = uses_3d_movement ? target_yaw : this->m_source_yaw;
		// Frame-reframing helper with the grounded reversal applied at every
		// conversion point (base fields and analog trajectory alike).
		const auto reframe = [ & ]( const math::vector2& move, float source_yaw, float target_yaw )
		{
			if ( ground_flip )
			{
				return reframe_move( move, target_yaw, source_yaw );
			}
			return reframe_move( move, source_yaw, target_yaw );
		};
		auto target_base = reframe( source_base, movement_source_yaw, target_yaw );
		const auto quantize_cvar = CONVAR( "sv_quantize_movement_input" );
		const auto quantized_input = quantize_cvar && quantize_cvar->get<bool>( );
		const auto subtick_view_cvar = CONVAR( "sv_subtick_movement_view_angles" );
		const auto subtick_movement_view = subtick_view_cvar && subtick_view_cvar->get<bool>( );
		const auto discrete_axis = []( float value )
		{
			const auto rounded = std::roundf( value );
			return std::isfinite( value )
				&& std::fabsf( value - rounded ) <= k_quantize_epsilon
				&& std::fabsf( rounded ) <= 1.0f;
		};
		const auto discrete_source_move = meaningful_delta( source_base )
			&& discrete_axis( source_base.x )
			&& discrete_axis( source_base.y );
		const auto attack_mask = static_cast<std::uintptr_t>(
			cstypes::command_buttons::in_attack | cstypes::command_buttons::in_second_attack );
		auto firing = ( ( cmd->buttons.value
			| cmd->buttons.value_changed
			| cmd->buttons.value_scroll ) & attack_mask ) != 0
			|| features::combat::g_rage.is_firing_this_tick( )
			|| ( cmd->csgo_user_cmd.m_has_bits.test( 0x20u )
				&& cmd->csgo_user_cmd.attack1_start_history_index( ) >= 0 )
			|| ( cmd->csgo_user_cmd.m_has_bits.test( 0x40u )
				&& cmd->csgo_user_cmd.attack2_start_history_index( ) >= 0 );
		if ( !firing && moves )
		{
			for ( auto i = 0; i < moves->size( ); ++i )
			{
				const auto step = moves->mutable_at( i );
				if ( step
					&& step->m_has_bits.test( 0x1u )
					&& ( step->button( ) & attack_mask ) != 0 )
				{
					firing = true;
					break;
				}
			}
		}

		auto owned_trajectory_valid = this->m_quantized_trajectory_claimed
			&& this->m_quantized_trajectory_base == base
			&& this->m_quantized_trajectory_count > 0
			&& moves
			&& moves->size( ) <= k_max_subtick_moves - 1
			&& quantized_input
			&& subtick_movement_view
			&& !firing
			&& !ground_flip && !uses_3d_movement;
		if ( owned_trajectory_valid )
		{
			owned_trajectory_valid = moves->size( ) > 0
				&& moves->mutable_at( 0 ) == this->m_quantized_trajectory[ 0 ].step;
			auto trajectory_state = this->m_source_impulses;
			auto previous_position{ -1 };

			for ( auto move_index = 0; owned_trajectory_valid && move_index < moves->size( ); ++move_index )
			{
				const auto step = moves->mutable_at( move_index );
				if ( !step )
				{
					owned_trajectory_valid = false;
					break;
				}

				if ( !is_claimed_step( step ) )
				{
					const auto presence = step->m_has_bits.bits[ 0 ] & 0x7fu;
					const auto when_valid = ( presence & 0x4u ) == 0
						|| ( std::isfinite( step->when( ) ) && step->when( ) >= 0.0f && step->when( ) <= 1.0f );
					if ( !when_valid || ( presence & ( 0x8u | 0x10u | 0x40u ) ) != 0 )
					{
						owned_trajectory_valid = false;
					}
					continue;
				}

				trajectory_state += read_analog_delta( step );
				if ( !discrete_axis( trajectory_state.x ) || !discrete_axis( trajectory_state.y ) )
				{
					owned_trajectory_valid = false;
				}
			}

			for ( auto claim_index = 0;
				owned_trajectory_valid && claim_index < this->m_quantized_trajectory_count;
				++claim_index )
			{
				const auto& claim = this->m_quantized_trajectory[ claim_index ];
				auto position{ -1 };
				for ( auto move_index = 0; move_index < moves->size( ); ++move_index )
				{
					if ( moves->mutable_at( move_index ) == claim.step )
					{
						position = move_index;
						break;
					}
				}

				if ( position <= previous_position )
				{
					owned_trajectory_valid = false;
					break;
				}
				previous_position = position;

				const auto presence = claim.step->m_has_bits.bits[ 0 ] & 0x7fu;
				const auto analog_delta = read_analog_delta( claim.step );
				const auto current_yaw_delta = ( presence & 0x40u ) != 0
					? claim.step->yaw_delta( )
					: 0.0f;
				if ( presence != claim.presence
					|| !std::isfinite( claim.step->when( ) )
					|| std::fabsf( claim.step->when( ) - claim.when ) > k_when_epsilon
					|| std::fabsf( analog_delta.x - claim.forward_delta ) > k_move_epsilon
					|| std::fabsf( analog_delta.y - claim.left_delta ) > k_move_epsilon
					|| !std::isfinite( current_yaw_delta )
					|| std::fabsf( std::remainderf( current_yaw_delta - claim.yaw_delta, 360.0f ) ) > k_move_epsilon )
				{
					owned_trajectory_valid = false;
				}
			}

			if ( meaningful_delta( limit_move( trajectory_state ) - source_base ) )
			{
				owned_trajectory_valid = false;
			}
		}

		if ( this->m_quantized_trajectory_claimed && !owned_trajectory_valid )
		{
			// A late shot, extra feature event, base-only overwrite or allocation
			// pressure invalidates the whole source-relative trajectory. Remove its
			// protobuf entries before falling back to ordinary movement correction.
			remove_claimed_trajectory( );
			source_terminal_yaw = final_subtick_yaw( base, this->m_source_yaw );
		}

		const auto contains_analog_trajectory = [ & ]
		{
			if ( !moves )
			{
				return false;
			}
			for ( auto i = 0; i < moves->size( ); ++i )
			{
				if ( meaningful_delta( read_analog_delta( moves->mutable_at( i ) ) ) )
				{
					return true;
				}
			}
			return false;
		};

		auto quantized_yaw_frame{ false };
		if ( owned_trajectory_valid )
		{
			quantized_yaw_frame = install_quantized_yaw_frame(
				base,
				moves,
				this->m_source_yaw,
				target_yaw,
				source_terminal_yaw,
				this->m_quantized_trajectory[ 0 ].step,
				false );
			if ( !quantized_yaw_frame )
			{
				remove_claimed_trajectory( );
				owned_trajectory_valid = false;
				source_terminal_yaw = final_subtick_yaw( base, this->m_source_yaw );
			}
		}

		if ( !quantized_yaw_frame
			&& frame_changed
			&& quantized_input
			&& subtick_movement_view
			&& !firing
			&& !ground_flip
			&& discrete_source_move
			&& !contains_analog_trajectory( ) )
		{
			quantized_yaw_frame = install_quantized_yaw_frame(
				base,
				moves,
				this->m_source_yaw,
				target_yaw,
				source_terminal_yaw,
				nullptr,
				false );
		}

		if ( quantized_yaw_frame )
		{
			// The subtick yaw sandwich performs the coordinate conversion, so the
			// analog payload stays in its source frame and remains quantizable.
			movement_source_yaw = target_yaw;
			target_base = source_base;
			stable_sort_subticks( moves );
		}

		const auto impulse_frame_changed = meaningful_delta( this->m_source_impulses - this->m_wire_impulses );
		const auto base_changed = meaningful_delta( target_base - this->m_wire_impulses );

		// base always remains the final absolute state. Subtick deltas are
		// transitions to that state, not a replacement that requires the
		// base fields to be zeroed later.
		base->set_forwardmove( target_base.x );
		base->set_leftmove( target_base.y );

		auto has_analog_trajectory{ false };
		auto source_trajectory_end = this->m_source_impulses;
		if ( moves )
		{
			for ( auto i = 0; i < moves->size( ); ++i )
			{
				const auto delta = read_analog_delta( moves->mutable_at( i ) );
				if ( meaningful_delta( delta ) )
				{
					has_analog_trajectory = true;
				}
				source_trajectory_end += delta;
			}

			if ( has_analog_trajectory
				&& meaningful_delta( limit_move( source_trajectory_end ) - source_base ) )
			{
				// A later base-only writer owns the final movement. Retaining an
				// earlier analog trajectory would override it on the server, so
				// remove only the analog fields and keep yaw/button events intact.
				for ( auto i = 0; i < moves->size( ); ++i )
				{
					if ( auto step = moves->mutable_at( i ) )
					{
						write_analog_delta( step, {} );
					}
				}
				has_analog_trajectory = false;
			}
		}

		if ( moves && ( frame_changed || impulse_frame_changed || base_changed ) )
		{

			proto::subtick_move_step* anchor{};
			for ( auto i = 0; i < moves->size( ); ++i )
			{
				auto step = moves->mutable_at( i );
				const auto when = step && step->m_has_bits.test( 0x4u ) ? step->when( ) : 0.0f;
				if ( step && ( !std::isfinite( when ) || when <= k_when_epsilon ) )
				{
					anchor = step;
					break;
				}
			}

			if ( !anchor )
			{
				anchor = prepend_anchor( moves );
			}

			auto source_state = this->m_source_impulses;
			auto wire_state = this->m_wire_impulses;
			proto::subtick_move_step* terminal_movement_step{ anchor };
			for ( auto i = 0; i < moves->size( ); ++i )
			{
				auto step = moves->mutable_at( i );
				if ( !step )
				{
					continue;
				}

				const auto original_delta = read_analog_delta( step );
				source_state += original_delta;
				if ( meaningful_delta( original_delta ) )
				{
					terminal_movement_step = step;
				}

				if ( step == anchor )
				{
					// A generated time-zero anchor rebases the accumulator into
					// this command's frame before a later air-strafe trajectory.
					// With no analog trajectory, the base move itself is the only
					// transition and therefore becomes the anchor's destination.
					const auto desired = has_analog_trajectory
						? reframe( source_state, movement_source_yaw, target_yaw )
						: target_base;
					write_analog_delta( step, desired - wire_state );
					wire_state = desired;
				}
				else if ( meaningful_delta( original_delta ) )
				{
					const auto desired = reframe( source_state, movement_source_yaw, target_yaw );
					write_analog_delta( step, desired - wire_state );
					wire_state = desired;
				}
				else if ( step->m_has_bits.test( 0x8u ) || step->m_has_bits.test( 0x10u ) )
				{
					write_analog_delta( step, {} );
				}
			}

			// Float accumulation can differ by a few ulps even for a valid
			// trajectory. Make the last transition converge exactly to base.
			if ( terminal_movement_step && meaningful_delta( target_base - wire_state ) )
			{
				const auto corrected_delta = read_analog_delta( terminal_movement_step )
					+ target_base - wire_state;
				write_analog_delta( terminal_movement_step, corrected_delta );
			}
		}

		// Normalize pooled protobuf entries even on ticks that needed no
		// coordinate conversion. A stored numeric zero is not movement and
		// must not retain analog presence bits from a prior pooled message.
		if ( moves )
		{
			for ( auto i = 0; i < moves->size( ); ++i )
			{
				auto step = moves->mutable_at( i );
				if ( step && !meaningful_delta( read_analog_delta( step ) ) )
				{
					write_analog_delta( step, {} );
				}
			}
		}

		if ( frame_changed )
		{
			auto buttons = cmd->buttons.value & ~k_movement_mask;
			if ( target_base.x > k_move_epsilon )
			{
				buttons |= cstypes::command_buttons::in_forward;
			}
			else if ( target_base.x < -k_move_epsilon )
			{
				buttons |= cstypes::command_buttons::in_back;
			}

			// Keep digital movement keys consistent with the analog axes.
			if ( target_base.y > k_move_epsilon )
			{
				buttons |= cstypes::command_buttons::in_moveleft;
			}
			else if ( target_base.y < -k_move_epsilon )
			{
				buttons |= cstypes::command_buttons::in_moveright;
			}

			cmd->buttons.value = buttons;
		}

		const auto target_buttons = cmd->buttons.value & k_movement_mask;
		if ( this->m_previous_buttons_valid )
		{
			// Button edges are relative to the immediately preceding wire command,
			// not MovementServices' potentially older processed command.
			const auto changed = ( this->m_previous_buttons ^ target_buttons ) & k_movement_mask;
			cmd->buttons.value_changed = ( cmd->buttons.value_changed & ~k_movement_mask ) | changed;
			cmd->buttons.value_scroll = ( cmd->buttons.value_scroll & ~k_movement_mask )
				| ( target_buttons & changed );
		}
		else if ( frame_changed )
		{
			// There is no trustworthy prior wire state after a pawn/command gap.
			cmd->buttons.value_changed &= ~k_movement_mask;
			cmd->buttons.value_scroll &= ~k_movement_mask;
		}

		const auto wire_yaw = final_subtick_yaw( base, target_yaw );
		const auto store_frame = [ & ]( command_frame* frames, std::intptr_t command_number )
		{
			if ( !frames || command_number < 0 )
			{
				return;
			}

			auto& frame = frames[ command_number % k_command_history_size ];
			frame.command_number = command_number;
			frame.pawn = pawn;
			frame.movement_buttons = target_buttons;
			frame.yaw = quantized_yaw_frame ? source_terminal_yaw : wire_yaw;
			frame.quantized_payload = quantized_yaw_frame;
		};
		store_frame( this->m_command_frames, cmd->command_number );
		if ( base->m_has_bits.test( 0x10u ) )
		{
			store_frame( this->m_legacy_command_frames, base->legacy_command_number( ) );
		}
		base->m_cached_size = 0;
		this->m_previous_buttons_valid = false;
		this->m_active = false;
	}

} // namespace features::movement
