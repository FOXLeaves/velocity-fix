#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/diag.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	namespace detail {

		struct bullet_trace_record
		{
			float enter_fraction;
			float exit_fraction;
			float damage_applied;
			int team_at_contact;
			std::uint16_t enter_contact_ix;
			std::uint16_t exit_contact_ix;
			std::uint8_t can_penetrate;
			std::uint8_t pad[ 3 ];
		};

		// Engine calls on the hot path (spread, inaccuracy, autowall) receive
		// parameters whose layout can drift across game updates. A mismatch
		// raises a divide-by-zero or access violation inside engine code;
		// swallow it here and return a safe fallback so one bad update cannot
		// take the whole process down. The lambda closure is POD, so this
		// function is safe to use with SEH.
		template <typename Fn>
		auto guarded( Fn&& fn ) -> decltype( fn( ) )
		{
			__try
			{
				return fn( );
			}
			__except ( EXCEPTION_EXECUTE_HANDLER )
			{
				return {};
			}
		}

	} // namespace detail

	void shared::penetration::prepare( std::uintptr_t weapon_vdata, std::uintptr_t weapon )
	{
		if ( !weapon_vdata || !weapon )
		{
			return;
		}

		this->m_weapon_data = weapon_data
		{
			.damage = static_cast< float >( memory::read<int>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_nDamage"_hash ) ) ),
			.penetration = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flPenetration"_hash ) ),
			.range_modifier = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRangeModifier"_hash ) ),
			.range = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRange"_hash ) ),
			.armor_ratio = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flArmorRatio"_hash ) ),
			.headshot_multiplier = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flHeadshotMultiplier"_hash ) )
		};
	}

	shared::penetration::run_context shared::penetration::prepare_target( std::uintptr_t target_pawn, lagcomp::record* record, const systems::hitboxes::set* external_hitboxes ) const
	{
		run_context ctx{};
		ctx.target_pawn = target_pawn;
		ctx.record = record;
		if ( external_hitboxes && external_hitboxes->count > 0 )
		{
			ctx.hitboxes = *external_hitboxes;
		}
		else if ( record && record->game_scene_node )
		{
			ctx.hitboxes = systems::g_hitboxes.query( record->game_scene_node );
		}

		ctx.target_armor = memory::read<int>( target_pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );
		ctx.target_team = memory::read<int>( target_pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );

		if ( ctx.target_armor > 0 )
		{
			const auto services = memory::read<std::uintptr_t>( target_pawn + SCHEMA( "C_BasePlayerPawn", "m_pItemServices"_hash ) );
			if ( services )
			{
				ctx.has_helmet = memory::read<bool>( services + SCHEMA( "CCSPlayer_ItemServices", "m_bHasHelmet"_hash ) );
			}
		}

		ctx.scales =
		{
			.ct_head = CONVAR ("mp_damage_scale_ct_head")->get<float>( ),
			.t_head = CONVAR ("mp_damage_scale_t_head")->get<float>( ),
			.ct_body = CONVAR ("mp_damage_scale_ct_body")->get<float>( ),
			.t_body = CONVAR ("mp_damage_scale_t_body")->get<float>( )
		};

		ctx.armor_ratio = this->m_weapon_data.armor_ratio;
		ctx.headshot_multiplier = this->m_weapon_data.headshot_multiplier;

		return ctx;
	}

	bool shared::penetration::run( const math::vector3& start, const math::vector3& end, const run_context& ctx, std::uintptr_t local_pawn, int local_team, result& out, int fallback_hitbox ) const
	{
		return detail::guarded( [ & ]( ) -> bool
		{
		out = {};

		if ( this->m_weapon_data.damage <= 0.0f )
		{
			return false;
		}

		const auto direction = ( end - start ).normalized( );
		const auto trace_delta = direction * this->m_weapon_data.range;

		// --- 1. Trace preparation -------------------------------------------
		auto filter = systems::g_tracing.make_filter( local_pawn, 0x1c300b, 3, 15 );
		// Rage scanning calls this hundreds of times in a frame. Reuse the large
		// trace buffer per worker instead of allocating and freeing 7 KB per
		// point; the buffer is zeroed every call to match the old (known-good)
		// behaviour - setup_trace may not fully re-initialize the hit arrays.
		thread_local systems::tracing::trace_data trace_storage{};
		trace_storage = {};
		auto* trace = &trace_storage;
		trace->array_pointer = &trace->elements;
		trace->hit_array_pointer = &trace->hit_elements;

		g_shared.m_current_autowall_record = ctx.record;
		g_shared.m_autowalling = true;

		// The hitbox-transform hook supplies this thread's record directly.
		// Do not swap the live entity pose: Present may read it concurrently.
		// The marker is intentionally dropped BEFORE trace_bullet (matching
		// the old known-good build): keeping it up through the engine call
		// made the hook rewrite transforms mid-trace, which turned scan_player
		// into an access-violation storm (0xC0000005 in client.dll trace code).
		// trace_bullet therefore resolves against the live pose, exactly like
		// the server does.
		systems::g_tracing.setup_trace( trace, start, trace_delta, filter, 4, true );

		g_shared.m_autowalling = false;
		g_shared.m_current_autowall_record = nullptr;

		// --- 2. Geometric target match ---------------------------------------
		// The engine resolves the trace against the LIVE pose (the marker is
		// dropped by design), while the server rewinds the target to the
		// record tick when the attack fires. Cross-check the ray against the
		// rewound RECORD skeleton: the closest intersected hitbox is the one
		// the server would resolve the shot through.
		auto actual_hitbox{ -1 };
		auto closest_hitbox_fraction{ 1.0f };
		if ( ctx.record )
		{
			for ( const auto& hitbox : ctx.hitboxes )
			{
				if ( hitbox.bone < 0 || hitbox.bone >= ctx.record->bone_count )
				{
					continue;
				}

				const auto& bone = ctx.record->bones[ hitbox.bone ];
				auto fraction{ 1.0f };
				auto intersects{ false };

				if ( hitbox.radius > 0.001f )
				{
					const auto capsule_start = bone.rotation.rotate_vector( hitbox.mins ) + bone.position;
					const auto capsule_end = bone.rotation.rotate_vector( hitbox.maxs ) + bone.position;
					intersects = g_shared.ray_vs_capsule( start, trace_delta, capsule_start, capsule_end, hitbox.radius, fraction );
				}
				else
				{
					// Box hitboxes: slab test in bone space.
					auto inverse = bone.rotation;
					inverse.x = -inverse.x;
					inverse.y = -inverse.y;
					inverse.z = -inverse.z;

					const auto local_origin = inverse.rotate_vector( start - bone.position );
					const auto local_delta = inverse.rotate_vector( trace_delta );
					auto entry{ 0.0f };
					auto exit{ 1.0f };

					const auto intersect_axis = [ & ]( float origin, float delta, float minimum, float maximum )
						{
							if ( std::fabsf( delta ) < 1.0e-8f )
							{
								return origin >= minimum && origin <= maximum;
							}

							auto first = ( minimum - origin ) / delta;
							auto second = ( maximum - origin ) / delta;
							if ( first > second ) std::swap( first, second );
							entry = std::max( entry, first );
							exit = std::min( exit, second );
							return entry <= exit;
						};

					intersects = intersect_axis( local_origin.x, local_delta.x, hitbox.mins.x, hitbox.maxs.x ) &&
						intersect_axis( local_origin.y, local_delta.y, hitbox.mins.y, hitbox.maxs.y ) &&
						intersect_axis( local_origin.z, local_delta.z, hitbox.mins.z, hitbox.maxs.z );
					fraction = entry;
				}

				if ( intersects && fraction < closest_hitbox_fraction )
				{
					closest_hitbox_fraction = fraction;
					actual_hitbox = hitbox.index;
				}
			}
		}

		// --- 3. Engine bullet simulation --------------------------------------
		const auto num_hits = trace->num_hits;
		const auto hit_array = reinterpret_cast< std::uintptr_t >( trace->hit_array_pointer );
		const auto surface_array = reinterpret_cast< std::uintptr_t >( trace->array_pointer );

		// Garbage hit counts from a drifted engine signature would walk the
		// hit array out of bounds below.
		if ( num_hits <= 0 || num_hits > 64 || !hit_array || !surface_array )
		{
			return false;
		}

		memory::call<void> (PATTERN (patterns::trace_bullet), trace, this->m_weapon_data.damage, this->m_weapon_data.penetration, this->m_weapon_data.range_modifier, 4, local_team, static_cast<std::uintptr_t>(0));

		// --- 4. Hit resolution ------------------------------------------------
		// State machine over the engine's hit records:
		//   flying  - still travelling, no entity hit yet
		//   struck  - a physical hit record tied back to the target pawn
		//   blocked - the bullet stopped inside cover (exit fraction 1)
		//   spent   - damage ran out before reaching the target
		//   missed  - the whole pass never touched the target
		enum class phase : std::uint8_t
		{
			flying,
			struck,
			blocked,
			spent,
			missed
		};

		auto current_phase{ phase::flying };
		auto last_damage{ 0.0f };
		auto wall_hits{ 0 };

		for ( auto i = 0; i < num_hits; ++i )
		{
			auto hit = reinterpret_cast< detail::bullet_trace_record* >( hit_array + i * sizeof( detail::bullet_trace_record ) );
			const auto damage = hit->damage_applied;

			if ( !std::isfinite( damage ) || damage <= 0.0f )
			{
				// The bullet ran out of damage before reaching the target -
				// the geometry fallback must not fire through a wall that
				// exhausted the shot.
				current_phase = phase::spent;
				break;
			}

			last_damage = damage;

			if ( ( hit->can_penetrate & 1 ) != 0 )
			{
				// Penetration record: an exit fraction of 1 means the bullet
				// did not make it through that surface.
				if ( hit->exit_fraction == 1.0f )
				{
					current_phase = phase::blocked;
					break;
				}

				++wall_hits;
				continue;
			}

			// Physical hit record: the contact index resolves back to a
			// surface array entry holding the hit entity handle.
			const auto trace_holder = surface_array + sizeof( systems::tracing::trace_array_element ) * ( hit->enter_contact_ix & 0x7fff );
			const auto hit_handle = memory::read<std::uint32_t>( trace_holder + 0x2c );

			// Entity handles carry a serial in the high bits; a large value is
			// legal, so only the invalid sentinels are rejected here (an
			// earlier range check killed every hit and silenced the ragebot).
			if ( hit_handle == 0 || hit_handle == 0xffffffffu )
			{
				continue;
			}

			const auto hit_entity = systems::g_entities.lookup( hit_handle );

			if ( !hit_entity || hit_entity != ctx.target_pawn )
			{
				continue;
			}

			current_phase = phase::struck;
			break;
		}

		// --- 5. Commit ---------------------------------------------------------
		if ( current_phase == phase::struck )
		{
			if ( actual_hitbox < 0 )
			{
				// The engine trace resolved against the LIVE pose, while the
				// intersection test above ran against the rewound RECORD
				// skeleton - on a moving target the stale record can miss
				// a trace that visibly hit the body, and the hit was
				// silently discarded. Fall back to the hitbox the caller
				// aimed at: the server rewinds the target to the record
				// tick when the shot fires, and that point IS the record
				// pose - so the aimed hitbox is the pose's own hitbox.
				actual_hitbox = fallback_hitbox;
				if ( actual_hitbox < 0 )
				{
					return false;
				}
			}

			out.hitbox = actual_hitbox;
			out.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( actual_hitbox );
			out.penetrated = wall_hits > 0;
			out.damage = last_damage;

			this->scale_damage( out.hitgroup, ctx.target_armor, ctx.has_helmet, ctx.target_team, ctx.armor_ratio, ctx.headshot_multiplier, ctx.scales, out.damage );

			return true;
		}

		// --- 6. Wallbang geometry fallback -------------------------------------
		// Physical hit records are tied back to the pawn through surface
		// contact handles, and a penetration pass frequently produces
		// records whose contact index resolves to the COVER's surface - the
		// hit is then silently dropped even though the shot visibly connects
		// (the player can hit it manually). When the client simulation
		// confirmed the bullet passed through cover (wall_hits > 0, no
		// blocked/spent phase), the bullet had damage left AND the ray
		// geometrically crosses the target's record hitboxes, the server
		// resolves that same trajectory against the same pose - accept the
		// hit with the last recorded damage instead of staring at the wall.
		if ( actual_hitbox >= 0 && current_phase == phase::flying && wall_hits > 0 && last_damage > 0.0f )
		{
			out.hitbox = actual_hitbox;
			out.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( actual_hitbox );
			out.penetrated = true;
			out.damage = last_damage;

			this->scale_damage( out.hitgroup, ctx.target_armor, ctx.has_helmet, ctx.target_team, ctx.armor_ratio, ctx.headshot_multiplier, ctx.scales, out.damage );

			return true;
		}

		return false;
		} );
	}

	bool shared::penetration::can( const math::vector3& start, const math::vector3& direction, float& out_damage, const systems::local::snapshot& local ) const
	{
		return detail::guarded( [ & ]( ) -> bool
		{
		out_damage = 0.0f;

		if ( this->m_weapon_data.damage <= 0.0f || this->m_weapon_data.penetration <= 0.0f )
		{
			return false;
		}

		const auto local_team = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		const auto trace_delta = direction * this->m_weapon_data.range;

		auto filter = systems::g_tracing.make_filter( local.pawn, 0x1c300b, 3, 15 );
		thread_local systems::tracing::trace_data trace_storage{};
		trace_storage = {};
		auto* trace = &trace_storage;
		trace->array_pointer = &trace->elements;
		trace->hit_array_pointer = &trace->hit_elements;

		systems::g_tracing.setup_trace( trace, start, trace_delta, filter, 4, true );

		const auto num_hits = trace->num_hits;
		const auto hit_array = reinterpret_cast< std::uintptr_t >( trace->hit_array_pointer );

		// A garbage hit count from a drifted engine call would walk the hit
		// array out of bounds below - the crosshair draw ran this every tick
		// and crashed on such a value.
		if ( num_hits <= 0 || num_hits > 64 || !hit_array )
		{
			return false;
		}

		memory::call<void> (PATTERN (patterns::trace_bullet), trace, this->m_weapon_data.damage, this->m_weapon_data.penetration, this->m_weapon_data.range_modifier, 4, local_team, static_cast<std::uintptr_t>(0));

		// Same penetration semantics as run(): the first penetration record
		// whose exit fraction is not 1 confirms the bullet got through, and
		// that record's damage is the remaining post-wall damage.
		for ( auto i = 0; i < num_hits; ++i )
		{
			auto hit = reinterpret_cast< detail::bullet_trace_record* >( hit_array + i * sizeof( detail::bullet_trace_record ) );
			const auto damage = hit->damage_applied;

			if ( !std::isfinite( damage ) || damage <= 0.0f )
			{
				break;
			}

			if ( ( hit->can_penetrate & 1 ) != 0 )
			{
				if ( hit->exit_fraction == 1.0f )
				{
					break;
				}

				out_damage = damage;
				return true;
			}
		}

		return false;
		} );
	}

	float shared::penetration::get_max_damage( int hitgroup, int target_armor, bool has_helmet, int target_team ) const
	{
		if ( this->m_weapon_data.damage <= 0.0f )
		{
			return 0.0f;
		}

		const damage_scales scales
		{
			.ct_head = CONVAR ("mp_damage_scale_ct_head")->get<float> (),
			.t_head = CONVAR ("mp_damage_scale_t_head")->get<float> (),
			.ct_body = CONVAR ("mp_damage_scale_ct_body")->get<float> (),
			.t_body = CONVAR ("mp_damage_scale_t_body")->get<float> ()
		};

		auto damage = this->m_weapon_data.damage;
		this->scale_damage( hitgroup, target_armor, has_helmet, target_team, this->m_weapon_data.armor_ratio, this->m_weapon_data.headshot_multiplier, scales, damage );
		return damage;
	}

	void shared::penetration::scale_damage( int hitgroup, int armor, bool has_helmet, int team, float armor_ratio, float headshot_multiplier, const damage_scales& scales, float& damage ) const
	{
		const auto is_ct = ( team == 3 );
		const auto head_scale = is_ct ? scales.ct_head : scales.t_head;
		const auto body_scale = is_ct ? scales.ct_body : scales.t_body;

		switch ( hitgroup )
		{
		case 1:
			damage *= headshot_multiplier * head_scale;
			break;
		case 2:
		case 4:
		case 5:
		case 8:
			damage *= body_scale;
			break;
		case 3:
			damage *= 1.25f * body_scale;
			break;
		case 6:
		case 7:
			damage *= 0.75f * body_scale;
			break;
		default:
			break;
		}

		const auto is_head = ( hitgroup == 1 );
		const auto is_armored = ( hitgroup >= 1 && hitgroup <= 5 ) || ( hitgroup == 8 );

		if ( armor <= 0 || !is_armored || ( is_head && !has_helmet ) )
		{
			damage = std::floor( damage );
			return;
		}

		constexpr auto armor_bonus{ 0.5f };
		const auto armor_ratio_scaled = armor_ratio * 0.5f;

		auto damage_to_health = damage * armor_ratio_scaled;
		auto damage_to_armor = ( damage - damage_to_health ) * armor_bonus;

		if ( damage_to_armor > static_cast< float >( armor ) )
		{
			damage_to_health = damage - ( static_cast< float >( armor ) / armor_bonus );
		}

		damage = std::floor( damage_to_health );
	}

	bool shared::lagcomp::record::setup( std::uintptr_t pawn )
	{
		this->pawn = pawn;
		this->game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );

		if ( !this->game_scene_node )
		{
			return false;
		}

		this->bone_cache = memory::read<std::uintptr_t>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 );
		if ( !this->bone_cache )
		{
			return false;
		}

		this->bone_count = memory::read<int>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x8c );
		if ( this->bone_count <= 0 )
		{
			return false;
		}
		this->bone_count = std::min( this->bone_count, 128 );

		const auto abs_origin = memory::read<math::vector3>( this->game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto abs_rotation = memory::read<math::vector3>( this->game_scene_node + SCHEMA( "CGameSceneNode", "m_angAbsRotation"_hash ) );
		if ( !std::isfinite( abs_origin.x ) || !std::isfinite( abs_origin.y ) || !std::isfinite( abs_origin.z ) )
		{
			return false;
		}

		// Network origin is encoded. Records and bones must stay in the same
		// evaluated world-space coordinate system.
		this->origin = abs_origin;
		this->rotation = abs_rotation;

		this->simulation_time = memory::read<float>( pawn + SCHEMA( "C_BaseEntity", "m_flSimulationTime"_hash ) );

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		if ( !global_vars )
		{
			return false;
		}

		const auto backup_current_time = memory::read<float>( global_vars + 0x30 );
		const auto backup_tick_count = memory::read<int>( global_vars + 0x44 );
		memory::write<float>( global_vars + 0x30, this->simulation_time );
		memory::write<int>( global_vars + 0x44, cstypes::time_to_ticks( this->simulation_time ) );

		memory::call<void>(PATTERN (patterns::game_scene_node_set_mesh_group), this->game_scene_node, 0xfffff );
		memory::call<void>(PATTERN (patterns::game_scene_node_set_skeleton), this->game_scene_node, 0x100 );

		memory::write<int>( global_vars + 0x44, backup_tick_count );
		memory::write<float>( global_vars + 0x30, backup_current_time );

		this->bone_cache = memory::read<std::uintptr_t>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 );
		if ( !this->bone_cache )
		{
			return false;
		}

		std::memcpy( this->bones, reinterpret_cast< void* >( this->bone_cache ), sizeof( systems::bones::data ) * this->bone_count );

		this->tick = cstypes::time_to_ticks( this->simulation_time );
		this->valid = true;

		return true;
	}

	bool shared::lagcomp::record::is_valid( ) const
	{
		if ( !this->valid )
		{
			return false;
		}

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );

		if ( !global_vars )
		{
			return false;
		}

		// Latency and max-unlag change at most once per tick; use the budget
		// cached by shared::update instead of querying the net channel vfunc
		// for every record in every scan.
		const auto budget = g_shared.latency_budget( );
		if ( !std::isfinite( budget ) )
		{
			return false;
		}

		const auto current_time = memory::read<float>( global_vars + 0x30 );

		// A non-positive budget (high latency vs sv_maxunlag) would void
		// every record and leave an empty backtrack - never hit anything.
		// Keep at least the freshest record (bt 0t) usable in that case.
		const auto effective_budget = budget > 0.0f ? budget : 0.05f;

		return this->simulation_time >= current_time - effective_budget;
	}

	void shared::lagcomp::record::apply( )
	{
		if ( !this->valid || this->is_applied || !this->game_scene_node )
		{
			return;
		}

		this->bone_cache = memory::read<std::uintptr_t>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 );
		if ( !this->bone_cache )
		{
			return;
		}

		const auto size = sizeof( systems::bones::data ) * this->bone_count;
		std::memcpy( this->bones_backup, reinterpret_cast< void* >( this->bone_cache ), size );
		std::memcpy( reinterpret_cast< void* >( this->bone_cache ), this->bones, size );

		this->is_applied = true;
	}

	void shared::lagcomp::record::restore( )
	{
		if ( !this->valid || !this->is_applied || !this->bone_cache )
		{
			return;
		}

		const auto size = sizeof( systems::bones::data ) * this->bone_count;
		std::memcpy( reinterpret_cast< void* >( this->bone_cache ), this->bones_backup, size );

		this->is_applied = false;
	}

	void shared::lagcomp::run( )
	{
		std::unique_lock records_lock( this->m_records_mtx );

		const auto local = systems::g_local.get( );
		if ( !local.is_alive )
		{
			this->m_records.clear( );

			std::lock_guard extrap_lock( this->m_extrap_cache_mtx );
			this->m_extrap_cache.clear( );
			return;
		}

		std::unordered_set<std::uintptr_t> active{};

		for ( const auto& p : systems::g_entities.get_by_type( systems::entities::type::player ) )
		{
			if ( !p.ptr || p.ptr == local.controller )
			{
				continue;
			}

			if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );

			if ( !pawn || pawn == local.pawn )
			{
				continue;
			}

			const auto team = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
			{
				continue;
			}

			active.insert( pawn );
		}

		std::erase_if( this->m_records, [ & ]( const auto& pair ) { return !active.contains( pair.first ); } );

		{
			std::lock_guard extrap_lock( this->m_extrap_cache_mtx );
			std::erase_if( this->m_extrap_cache, [ & ]( const auto& pair ) { return !active.contains( pair.first ); } );
		}

		struct pending_record
		{
			std::uintptr_t pawn{};
			int simulation_tick{};
		};

		std::vector<pending_record> pending;
		pending.reserve( active.size( ) );

		for ( const auto& pawn : active )
		{
			const auto health = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( health <= 0 )
			{
				this->m_records.erase( pawn );

				std::lock_guard extrap_lock( this->m_extrap_cache_mtx );
				this->m_extrap_cache.erase( pawn );
				continue;
			}

			auto& records = this->m_records[ pawn ];
			const auto simulation_time = memory::read<float>( pawn + SCHEMA( "C_BaseEntity", "m_flSimulationTime"_hash ) );
			const auto simulation_tick = cstypes::time_to_ticks( simulation_time );

			if ( records.empty( ) || simulation_tick > records.front( ).tick )
			{
				pending.push_back( { pawn, simulation_tick } );
			}

			while ( !records.empty( ) && !records.back( ).is_valid( ) )
			{
				records.pop_back( );
			}
		}

		if ( pending.empty( ) )
		{
			return;
		}

		for ( auto& p : pending )
		{
			record rec{};

			if ( rec.setup( p.pawn ) )
			{
				this->m_records[ p.pawn ].emplace_front( std::move( rec ) );
			}
		}

		for ( auto& [pawn, records] : this->m_records )
		{
			// Bound the per-player history: records beyond the server unlag
			// window can never be used, and unbounded growth only costs
			// memory and scan time.
			if ( records.size( ) > 64 )
			{
				records.resize( 64 );
			}

			for ( auto& rec : records )
			{
				rec.was_valid = rec.is_valid( );
			}
		}
	}

	shared::lagcomp::record* shared::lagcomp::get_oldest_valid( std::uintptr_t pawn )
	{
		std::shared_lock records_lock( this->m_records_mtx );

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.empty( ) )
		{
			return nullptr;
		}

		for ( auto rit = it->second.rbegin( ); rit != it->second.rend( ); ++rit )
		{
			if ( rit->is_valid( ) )
			{
				return &( *rit );
			}
		}

		return nullptr;
	}

	std::vector<shared::lagcomp::display_point> shared::lagcomp::get_display_points( std::uintptr_t pawn ) const
	{
		std::vector<display_point> out;

		std::shared_lock records_lock( this->m_records_mtx );

		const auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.empty( ) )
		{
			return out;
		}

		out.reserve( it->second.size( ) );
		math::vector3 last_origin{};
		bool have_last_origin{ false };
		for ( const auto& rec : it->second )
		{
			// Skip stationary duplicates: a target that never moved yields
			// per-tick records at the same spot - showing them all just
			// stacks identical ghosts on top of each other.
			if ( have_last_origin && ( rec.origin - last_origin ).length_sqr( ) < 1.0f )
			{
				continue;
			}

			last_origin = rec.origin;
			have_last_origin = true;

			display_point pt{};
			pt.origin = rec.origin;
			pt.tick = rec.tick;
			pt.valid = rec.valid || rec.was_valid;
			pt.extrapolated = rec.extrapolated;

			for ( auto b = 0; b < 27 && b < rec.bone_count; ++b )
			{
				pt.bones[ b ] = rec.bones[ b ].position;
			}

			out.push_back( pt );
		}

		return out;
	}

	shared::lagcomp::record* shared::lagcomp::get_oldest_was_valid( std::uintptr_t pawn )
	{
		std::shared_lock records_lock( this->m_records_mtx );

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.empty( ) )
		{
			return nullptr;
		}

		for ( auto rit = it->second.rbegin( ); rit != it->second.rend( ); ++rit )
		{
			if ( rit->was_valid )
			{
				return &( *rit );
			}
		}

		return nullptr;
	}

	std::optional<shared::lagcomp::visual_record> shared::lagcomp::get_oldest_was_valid_visual( std::uintptr_t pawn ) const
	{
		std::shared_lock records_lock( this->m_records_mtx );

		const auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) )
		{
			return std::nullopt;
		}

		for ( auto rit = it->second.rbegin( ); rit != it->second.rend( ); ++rit )
		{
			if ( !rit->was_valid )
			{
				continue;
			}

			visual_record out{};
			out.origin = rit->origin;
			for ( auto i = 0; i < 27; ++i )
			{
				out.bones[ i ] = rit->bones[ i ];
			}

			return out;
		}

		return std::nullopt;
	}

	std::vector<shared::lagcomp::record*> shared::lagcomp::get_valid_records( std::uintptr_t pawn )
	{
		std::shared_lock records_lock( this->m_records_mtx );

		std::vector<record*> result;

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) )
		{
			return result;
		}

		result.reserve( it->second.size( ) );

		for ( auto& rec : it->second )
		{
			if ( rec.is_valid( ) )
			{
				result.push_back( &rec );
			}
		}

		if ( result.empty( ) )
		{
			return result;
		}

		const auto max_ticks = std::clamp( settings::g_combat.m_lagcomp.max_backtrack_ticks.value, 1, static_cast< int >( rage::k_max_lagcomp_records ) );
		const auto newest_tick = result.front( )->tick;

		result.erase(
			std::remove_if( result.begin( ), result.end( ), [ newest_tick, max_ticks ]( const record* rec )
				{
					return ( newest_tick - rec->tick ) > max_ticks;
				} ),
			result.end( )
		);

		return result;
	}

	std::array<systems::bones::data, 27> shared::lagcomp::get_skeleton( const record& record ) const
	{
		std::array<systems::bones::data, 27> skeleton;

		if ( record.valid )
		{
			for ( auto i = 0; i < 27; ++i )
			{
				skeleton[ i ] = record.bones[ i ];
			}
		}

		return skeleton;
	}

	void shared::shoot_history::snapshot( std::uintptr_t local_pawn, std::uintptr_t weapon_services )
	{
		this->m_count = 0;

		if ( !weapon_services )
		{
			return;
		}

		{
			const auto net_client = addresses::globals::network_client_service;
			if ( !net_client )
			{
				return;
			}

			const auto tick_state = memory::call_vfunc<std::uintptr_t>( net_client, 23 );
			if ( !tick_state )
			{
				return;
			}

			this->m_server_tick = memory::read<int>( tick_state + 892 );
		}

		{
			const auto idx_raw = memory::read<int>( addresses::globals::frame_input_ring_idx );
			const auto idx = static_cast< unsigned >( idx_raw ) % 10u;
			const auto slot = addresses::globals::frame_input_ring_base + 40ull * idx;

			this->m_client_tick = memory::read<int>( slot + 0x0c );
			this->m_client_tick_frac = memory::read<float>( slot + 0x10 );
		}

		{
			const auto lerp_seconds = memory::call<float>(PATTERN (patterns::get_interp_amount), local_pawn );
			const auto lerp_ticks_f = lerp_seconds * 64.0f;
			const auto rounded = std::round( lerp_ticks_f );

			if ( std::fabs( lerp_ticks_f - rounded ) < 1e-4f )
			{
				this->m_lerp_ticks_int = static_cast< int >( rounded );
				this->m_lerp_ticks_frac = 0.0f;
			}
			else
			{
				this->m_lerp_ticks_int = static_cast< int >( std::floor( lerp_ticks_f ) );
				this->m_lerp_ticks_frac = lerp_ticks_f - static_cast< float >( this->m_lerp_ticks_int );
			}
		}

		const auto tail = memory::read<int>( weapon_services + 872 );
		const auto count = memory::read<int>( weapon_services + 876 );

		if ( count <= 0 || count > 32 || tail < 0 || tail >= 32 )
		{
			return;
		}

		for ( auto i = 0; i < count; ++i )
		{
			const auto idx = ( tail + i ) % 32;
			const auto off = weapon_services + 232 + 20ull * idx;

			auto& e = this->m_entries[ i ];
			e.tick = memory::read<int>( off + 0x00 );
			e.fraction = memory::read<float>( off + 0x04 );
			e.position.x = memory::read<float>( off + 0x08 );
			e.position.y = memory::read<float>( off + 0x0C );
			e.position.z = memory::read<float>( off + 0x10 );
		}

		this->m_count = count;
	}

	shared::shoot_history::eye_candidates shared::shoot_history::get_candidates( ) const
	{
		eye_candidates out{};

		if ( this->m_count < 1 )
		{
			return out;
		}

		constexpr auto ring_slot{ 0.03125f };

		const auto newest_valid_tick = this->m_client_tick - this->m_lerp_ticks_int;
		const auto oldest_valid_tick = this->m_client_tick - this->m_lerp_ticks_int - 1;

		auto first_valid{ -1 };
		auto last_valid{ -1 };

		for ( auto i = 0; i < this->m_count; ++i )
		{
			const auto t = this->m_entries[ i ].tick;
			if ( t > newest_valid_tick || t < oldest_valid_tick )
			{
				continue;
			}

			if ( first_valid == -1 )
			{
				first_valid = i;
			}

			last_valid = i;
		}

		if ( last_valid == -1 )
		{
			return out;
		}

		const auto& newest = this->m_entries[ last_valid ];
		out.entries[ 0 ].position = newest.position;
		out.entries[ 0 ].player_tick = newest.tick;
		out.entries[ 0 ].player_frac = newest.fraction + ring_slot;
		out.entries[ 0 ].lerp_ticks_int = this->m_lerp_ticks_int;
		out.entries[ 0 ].lerp_ticks_frac = this->m_lerp_ticks_frac;
		out.count = 1;

		if ( first_valid != last_valid )
		{
			const auto& oldest = this->m_entries[ first_valid ];
			const auto  delta = oldest.position - newest.position;

			if ( delta.x * delta.x + delta.y * delta.y + delta.z * delta.z >= 4.0f )
			{
				out.entries[ 1 ].position = oldest.position;
				out.entries[ 1 ].player_tick = oldest.tick;
				out.entries[ 1 ].player_frac = oldest.fraction + ring_slot;
				out.entries[ 1 ].lerp_ticks_int = this->m_lerp_ticks_int;
				out.entries[ 1 ].lerp_ticks_frac = this->m_lerp_ticks_frac;
				out.count = 2;
			}
		}

		return out;
	}

	math::vector3 shared::shoot_history::position_at( int tick, float frac ) const
	{
		if ( this->m_count <= 0 )
		{
			return {};
		}

		// Entries are ordered oldest -> newest (snapshot fills 0..count-1
		// from the engine ring tail).
		const auto& first = this->m_entries[ 0 ];
		const auto& last = this->m_entries[ this->m_count - 1 ];

		if ( tick <= first.tick )
		{
			return first.position;
		}

		if ( tick >= last.tick )
		{
			return last.position;
		}

		for ( auto i = 0; i + 1 < this->m_count; ++i )
		{
			const auto& a = this->m_entries[ i ];
			const auto& b = this->m_entries[ i + 1 ];

			const auto b_after = b.tick > tick || ( b.tick == tick && b.fraction >= frac );
			if ( !b_after )
			{
				continue;
			}

			const auto ta = static_cast< float >( a.tick ) + a.fraction;
			const auto tb = static_cast< float >( b.tick ) + b.fraction;
			const auto target = static_cast< float >( tick ) + frac;
			const auto span = tb - ta;
			const auto t = span > 0.0001f ? std::clamp( ( target - ta ) / span, 0.0f, 1.0f ) : 0.0f;

			return a.position + ( b.position - a.position ) * t;
		}

		return last.position;
	}

	void shared::update( )
	{
		this->m_ctx = {};

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		// Refresh the lag-comp validity budget once per tick instead of once
		// per record. is_valid() consumes this figure for every scanned record.
		this->m_latency_budget = -1.0f;		{
			const auto net_channel = memory::call<std::uintptr_t>(PATTERN (patterns::get_net_channel), 0, 0 );
			if ( net_channel )
			{
				const auto server_limit = CONVAR ("sv_maxunlag")->get<float>( );
				const auto player_limit = CONVAR ("sv_maxunlag_player")->get<float>( );
				const auto max_unlag = player_limit > 0.0f ? std::min( server_limit, player_limit ) : server_limit;
				const auto latency = memory::call_vfunc<float>( net_channel, 10, 0 );

				if ( std::isfinite( max_unlag ) && std::isfinite( latency ) )
				{
					this->m_latency_budget = max_unlag - std::max( latency, 0.0f );
				}
			}
		}

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );

		if ( !global_vars || !movement_services )
		{
			return;
		}

		this->m_ctx.current_tick = memory::read<int>( global_vars + 0x44 );
		this->m_ctx.current_time = memory::read<float>( global_vars + 0x30 );
		this->m_ctx.is_scoped = memory::read<bool>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );
		this->m_ctx.ticks_since_land = this->m_ctx.current_tick - memory::read<int>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_ModernJump"_hash ) + SCHEMA( "CCSPlayerModernJump", "m_nLastLandedTick"_hash ) );
		this->m_ctx.weapon_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );

		if ( !this->m_ctx.weapon_services )
		{
			return;
		}

		const auto weapon_handle = memory::read<std::uint32_t>( this->m_ctx.weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
		if ( !weapon_handle )
		{
			return;
		}

		this->m_ctx.weapon = systems::g_entities.lookup( weapon_handle );
		if ( !this->m_ctx.weapon )
		{
			return;
		}

		this->m_ctx.weapon_vdata = memory::read<std::uintptr_t>( this->m_ctx.weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 );
		if ( !this->m_ctx.weapon_vdata )
		{
			return;
		}

		this->m_ctx.range = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRange"_hash ) );
		this->m_ctx.weapon_type = memory::read<std::uint32_t>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_WeaponType"_hash ) );
		this->m_ctx.item_def_idx = memory::read<std::uint16_t>( this->m_ctx.weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_hash ) + SCHEMA( "C_AttributeContainer", "m_Item"_hash ) + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
		this->m_ctx.num_bullets = memory::read<int>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_nNumBullets"_hash ) );
		this->m_ctx.recoil_index = memory::read<float>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_hash ) );
		this->m_ctx.weapon_max_speed = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_hash ) );
		this->m_ctx.is_jump_scouting = ( systems::g_prediction.pre( ).flags & cstypes::entity_flags::on_ground ) == 0 && this->m_ctx.item_def_idx == cstypes::item_definition_index::weapon_ssg_08 && this->m_ctx.is_scoped;
		this->m_ctx.valid = true;

		this->m_pen.prepare( this->m_ctx.weapon_vdata, this->m_ctx.weapon );
	}

	void shared::invalidate_if_needed( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn || !local.controller )
		{
			this->m_ctx = {};
			this->m_last_shoot_tick = 0;
		}
	}

	std::uint32_t shared::get_spread_seed( const math::vector3& angles, int tick ) const
	{
		return detail::guarded( [ & ]( ) -> std::uint32_t
			{
				return memory::call<std::uint32_t>(PATTERN (patterns::get_tick_view_angles), nullptr, &angles, tick );
			} );
	}

	// Engine signature (kept in sync with patterns::weapon_calculate_spread):
	//   void CalculateSpread( int16_t item_index, int num_bullets, int seed2,
	//                         uint32_t seed, float accuracy, float spread,
	//                         float recoil_index, float* x, float* y )
	// The seed offset of +1 mirrors the engine's FireBullets convention
	// (RandomSeed( seed + 1 )). The seed arrives as a uint32 (often larger
	// than INT_MAX - it wraps to a negative int in this signature and is
	// widened back with the uint32 cast); no range gate may reject it.
	math::vector2 shared::calculate_spread( int seed, float accuracy, float spread, float recoil_index, int item_def_idx, int num_bullets ) const
	{
		return detail::guarded( [ & ]( ) -> math::vector2
			{
				math::vector2 out{};

				memory::call<void>(PATTERN (patterns::weapon_calculate_spread), static_cast< std::int16_t >( item_def_idx ), num_bullets, 0, static_cast< std::uint32_t >( seed + 1 ), accuracy, spread, recoil_index, &out.x, &out.y );

				if ( !std::isfinite( out.x ) || !std::isfinite( out.y ) )
				{
					return {};
				}

				// Diagnostic: verify the engine spread function actually
				// responds to the accuracy input. A large accuracy (0.02+
				// rad cone) MUST produce a large spread vector - if it
				// comes back tiny, the hitchance computed from it is
				// systematically inflated and every "accurate" shot
				// misses on the real cone.
				if ( accuracy > 0.02f && seed == 0 )
				{
					static bool spread_warned{};
					if ( !spread_warned )
					{
						spread_warned = true;
						diag::writef( diag::level::info, "[spread] WARNING: accuracy {:.4f} spread_in {:.5f} -> engine out ({:.4f},{:.4f})", accuracy, spread, out.x, out.y );
					}
				}

				return out;
			} );
	}

	math::vector3 shared::get_aim_punch( std::uintptr_t local_pawn ) const
	{
		return detail::guarded( [ & ]( ) -> math::vector3
			{
				math::vector3 out{};

				memory::call<void>(PATTERN (patterns::get_aim_punch), memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_CSPlayerPawn", "m_pAimPunchServices"_hash ) ), &out, 0u );

				return out;
			} );
	}

	float shared::calculate_hitchance( const math::vector3& shoot_position, const math::vector3& aim_angle, const systems::hitboxes::entry& hitbox, const systems::bones::data& bone, float inaccuracy, float spread, int samples, float threshold ) const
	{
		const auto total = spread + inaccuracy;
		if ( total < 0.0001f )
		{
			// Matches the old build: a zero cone is treated as perfect. The
			// no-spread path forces hitchance to 1.0 anyway, so gating this
			// on the scoped state only broke unscoped no-spread shots.
			return 1.0f;
		}

		if ( samples <= 0 )
		{
			return 0.0f;
		}

		const auto capsule_start = bone.rotation.rotate_vector( hitbox.mins ) + bone.position;
		const auto capsule_end = bone.rotation.rotate_vector( hitbox.maxs ) + bone.position;
		const auto is_capsule = hitbox.radius > 0.001f;
		auto inverse_rotation = bone.rotation;
		inverse_rotation.x = -inverse_rotation.x;
		inverse_rotation.y = -inverse_rotation.y;
		inverse_rotation.z = -inverse_rotation.z;
		const auto box_ray_origin = inverse_rotation.rotate_vector( shoot_position - bone.position );

		const auto ray_vs_box = [ & ]( const math::vector3& ray_direction )
		{
			const auto direction = inverse_rotation.rotate_vector( ray_direction );
			auto entry{ 0.0f };
			auto exit{ 1.0f };

			const auto intersect_axis = [ & ]( float origin, float delta, float minimum, float maximum )
			{
				if ( std::fabs( delta ) < 1.0e-8f )
				{
					return origin >= minimum && origin <= maximum;
				}

				auto first = ( minimum - origin ) / delta;
				auto second = ( maximum - origin ) / delta;
				if ( first > second )
				{
					std::swap( first, second );
				}

				entry = std::max( entry, first );
				exit = std::min( exit, second );
				return entry <= exit;
			};

			return intersect_axis( box_ray_origin.x, direction.x, hitbox.mins.x, hitbox.maxs.x ) &&
				intersect_axis( box_ray_origin.y, direction.y, hitbox.mins.y, hitbox.maxs.y ) &&
				intersect_axis( box_ray_origin.z, direction.z, hitbox.mins.z, hitbox.maxs.z );
		};

		math::vector3 forward{}, left{}, up{};
		math::helpers::angle_vectors_left( aim_angle, &forward, &left, &up );

		// Every candidate in a scan uses the same weapon state. The engine spread
		// function is much more expensive than the capsule test, so calculate each
		// deterministic seed once and reuse it for all candidate points.
		struct spread_cache
		{
			std::uintptr_t weapon{};
			float inaccuracy{};
			float spread{};
			float recoil_index{};
			int item_def_idx{};
			int num_bullets{};
			int count{};
			bool initialized{};
			std::array<math::vector2, 512> values{};
		};

		thread_local spread_cache cache{};
		if ( !cache.initialized || cache.weapon != this->m_ctx.weapon || cache.inaccuracy != inaccuracy || cache.spread != spread ||
			cache.recoil_index != this->m_ctx.recoil_index || cache.item_def_idx != this->m_ctx.item_def_idx ||
			cache.num_bullets != this->m_ctx.num_bullets )
		{
			cache.weapon = this->m_ctx.weapon;
			cache.inaccuracy = inaccuracy;
			cache.spread = spread;
			cache.recoil_index = this->m_ctx.recoil_index;
			cache.item_def_idx = this->m_ctx.item_def_idx;
			cache.num_bullets = this->m_ctx.num_bullets;
			cache.count = 0;
			cache.initialized = true;
		}

		const auto cached_samples = std::min( samples, static_cast< int >( cache.values.size( ) ) );
		for ( auto i = cache.count; i < cached_samples; ++i )
		{
			cache.values[ i ] = this->calculate_spread( i, inaccuracy, spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );
		}
		cache.count = std::max( cache.count, cached_samples );

		auto hits{ 0 };

		for ( auto i = 0; i < samples; ++i )
		{
			const auto calculated_spread = i < cached_samples
				? cache.values[ i ]
				: this->calculate_spread( i, inaccuracy, spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );
			const auto direction = forward + ( left * calculated_spread.x ) + ( up * calculated_spread.y );
			const auto ray_end = direction.normalized( ) * 8192.0f;

			auto hit{ false };
			if ( is_capsule )
			{
				auto fraction{ 1.0f };
				hit = this->ray_vs_capsule( shoot_position, ray_end, capsule_start, capsule_end, hitbox.radius, fraction );
			}
			else
			{
				hit = ray_vs_box( ray_end );
			}

			if ( hit )
			{
				++hits;
			}

			// Exact early exit against the caller's gate threshold: when
			// the sampled hits already clear the final target even if
			// every remaining sample misses (or can no longer reach it
			// no matter how many hit), the outcome is decided and the
			// rest of the budget is skipped. These are hard bounds, not
			// heuristics - the pass/fail decision is identical to a full
			// sample run, but the common cases (center body points, out-
			// of-body angles) sample a fraction of the budget. This is
			// the hottest ragebot cost after the scan traces.
			if ( threshold >= 0.0f )
			{
				const auto threshold_total = threshold * static_cast< float >( samples );
				if ( static_cast< float >( hits ) >= threshold_total )
				{
					// Return the conservative lower bound (every remaining
					// sample misses) instead of hits/(i+1): the latter is
					// the rate of the samples actually drawn and reads as
					// ~100% on early exits, which inflated the reported
					// hitchance and the force-shot eligibility checks.
					return static_cast< float >( hits ) / static_cast< float >( samples );
				}

				if ( static_cast< float >( hits + ( samples - i - 1 ) ) < threshold_total )
				{
					return static_cast< float >( hits + ( samples - i - 1 ) ) / static_cast< float >( samples );
				}
			}
		}

		return static_cast< float >( hits ) / static_cast< float >( samples );
	}

	math::vector3 shared::find_spread_correction( const math::vector3& aim_angle, int tick, std::uint32_t known_seed ) const
	{
		// Restored from the previous build: full 360 deg / 0.5 deg sweep
		// (720 probes). The focused +/-6 deg grid that replaced it missed
		// the seed bucket on some weapons (large spread compensation, yaw-
		// dependent buckets) and returned an empty correction - which
		// silently dropped every no-spread shot and made the feature look
		// broken. The full sweep always finds the bucket the coarse grid
		// could jump over.
		//
		// Speed: the 720 probes walked pitch 0..359.5 in 0.5 deg steps,
		// which after the ±90 normalize visits every legal pitch TWICE -
		// half the engine calls were wasted on duplicate angles. The
		// sweep now probes the aim pitch neighborhood first (the seed
		// bucket flips locally around the corrected pitch) and only falls
		// back to the full legal pitch range (-89.5..89.5) when the
		// neighborhood misses - the common case drops from 720 engine
		// calls to ~40.
		const auto try_pitch = [ & ]( float pitch ) -> std::optional<math::vector3>
			{
				const auto test_angles = math::vector3{ pitch, aim_angle.y, 0.0f };
				const auto seed = this->get_spread_seed( test_angles, tick );

				// The engine call is SEH-guarded and returns 0 on failure;
				// skip rather than build a correction on a garbage seed.
				if ( seed == 0 )
				{
					return std::nullopt;
				}

				const auto spread = this->calculate_spread( seed, this->m_ctx.inaccuracy, this->m_ctx.spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );

				auto adj_angle = aim_angle;
				adj_angle.x += math::helpers::rad_to_deg( std::atan( std::sqrt( spread.x * spread.x + spread.y * spread.y ) ) );
				adj_angle.z = -math::helpers::rad_to_deg( std::atan2( spread.x, spread.y ) );

				if ( this->get_spread_seed( adj_angle, tick ) == seed )
				{
					return adj_angle;
				}

				return std::nullopt;
			};

		// Spread-cone lift of the aim itself (reuses the caller's seed
		// when available) - the neighborhood center.
		auto lift{ 0.0f };
		if ( const auto aim_seed = known_seed != 0 ? known_seed : this->get_spread_seed( aim_angle, tick ) )
		{
			const auto aim_spread = this->calculate_spread( aim_seed, this->m_ctx.inaccuracy, this->m_ctx.spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );
			lift = math::helpers::rad_to_deg( std::atan( std::sqrt( aim_spread.x * aim_spread.x + aim_spread.y * aim_spread.y ) ) );
		}

		const auto center = aim_angle.x + lift;

		// Tight neighborhood: +-2 deg in 0.5 deg steps (9 probes) around
		// the lifted pitch - the target bucket sits right there.
		for ( auto i = -4; i <= 4; ++i )
		{
			if ( auto correction = try_pitch( center + static_cast< float >( i ) * 0.5f ) )
			{
				return *correction;
			}
		}

		// Wider neighborhood: +-6 deg (24 more probes) for weapons with
		// a large cone whose lift shifts across buckets.
		for ( auto i = -12; i <= 12; ++i )
		{
			if ( std::abs( i ) <= 4 )
			{
				continue;
			}

			if ( auto correction = try_pitch( center + static_cast< float >( i ) * 0.5f ) )
			{
				return *correction;
			}
		}

		// Full legal pitch range: 0..179.5 deg (0.5 deg steps) covers
		// every pitch after the +-90 normalize - no duplicates.
		for ( auto i = 0; i < 360; ++i )
		{
			if ( auto correction = try_pitch( static_cast< float >( i ) / 2.0f ) )
			{
				return *correction;
			}
		}

		return {};
	}

	math::vector3 shared::get_eye_position( std::uintptr_t local_pawn ) const
	{
		const auto game_scene_node = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto view_offset = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
		return origin + view_offset;
	}

	math::vector3 shared::get_shoot_position( ) const
	{
		math::vector3 out{};
		memory::call_vfunc<void>( this->m_ctx.weapon_services, 29, reinterpret_cast< std::uintptr_t >( &out ) );
		return out;
	}

	math::vector3 shared::get_interpolated_shoot_position( std::uintptr_t local_pawn, bool newest ) const
	{
		const auto ws = this->m_ctx.weapon_services;
		const auto head = memory::read<int>( ws + 872 );
		const auto count = memory::read<int>( ws + 876 );

		if ( count < 1 )
		{
			return this->get_shoot_position( );
		}

		if ( newest )
		{
			const auto newest_idx = ( head + count - 1 ) % 32;
			const auto newest_off = ws + 232 + 20ull * newest_idx;
			return memory::read<math::vector3>( newest_off + 8 );
		}

		if ( count < 2 )
		{
			return this->get_shoot_position( );
		}

		const auto interp = memory::call<float>(PATTERN (patterns::get_interp_amount), local_pawn );
		const auto newest_idx = ( head + count - 1 ) % 32;
		const auto newest_off = ws + 232 + 20ull * newest_idx;
		const auto newest_tick = memory::read<int>( newest_off );
		const auto newest_frac = memory::read<float>( newest_off + 4 );

		const auto target = cstypes::tick_fraction{ newest_tick, newest_frac }.subtract_value( interp * 64.0f );

		for ( auto i = 0; i < count - 1; ++i )
		{
			const auto idx_a = ( head + static_cast< std::size_t >( i ) ) % 32;
			const auto idx_b = ( head + static_cast< std::size_t >( i ) + 1 ) % 32;

			const auto a_off = ws + 232 + 20ull * idx_a;
			const auto b_off = ws + 232 + 20ull * idx_b;

			const auto a_tick = memory::read<int>( a_off );
			const auto a_frac = memory::read<float>( a_off + 4 );
			const auto b_tick = memory::read<int>( b_off );
			const auto b_frac = memory::read<float>( b_off + 4 );

			const auto a_before = a_tick < target.tick || ( a_tick == target.tick && a_frac <= target.frac );
			if ( !a_before )
			{
				break;
			}

			const auto b_after = b_tick > target.tick || ( b_tick == target.tick && b_frac >= target.frac );
			if ( !b_after )
			{
				continue;
			}

			const auto a_pos = memory::read<math::vector3>( a_off + 8 );
			const auto b_pos = memory::read<math::vector3>( b_off + 8 );

			const auto span = cstypes::tick_fraction{ b_tick, b_frac }.subtract( { a_tick, a_frac } );
			const auto partial = target.subtract( { a_tick, a_frac } );

			const auto total_f = static_cast< float >( span.tick ) + span.frac;
			const auto partial_f = static_cast< float >( partial.tick ) + partial.frac;

			const auto t = total_f > 0.0f ? partial_f / total_f : 0.0f;

			return a_pos + ( b_pos - a_pos ) * t;
		}

		return this->get_shoot_position( );
	}

	int shared::calculate_stop_ticks( const math::vector3& velocity, float max_speed, std::uintptr_t local_pawn ) const
	{
		auto vel = velocity;
		vel.z = 0.0f;

		auto ticks{ 0 };
		const auto sv_friction = CONVAR ("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR ("sv_stopspeed")->get<float>( );
		const auto sv_accelerate = CONVAR ("sv_accelerate")->get<float>( );
		const auto surface_friction = systems::g_prediction.pre( ).surface_friction;
		const auto accurate_threshold = max_speed * 0.34f;

		const auto is_scoped = this->m_ctx.is_scoped;
		auto max_move_speed{ 250.0f };

		if ( is_scoped && local_pawn )
		{
			const auto movement_services = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
			if ( movement_services )
			{
				max_move_speed = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) );
			}
		}

		while ( vel.length_2d( ) > accurate_threshold && ticks < 15 )
		{
			const auto speed = vel.length_2d( );
			if ( speed <= 0.0f )
			{
				break;
			}

			const auto control = std::fmaxf( speed, sv_stopspeed );
			const auto drop = sv_friction * surface_friction * control * cstypes::tick_interval;
			auto new_speed = std::fmaxf( speed - drop, 0.0f );

			auto accel = sv_accelerate;

			if ( is_scoped )
			{
				const auto weapon_ratio = std::fminf( 1.0f, max_speed / 250.0f );
				const auto scoped_max = std::fmaxf( 250.0f, max_move_speed ) * weapon_ratio * 0.52f;

				if ( new_speed > scoped_max - 5.0f )
				{
					const auto t = 1.0f - std::fmaxf( 0.0f, new_speed - ( scoped_max - 5.0f ) ) / std::fmaxf( 0.01f, 5.0f );
					accel *= std::clamp( t, 0.0f, 1.0f );
				}
			}

			const auto accel_speed = std::fminf( accel * max_speed * surface_friction * cstypes::tick_interval, new_speed );
			new_speed = std::fmaxf( new_speed - accel_speed, 0.0f );

			vel *= ( new_speed / speed );
			ticks++;
		}

		return ticks;
	}

	float shared::get_spread( ) const
	{
		static const auto get_spread = PATTERN( patterns::get_spread );
		return detail::guarded( [ & ]( ) -> float
			{
				// The engine reads the weapon's live spread. A drift or a stale
				// weapon pointer must not feed NaN into the spread math
				// downstream, so non-finite results collapse to zero.
				const auto value = memory::call<float>( get_spread, this->m_ctx.weapon );
				return std::isfinite( value ) && value >= 0.0f ? value : 0.0f;
			} );
	}

	float shared::get_inaccuracy( bool update_accuracy_penalty ) const
	{
		const auto weapon = this->m_ctx.weapon;
		if ( !weapon )
		{
			return 0.0f;
		}

		// weapon_update_accuracy rewrites the accuracy state (turning, move,
		// air and recoil penalties) before the getter runs. When the update is
		// requested, the touched byte range is snapshotted and restored around
		// the engine calls, so the cheat observes a realistic value without
		// permanently mutating the weapon. The getter alone never mutates
		// anything, so the no-spread fast path (update_accuracy_penalty ==
		// false) skips the snapshot entirely.
		if ( !update_accuracy_penalty )
		{
			return detail::guarded( [ & ]( ) -> float
				{
					static const auto get_inaccuracy = PATTERN( patterns::get_inaccuracy );
					const auto value = memory::call<float>(
						get_inaccuracy, weapon,
						static_cast<float*>( nullptr ), static_cast<float*>( nullptr ) );
					return std::isfinite( value ) ? value : 0.0f;
				} );
		}

		const auto accuracy_state_begin = SCHEMA( "C_CSWeaponBase", "m_flTurningInaccuracyDelta"_hash );
		const auto accuracy_state_end = SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_hash );
		if ( accuracy_state_begin <= 0 || accuracy_state_end < accuracy_state_begin )
		{
			return 0.0f;
		}

		const auto accuracy_state_size = static_cast< std::size_t >( accuracy_state_end - accuracy_state_begin ) + sizeof( float );
		if ( accuracy_state_size > 0x100 )
		{
			return 0.0f;
		}

		// accuracy_state_size is bounded by 0x100 above; a stack buffer avoids
		// a heap allocation on every invocation (several per tick). The engine
		// calls sit in their own guard so the restore always runs even when an
		// engine-side exception was swallowed, and the whole sequence is
		// guarded again so a bad weapon pointer cannot take the process down.
		std::array<std::uint8_t, 0x100> backup{};
		return detail::guarded( [ & ]( ) -> float
			{
				std::memcpy( backup.data( ), reinterpret_cast< const void* >( weapon + accuracy_state_begin ), accuracy_state_size );

				const auto inaccuracy = detail::guarded( [ & ]( ) -> float
					{
						memory::call<void>(PATTERN (patterns::weapon_update_accuracy), weapon );

						static const auto get_inaccuracy = PATTERN( patterns::get_inaccuracy );
						const auto value = memory::call<float>(
							get_inaccuracy, weapon,
							static_cast<float*>( nullptr ), static_cast<float*>( nullptr ) );
						return std::isfinite( value ) ? value : 0.0f;
					} );

				std::memcpy( reinterpret_cast< void* >( weapon + accuracy_state_begin ), backup.data( ), accuracy_state_size );

				return inaccuracy;
			} );
	}

	float shared::get_inaccuracy_at_velocity( std::uintptr_t local_pawn, const math::vector3& velocity ) const
	{
		const auto accuracy_state_begin = SCHEMA( "C_CSWeaponBase", "m_flTurningInaccuracyDelta"_hash );
		const auto accuracy_state_end = SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_hash );
		if ( !this->m_ctx.weapon || !local_pawn || accuracy_state_begin <= 0 || accuracy_state_end < accuracy_state_begin )
		{
			return 0.0f;
		}

		const auto accuracy_state_size = static_cast< std::size_t >( accuracy_state_end - accuracy_state_begin ) + sizeof( float );
		if ( accuracy_state_size > 0x100 )
		{
			return 0.0f;
		}

		std::array<std::uint8_t, 0x100> backup{};
		std::memcpy( backup.data( ), reinterpret_cast< const void* >( this->m_ctx.weapon + accuracy_state_begin ), accuracy_state_size );

		const auto old_velocity = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
		const auto old_eflags = memory::read<std::uint32_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ) );

		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ), old_eflags & ~0x1000u );
		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ), velocity );

		// The engine call is guarded; the pawn fields and weapon accuracy state
		// are restored afterwards regardless of whether it succeeded.
		const auto inaccuracy = detail::guarded( [ & ]( ) -> float
			{
				memory::call<void>(PATTERN (patterns::weapon_update_accuracy), this->m_ctx.weapon );

				static const auto get_inaccuracy = PATTERN( patterns::get_inaccuracy );
				return memory::call<float>(
					get_inaccuracy, this->m_ctx.weapon,
					static_cast<float*>( nullptr ), static_cast<float*>( nullptr ) );
			} );

		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ), old_velocity );
		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ), old_eflags );

		std::memcpy( reinterpret_cast< void* >( this->m_ctx.weapon + accuracy_state_begin ), backup.data( ), accuracy_state_size );

		return inaccuracy;
	}

	float shared::get_air_inaccuracy( float vertical_speed, float jump_initial, float jump_apex ) const
	{
		constexpr auto sqrt_threshold{ 17.37795666f };
		const auto val = ( ( std::sqrtf( std::fabsf( vertical_speed ) ) - sqrt_threshold * 0.25f ) * ( jump_initial - jump_apex ) ) / ( sqrt_threshold * 0.75f ) + jump_apex;
		return std::clamp( val, 0.0f, jump_initial * 2.0f );
	}

	bool shared::can_shoot( systems::input::usercmd* cmd, std::uintptr_t local_controller, bool check_next_attack ) const
	{
		if ( this->m_ctx.weapon_type != cstypes::weapon_type::knife )
		{
			if ( memory::read<bool>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_bInReload"_hash ) ) )
			{
				return false;
			}

			if ( memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_hash ) ) <= 0 )
			{
				return false;
			}
		}

		if ( !check_next_attack )
		{
			return true;
		}

		const auto tick_base = memory::read<int>( local_controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto base_cmd = cmd->csgo_user_cmd.base( );
		const auto client_tick = base_cmd ? base_cmd->client_tick( ) : tick_base;
		const auto next_primary = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );

		// A garbage cooldown (negative schema field after a game update)
		// must never unlock continuous fire. A zero field happens on
		// weapon switch / respawn transitions where the cooldown has not
		// been stamped yet - treat it as "ready" and let the weapon-cycle
		// branch below rate-limit instead of blocking the shot entirely
		// (the old `<= 0` gate caused the occasional no-shots).
		if ( next_primary < 0 || next_primary - client_tick >= 256 )
		{
			return false;
		}

		if ( this->m_ctx.weapon_type == cstypes::weapon_type::knife )
		{
			const auto next_secondary = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_hash ) );
			return tick_base >= this->m_last_shoot_tick + 2 && ( client_tick >= next_primary || client_tick >= next_secondary );
		}

		// The +1 gap only guards against same-tick re-entry; the real fire
		// rate is bound by the server-side next-primary cooldown.
		const auto min_shot_gap = 1;

		// When next_primary lags (local prediction did not advance it, e.g.
		// our simulation restores the field), client_tick >= next_primary is
		// always true and the bot fires every tick - double-shots that the
		// server rejects and that show up as extra misses. Fall back to the
		// weapon cycle as the rate limit in that case.
		if ( client_tick - next_primary > 8 )
		{
			const auto cycle_time = this->m_ctx.weapon_vdata
				? memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flCycleTime"_hash ) )
				: 0.1f;
			const auto cycle_ticks = std::max( 2, static_cast< int >( std::ceil( cycle_time / cstypes::tick_interval ) ) );
			return tick_base >= this->m_last_shoot_tick + cycle_ticks && client_tick >= next_primary;
		}

		return tick_base >= this->m_last_shoot_tick + min_shot_gap && client_tick >= next_primary;
	}

	void shared::note_seed_shot( bool hit )
	{
		++this->m_seed_window_total;
		if ( hit )
		{
			++this->m_seed_window_hits;
		}

		// Sliding-window decay: every 32 confirmed results halve the older
		// half so the verdict follows the current server (and mode) instead
		// of stale history.
		if ( this->m_seed_window_total >= 32 )
		{
			this->m_seed_window_total = 16 + ( this->m_seed_window_total - 16 ) / 2;
			this->m_seed_window_hits /= 2;
		}

		// Not enough data yet - keep the current verdict.
		if ( this->m_seed_window_total < 10 )
		{
			return;
		}

		// Seed verification only admits shots the predicted seed lands, so
		// a trustworthy seed shows a near-100% hit rate. A sustained rate
		// far below that means the predicted seed does not match the
		// server's - stop gating on it.
		this->m_seed_synced = this->m_seed_window_hits * 2 >= this->m_seed_window_total;
	}

	bool shared::is_max_accuracy( float inaccuracy ) const
	{
		const auto& prestate = systems::g_prediction.pre( );
		const auto on_ground = ( prestate.flags & 1 ) != 0;
		const auto is_ducking = ( prestate.flags & 4 ) != 0;
		const auto speed = prestate.networked_velocity.length_2d( );

		if ( on_ground )
		{
			if ( this->m_ctx.weapon_type == cstypes::weapon_type::sniper )
			{
				// SSG 08 is accurate enough unscoped to land a headshot
				// (its unscoped spread floor is small), so it skips the
				// scope gate - otherwise the unscoped force path never
				// triggers and a visible head is never fired on.
				const auto is_ssg = this->m_ctx.item_def_idx == cstypes::item_definition_index::weapon_ssg_08;
				if ( !is_ssg && !this->m_ctx.is_scoped )
				{
					return false;
				}

				if ( is_ducking )
				{
					const auto rounded = std::floorf( inaccuracy * 300.0f ) / 300.0f;
					return rounded < inaccuracy;
				}

				if ( speed <= 0.1f )
				{
					const auto rounded = std::floorf( inaccuracy * 170.0f ) / 170.0f;
					return rounded < inaccuracy;
				}

				return false;
			}

			return speed <= this->m_ctx.weapon_max_speed * 0.34f;
		}

		const auto inaccuracy_jump_apex = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );
		const auto accuracy_penalty = memory::read<float>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_fAccuracyPenalty"_hash ) );
		const auto min_air_inaccuracy = accuracy_penalty + inaccuracy_jump_apex;

		constexpr auto tolerance{ 0.001f };
		return inaccuracy <= min_air_inaccuracy + tolerance;
	}

	math::vector3 shared::simulate_aim_punch( int recoil_index ) const
	{
		if ( recoil_index <= 0 || !this->m_ctx.valid )
		{
			return {};
		}

		const auto weapon_mode = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_weaponMode"_hash ) );
		const auto cycle_time = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flCycleTime"_hash ) );

		constexpr auto decay_rate{ 4.5f };
		constexpr auto decay2_exp{ 8.0f };
		constexpr auto decay2_lin{ 18.0f };
		constexpr auto recoil_scale{ 2.0f };

		math::vector3 punch{};
		math::vector3 punch_vel{};

		auto hybrid_decay = [ ]( math::vector3& v, float exp, float lin, float dt )
			{
				v *= std::expf( -exp * dt );

				const auto mag = v.length( );
				if ( mag > lin * dt )
				{
					v *= ( 1.0f - ( lin * dt ) / mag );
				}
				else
				{
					v = {};
				}
			};

		for ( auto i = 0; i < recoil_index; ++i )
		{
			float angle{}, magnitude{};
			memory::call<void>(PATTERN (patterns::weapon_get_recoil_offset), addresses::globals::weapon_recoil_data, this->m_ctx.weapon, weapon_mode, i, &angle, &magnitude );

			math::vector3 offset{};
			offset.x = std::cosf( math::helpers::deg_to_rad( angle ) ) * magnitude;
			offset.y = std::sinf( math::helpers::deg_to_rad( angle ) ) * magnitude;

			punch_vel -= offset;

			for ( auto time = 0.0f; time <= cycle_time; time += cstypes::tick_interval )
			{
				hybrid_decay( punch, decay2_exp, decay2_lin, cstypes::tick_interval );

				punch += punch_vel * cstypes::tick_interval * 0.5f;
				punch_vel *= std::expf( -decay_rate * cstypes::tick_interval );

				if ( punch_vel.length( ) < 0.03125f )
				{
					punch_vel = {};
				}

				punch += punch_vel * cstypes::tick_interval * 0.5f;
			}
		}

		return punch * recoil_scale;
	}

	bool shared::ray_vs_capsule( const math::vector3& ray_origin, const math::vector3& ray_dir, const math::vector3& capsule_a, const math::vector3& capsule_b, float radius, float& out_fraction ) const
	{
		const auto ab = capsule_b - capsule_a;
		const auto ab_sq = ab.dot( ab );
		const auto oc = ray_origin - capsule_a;
		const auto dir_sq = ray_dir.dot( ray_dir );

		if ( dir_sq < 1e-8f )
		{
			return false;
		}

		auto best_t{ 1.0f };
		auto hit{ false };

		if ( ab_sq > 1e-8f )
		{
			const float m = ab.dot( ray_dir ) / ab_sq;
			const float n = ab.dot( oc ) / ab_sq;

			const auto d_perp = ray_dir - ab * m;
			const auto oc_perp = oc - ab * n;

			const auto a = d_perp.dot( d_perp );
			const auto half_b = d_perp.dot( oc_perp );
			const auto c = oc_perp.dot( oc_perp ) - radius * radius;

			if ( a > 1e-8f )
			{
				const auto disc = half_b * half_b - a * c;
				if ( disc >= 0.0f )
				{
					const auto sqrt_disc = std::sqrt( disc );

					for ( int r = 0; r < 2; r++ )
					{
						const auto t = ( -half_b + ( r == 0 ? -sqrt_disc : sqrt_disc ) ) / a;
						if ( t < 0.0f || t >= best_t )
						{
							continue;
						}

						const auto s = m * t + n;
						if ( s >= 0.0f && s <= 1.0f )
						{
							best_t = t;
							hit = true;
							break;
						}
					}
				}
			}
		}

		const math::vector3 caps[ ]{ capsule_a, capsule_b };

		for ( int i = 0; i < 2; i++ )
		{
			const auto co = ray_origin - caps[ i ];
			const auto half_b = co.dot( ray_dir );
			const auto c = co.dot( co ) - radius * radius;
			const auto disc = half_b * half_b - dir_sq * c;

			if ( disc < 0.0f )
			{
				continue;
			}

			const auto sqrt_disc = std::sqrt( disc );

			for ( int r = 0; r < 2; r++ )
			{
				const auto t = ( -half_b + ( r == 0 ? -sqrt_disc : sqrt_disc ) ) / dir_sq;
				if ( t < 0.0f || t >= best_t )
				{
					continue;
				}

				if ( ab_sq > 1e-8f )
				{
					const auto hit_point = ray_origin + ray_dir * t - caps[ i ];
					const auto sign = i == 0 ? -1.0f : 1.0f;

					if ( sign * ab.dot( hit_point ) < 0.0f )
					{
						continue;
					}
				}

				best_t = t;
				hit = true;
				break;
			}
		}

		if ( hit )
		{
			out_fraction = best_t;
		}

		return hit;
	}

} // namespace features::combat
