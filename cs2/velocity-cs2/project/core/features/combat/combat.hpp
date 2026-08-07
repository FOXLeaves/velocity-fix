#pragma once

#include <core/systems/systems.hpp>

namespace features::combat {

	class shared
	{
	public:
		class lagcomp
		{
		public:
			struct record
			{
				std::uintptr_t pawn{};
				std::uintptr_t game_scene_node{};
				std::uintptr_t bone_cache{};
				int bone_count{};

				systems::bones::data bones[ 128 ]{};
				systems::bones::data bones_backup[ 128 ]{};

				math::vector3 origin{};
				math::vector3 rotation{};

				float simulation_time{};
				int tick{};
				bool valid{};
				bool was_valid{};
				bool is_applied{};
				bool extrapolated{};

				bool setup( std::uintptr_t pawn );
				[[nodiscard]] bool is_valid( ) const;

				void apply( );
				void restore( );
			};

			struct visual_record
			{
				math::vector3 origin{};
				std::array<systems::bones::data, 27> bones{};
			};

			struct extrapolation_data
			{
				math::vector3 origin{};
				math::vector3 velocity{};
				math::vector3 obb_mins{};
				math::vector3 obb_maxs{};
				std::uint32_t flags{};
				float sim_time{};
				float direction{};
			};

			void run( );

			[[nodiscard]] record* get_oldest_valid( std::uintptr_t pawn );
			[[nodiscard]] record* get_oldest_was_valid( std::uintptr_t pawn );
			[[nodiscard]] std::optional<visual_record> get_oldest_was_valid_visual( std::uintptr_t pawn ) const;
			[[nodiscard]] std::vector<record*> get_valid_records( std::uintptr_t pawn );
			[[nodiscard]] std::array<systems::bones::data, 27> get_skeleton( const record& record ) const;

			[[nodiscard]] std::optional<record> extrapolate( std::uintptr_t pawn );

			// Lightweight per-player snapshot for ESP displays (origin,
			// key bones + flags, copied under the lock).
			struct display_point
			{
				math::vector3 origin{};
				std::array<math::vector3, 27> bones{};
				int tick{};
				bool valid{};
				bool extrapolated{};
			};

			[[nodiscard]] std::vector<display_point> get_display_points( std::uintptr_t pawn ) const;

		private:
			void predict_movement( extrapolation_data& data, std::uintptr_t skip_entity ) const;

			// Per-player extrapolation cache: extrapolate() is called for
			// every enemy on every tick, and a full recompute runs up to
			// 18 hull traces per enemy - with a full server that is
			// hundreds of engine traces per tick (the frame drops).
			// Between ticks the server tick only advances by 1-2, so the
			// previous simulation state can be advanced incrementally
			// instead of being recomputed from scratch.
			struct extrap_cache_entry
			{
				extrapolation_data data{};
				int server_tick{};
				float speed{};
				float direction{};
				float direction_change{};
				bool valid{};
			};
			mutable std::unordered_map<std::uintptr_t, extrap_cache_entry> m_extrap_cache{};
			mutable std::mutex m_extrap_cache_mtx{};

			std::unordered_map<std::uintptr_t, std::deque<record>> m_records{};
			mutable std::shared_mutex m_records_mtx{};
		};

		class penetration
		{
		public:
			struct weapon_data
			{
				float damage;
				float penetration;
				float range_modifier;
				float range;
				float armor_ratio;
				float headshot_multiplier;
			};

			struct damage_scales
			{
				float ct_head;
				float t_head;
				float ct_body;
				float t_body;
			};

			struct run_context
			{
				std::uintptr_t target_pawn{};
				int target_armor{};
				int target_team{};
				bool has_helmet{};
				damage_scales scales{};
				float armor_ratio{};
				float headshot_multiplier{};
				systems::hitboxes::set hitboxes{};
				lagcomp::record* record{};
			};

			struct result
			{
				float damage{};
				int hitbox{ -1 };
				int hitgroup{ -1 };
				bool penetrated{};
			};

			void prepare( std::uintptr_t weapon_vdata, std::uintptr_t weapon );

			[[nodiscard]] run_context prepare_target( std::uintptr_t target_pawn, lagcomp::record* record, const systems::hitboxes::set* external_hitboxes = nullptr ) const;
			[[nodiscard]] bool run( const math::vector3& start, const math::vector3& end, const run_context& ctx, std::uintptr_t local_pawn, int local_team, result& out ) const;
			[[nodiscard]] bool can( const math::vector3& start, const math::vector3& direction, float& out_damage, const systems::local::snapshot& local ) const;
			[[nodiscard]] float get_max_damage( int hitgroup, int target_armor, bool has_helmet, int target_team ) const;
			[[nodiscard]] const weapon_data& get_weapon_data( ) const { return this->m_weapon_data; }

		private:
			void scale_damage( int hitgroup, int armor, bool has_helmet, int team, float armor_ratio, float headshot_multiplier, const damage_scales& scales, float& damage ) const;
			weapon_data m_weapon_data{};
		};

		class shoot_history
		{
		public:
			struct ring_entry
			{
				int tick{};
				float fraction{};
				math::vector3 position{};
			};

			struct eye_candidate
			{
				math::vector3 position{};
				int player_tick{};
				float player_frac{};
				int lerp_ticks_int{};
				float lerp_ticks_frac{};
				bool is_uninterpolated{};
			};

			struct eye_candidates
			{
				eye_candidate entries[ 2 ]{};
				int count{};
			};

			void snapshot( std::uintptr_t local_pawn, std::uintptr_t weapon_services );
			[[nodiscard]] eye_candidates get_candidates( ) const;
			// Linear-interpolated local shoot position at an arbitrary
			// (tick, fraction) from the recorded history. Used to aim from
			// the position the server lag-compensates the attacker back to
			// on a rewound attack - otherwise the "shoot position mismatch"
			// whiffs.
			[[nodiscard]] math::vector3 position_at( int tick, float frac ) const;
			[[nodiscard]] bool has_data( ) const { return this->m_count > 0; }
			[[nodiscard]] int client_tick( ) const { return this->m_client_tick; }
			[[nodiscard]] float client_tick_frac( ) const { return this->m_client_tick_frac; }
			[[nodiscard]] int server_tick( ) const { return this->m_server_tick; }
			[[nodiscard]] int lerp_ticks_int( ) const { return this->m_lerp_ticks_int; }
			[[nodiscard]] float lerp_ticks_frac( ) const { return this->m_lerp_ticks_frac; }
			[[nodiscard]] int count( ) const { return this->m_count; }
			[[nodiscard]] int oldest_tick( ) const { return this->m_count > 0 ? this->m_entries[ 0 ].tick : -1; }
			[[nodiscard]] int newest_tick( ) const { return this->m_count > 0 ? this->m_entries[ this->m_count - 1 ].tick : -1; }

		private:
			ring_entry m_entries[ 32 ]{};
			int m_count{};
			int m_client_tick{};
			float m_client_tick_frac{};
			int m_server_tick{};
			int m_lerp_ticks_int{};
			float m_lerp_ticks_frac{};
		};

		struct context
		{
			std::uintptr_t weapon{};
			std::uintptr_t weapon_services{};
			std::uintptr_t weapon_vdata{};
			std::uint32_t weapon_type{};
			std::uint16_t item_def_idx{};
			int num_bullets{};
			float recoil_index{};
			int current_tick{};
			int ticks_since_land{};
			float current_time{};
			float weapon_max_speed{};
			float range{};
			bool is_jump_scouting{};
			bool is_scoped{};
			bool valid{};

			float inaccuracy{};
			float spread{};
		};

		void update( );
		void invalidate_if_needed( );

		[[nodiscard]] context& ctx( ) { return this->m_ctx; }
		[[nodiscard]] penetration& pen( ) { return this->m_pen; }
		[[nodiscard]] lagcomp& lc( ) { return this->m_lc; }
		[[nodiscard]] shoot_history& sh( ) { return this->m_sh; }

		[[nodiscard]] bool autowalling( ) const { return this->m_autowalling; }
		[[nodiscard]] lagcomp::record* current_autowall_record( ) const { return this->m_current_autowall_record; }

		[[nodiscard]] int& last_shoot_tick( ) { return this->m_last_shoot_tick; }
		[[nodiscard]] float latency_budget( ) const { return this->m_latency_budget; }

		// Seed-sync tracking: servers that block client/server seed sync
		// make the predicted spread seed meaningless, so seed-mode
		// verification would gate shots on a random check. Feed it the
		// confirmed result of every seed-mode shot (hit/miss); when the
		// sustained hit rate collapses, seed_synced() flips false and the
		// ragebot stops gating on the predicted seed.
		void note_seed_shot( bool hit );
		[[nodiscard]] bool seed_synced( ) const { return this->m_seed_synced; }

		[[nodiscard]] std::uint32_t get_spread_seed( const math::vector3& angles, int tick ) const;
		[[nodiscard]] math::vector2 calculate_spread( int seed, float accuracy, float spread, float recoil_index, int item_def_idx, int num_bullets ) const;
		[[nodiscard]] math::vector3 get_aim_punch( std::uintptr_t local_pawn ) const;
		[[nodiscard]] float calculate_hitchance( const math::vector3& shoot_position, const math::vector3& aim_angle, const systems::hitboxes::entry& hitbox, const systems::bones::data& bone, float inaccuracy, float spread, int samples = 512 ) const;
		[[nodiscard]] math::vector3 find_spread_correction( const math::vector3& aim_angle, int tick ) const;
		[[nodiscard]] math::vector3 get_eye_position( std::uintptr_t local_pawn ) const;
		[[nodiscard]] math::vector3 get_shoot_position( ) const;
		[[nodiscard]] math::vector3 get_interpolated_shoot_position( std::uintptr_t local_pawn, bool newest = false ) const;
		[[nodiscard]] int calculate_stop_ticks( const math::vector3& velocity, float max_speed, std::uintptr_t local_pawn ) const;
		[[nodiscard]] float get_spread( ) const;
		[[nodiscard]] float get_inaccuracy( bool update_accuracy_penalty ) const;
		[[nodiscard]] float get_inaccuracy_at_velocity( std::uintptr_t local_pawn, const math::vector3& velocity ) const;
		[[nodiscard]] float get_air_inaccuracy( float vertical_speed, float jump_initial, float jump_apex ) const;
		[[nodiscard]] bool can_shoot( systems::input::usercmd* cmd, std::uintptr_t local_controller, bool check_next_attack = true ) const;
		[[nodiscard]] bool is_max_accuracy( float inaccuracy ) const;
		[[nodiscard]] math::vector3 simulate_aim_punch( int recoil_index ) const;

		bool ray_vs_capsule( const math::vector3& ray_origin, const math::vector3& ray_dir, const math::vector3& capsule_a, const math::vector3& capsule_b, float radius, float& out_fraction ) const;

	private:
		context m_ctx{};
		penetration m_pen{};
		lagcomp m_lc{};
		shoot_history m_sh{};

		// Parallel rage workers must not overwrite each other's trace record.
		inline static thread_local bool m_autowalling{};
		inline static thread_local lagcomp::record* m_current_autowall_record{ nullptr };

		int m_last_shoot_tick{};
		// Cached per tick: max-unlag minus latency. Recomputing the net channel
		// vfunc for every lag-comp record (dozens per tick) is wasteful.
		float m_latency_budget{ -1.0f };

		int m_seed_window_total{};
		int m_seed_window_hits{};
		bool m_seed_synced{ true };
	};

	class misc
	{
	private:
		class antiaim
		{
		public:
			void on_create_move( systems::input::usercmd* cmd );
			void on_render( xdraw::draw_list& draw_list ) const;

			[[nodiscard]] bool has_modified_angles( ) const { return this->m_should_correct || this->m_modified_angles.y != this->m_old_angles.y; }
			[[nodiscard]] const math::vector3& get_modified_angles( ) const { return this->m_modified_angles; }
			[[nodiscard]] bool antiaim_active( ) const { return this->m_antiaim_active; }
			// -1 = forced left (Z), +1 = forced right (C), 0 = none.
			[[nodiscard]] int yaw_side( ) const { return this->m_yaw_side; }
			// Command buttons captured before correct_movement rewrites the
			// move keys; the airstrafe reads these so its direction
			// bookkeeping (m_last_pressed) is not skewed by a 90 deg
			// side-step button rewrite.
			[[nodiscard]] std::uintptr_t raw_movement_buttons( ) const { return this->m_raw_movement_buttons; }
			// Rotates a move vector (forward/side) from the real-view frame
			// into the anti-aim frame. The airstrafe / test_strafer write
			// their outputs in the real frame; the engine decomposes along
			// the command angles, so the outputs must be rotated too or
			// bunnyhop accel steers off under anti-aim.
			void rotate_move_to_aa( proto::base_usercmd_pb* base ) const;
			// Inverse of rotate_move_to_aa: restores the real-view frame.
			// test_strafer steers the view in the real frame via subtick
			// yaw deltas, so the AA-rotated input must be brought back or
			// side-steps compound a 90 deg error (the 180 deg back mode
			// hides it by symmetry).
			void unrotate_move_from_aa( proto::base_usercmd_pb* base ) const;

		private:
			[[nodiscard]] float get_pitch( float view_pitch );
			[[nodiscard]] float get_yaw( const math::vector3& view_angles, const systems::local::snapshot& local );
			void correct_movement( systems::input::usercmd* cmd );
			[[nodiscard]] bool is_near_ladder( std::uintptr_t local_pawn ) const;

			math::vector3 m_old_angles{};
			math::vector3 m_modified_angles{};

			int m_yaw_side{};
			bool m_should_correct{};
			bool m_antiaim_active{};
			std::uintptr_t m_raw_movement_buttons{};
			float m_indicator_yaw{};
		};

		class duckpeek
		{
		public:
			void on_create_move( systems::input::usercmd* cmd );
			void on_override_view( std::uintptr_t view_setup );

		private:
			bool m_was_active{};
			bool m_fake_stand_active{};
		};

		class quickpeek
		{
		public:
			void on_create_move( systems::input::usercmd* cmd );
			void reset_if_needed( );

		private:
			static constexpr std::uint32_t invalid_effect_index{ static_cast<std::uint32_t>( -1 ) };

			void create_particle( );
			void update_particle( );
			void release_particle( );
			void reset( );

			math::vector3 m_saved_origin{};
			bool m_should_retrack{};
			bool m_fired{};
			bool m_active{};
			std::uint32_t m_particle_effect{ invalid_effect_index };
			bool m_particle_loaded{};
			std::uintptr_t m_prev_movement_bits{};
		};

		class autostop
		{
		public:
			void on_create_move( systems::input::usercmd* cmd );

		private:
			[[nodiscard]] float get_effective_accel_base( std::uintptr_t local_pawn, std::uintptr_t movement_services, std::uint32_t flags, float max_weapon_speed ) const;
		};

		antiaim m_antiaim{};
		duckpeek m_duckpeek{};
		quickpeek m_quickpeek{};
		autostop m_autostop{};

	public:
		[[nodiscard]] antiaim& antiaim( ) { return this->m_antiaim; }
		[[nodiscard]] duckpeek& duckpeek( ) { return this->m_duckpeek; }
		[[nodiscard]] quickpeek& quickpeek( ) { return this->m_quickpeek; }
		[[nodiscard]] autostop& autostop( ) { return this->m_autostop; }
	};

	class rage
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		void on_render( xdraw::draw_list& draw_list );

		// Auto scope: engages the sniper scope only when a viable target
		// is locked and the scope is not up yet; releases when no target
		// remains (the player regains manual control).
		void update_auto_scope( systems::input::usercmd* cmd, bool has_best );

		[[nodiscard]] bool should_stop( ) const noexcept { return this->m_should_stop; }
		[[nodiscard]] bool is_firing_this_tick( ) const noexcept { return this->m_firing_this_tick; }
		[[nodiscard]] std::uintptr_t last_aim_pawn( ) const noexcept { return this->m_last_aim_pawn; }
		[[nodiscard]] const math::vector3& last_aim_position( ) const noexcept { return this->m_last_aim_position; }

		// Per-tick extrapolated candidate records (populated while gathering
		// targets); used by the extrapolation display.
		[[nodiscard]] const std::vector<shared::lagcomp::record>& extrapolated_records( ) const noexcept { return this->m_extrapolated_records; }
		[[nodiscard]] bool is_cocking_revolver( ) const noexcept { return this->m_revolver_cock_ticks > 0; }
		[[nodiscard]] bool should_release_duck_for_shot( ) const noexcept { return this->m_release_duck_for_shot; }
		[[nodiscard]] bool duckpeek_wants_reduck( ) const noexcept { return this->m_duckpeek_reduck; }
		void clear_duckpeek_reduck( ) noexcept { this->m_duckpeek_reduck = false; }

		// Double tap: both shots of a pair ride in the SAME usercmd. The
		// input-history entries claim the attack ticks - entry 0 claims the
		// weapon-ready tick (last shot + fire interval, floored at the
		// server tick + 1), entry 1 claims one tick later with a half-tick
		// fraction, and the attack-start index points at the second entry.
		// The server then resolves both shots in one pass, one tick apart.
		// The charge preview renders the readiness (charging / ready) and
		// failures.
		class double_tap
		{
		public:
			enum class charge_state : std::uint8_t
			{
				// Charging: the weapon is still cooling down - the pair
				// cannot be fired yet.
				charging,
				// Ready: the weapon is up; firing this cmd sends both shots.
				ready,
				failed
			};

			enum class fail_reason : std::uint8_t
			{
				none,
				reloading,
				empty_clip
			};

			void on_create_move( systems::input::usercmd* cmd );
			void on_fired( systems::input::usercmd* cmd, bool fired );
			void on_render( xdraw::draw_list& draw_list ) const;

			[[nodiscard]] charge_state state( ) const noexcept { return this->m_state; }
			[[nodiscard]] float progress( ) const noexcept { return this->m_progress; }
			[[nodiscard]] int ticks_to_ready( ) const noexcept { return this->m_ticks_to_ready; }
			[[nodiscard]] fail_reason last_fail( ) const noexcept { return this->m_fail_reason; }
			// True while the auto second shot of a pair is pending: the
			// ragebot defers its own fire to this tick so the pair fires
			// like a manual double shot (first + auto second).
			[[nodiscard]] bool is_second_pending( ) const noexcept { return this->m_auto_second; }
			// The ragebot feeds its latest aim decision here every tick;
			// the auto second shot reuses it so it follows the CURRENT
			// target, not the first shot's stale angles.
			void update_aim( const math::vector3& angles ) noexcept
			{
				this->m_dt_aim = angles;
				this->m_has_dt_aim = true;
			}

			// Weapon-ready tick claimed on the input history. The server
			// resolves the attack against this tick, so the ragebot aligns
			// its scanned pose with it during double tap - otherwise the
			// rewound target pose never matches the aimed one and shots
			// miss. During the second-shot window (1-2 ticks after the
			// first shot) this returns the PREDICTED server cooldown so
			// the aim and the claim stay on the same tick - the pair lands
			// reliably without waiting for the sync.
			[[nodiscard]] int claimed_tick( ) const noexcept { return this->m_claimed_for_aim; }
			// Whether the current claimed tick is the PREDICTED cooldown
			// (second-shot window, local sync not yet arrived) - the
			// predicted claim sits ahead of the record grid, so pose
			// alignment is only meaningful there; normal/stale claims
			// (weapon ready for a long time) must never be gated.
			[[nodiscard]] bool claim_is_predicted( ) const noexcept { return this->m_claim_predicted; }
			// Tick fire_gun actually used for the no-spread seed correction
			// (stamp_tick, or the DT claim when valid). on_fired stamps the
			// input-history entries with this exact tick so the server
			// derives the spread seed from the same tick the correction
			// was computed against - otherwise the entries get stamped
			// with 0 / a stale claim and every no-spread bullet flies off.
			void set_no_spread_claim_tick( int tick ) noexcept { this->m_no_spread_claim_tick = tick; }
			[[nodiscard]] int no_spread_claim_tick( ) const noexcept { return this->m_no_spread_claim_tick; }
			// Tick of the last delivered attack (any rhythm) - used to
			// tell the "first shot of the pair" from follow-up attempts
			// inside the same fire cycle.
			[[nodiscard]] int last_fire_tick( ) const noexcept { return this->m_last_fire_tick; }
			// Weapon fire cycle in ticks (cooldown between two shots).
			[[nodiscard]] int cycle_ticks( ) const noexcept { return this->m_cycle_ticks; }

		private:
			void reset( ) noexcept;
			void fail( fail_reason reason ) noexcept;

			charge_state m_state{ charge_state::charging };
			fail_reason m_fail_reason{ fail_reason::none };
			std::uintptr_t m_weapon{};
			int m_shot_count{};
			int m_last_fire_tick{ -1 };
			// UC reference: static bool Fired - the alternating fire rhythm.
			bool m_fired{};
			// Optimization inside the UC structure: after the first shot of
			// a press the dt request stays forced so the alternating rhythm
			// delivers the second shot automatically; cleared after it.
			// Only applies while holding the left button (or the ragebot
			// firing); a single click restarts the charge instead.
			bool m_auto_second{};
			// Previous tick's left-attack state - distinguishes a hold from
			// a single click.
			bool m_prev_attack{};
			// First shot's aim angles (the silent aim angles live in the
			// entry) - reused by the auto second shot when the ragebot did
			// not fire again, so it aims at the target instead of along
			// the player's crosshair.
			math::vector3 m_last_angles{};
			bool m_has_last_angles{};
			// Freshest ragebot aim decision (fed every tick via update_aim)
			// - the auto second shot follows the CURRENT target.
			math::vector3 m_dt_aim{};
			bool m_has_dt_aim{};
			// Whether the last attack edge was manual (false = ragebot).
			// The auto second shot picks its angles accordingly: manual
			// pairs reuse the first shot's aim, ragebot pairs follow the
			// ragebot's current aim decision.
			bool m_last_shot_manual{};
			// Tick the weapon came off cooldown - the charge bar's second
			// pass (50 -> 100%) is pre-loaded from here, before any shot.
			int m_ready_tick{ -1 };
			// Weapon-ready tick (next attack + ratio carry + wat offset).
			int m_shoot_tick{ -1 };
			// Claimed tick for the input history AND the aim alignment:
			// the weapon-ready tick normally; during the second-shot
			// window (1-2 ticks after the last attack) the PREDICTED
			// server cooldown (ready tick + cycle) - but only while the
			// local ready tick has NOT synced yet (m_next_attack still
			// equals the last delivered claim). On local/zero-latency
			// servers the sync is nearly instant: predicting there would
			// overshoot by a full cycle, the server would rewind the
			// target to a future pose and the second bullet would fly
			// past a moving target.
			int m_claimed_for_aim{};
			// Whether m_claimed_for_aim is the predicted cooldown this tick
			// (see claim_is_predicted).
			bool m_claim_predicted{};
			// See set_no_spread_claim_tick.
			int m_no_spread_claim_tick{};
			// The claim actually sent with the last delivered attack -
			// used to detect whether the local ready tick has synced past
			// it (second-shot window prediction gate).
			int m_last_claimed_tick{ -1 };
			// Last tick's clip count - the difference is the number of
			// rounds that really left the barrel (charge tracking by ammo).
			int m_prev_clip{ -1 };
			int m_cycle_ticks{ 4 };
			int m_next_attack{};
			float m_ratio{};
			float m_wat_offset{};
			// Frames the ready/failed states stay visible in the preview.
			int m_state_hold{};
			int m_ticks_to_ready{};
			float m_progress{};
		} m_double_tap{};

		[[nodiscard]] double_tap& dt( ) noexcept { return this->m_double_tap; }
		[[nodiscard]] const double_tap& dt( ) const noexcept { return this->m_double_tap; }

		// Lag-compensation window: the server rewinds the hitbox for at
		// most 200 ms of game time (sv_maxunlag ceiling) - 12 full ticks
		// on a 64-tick official server (0.1875 s). Records deeper than
		// that can never be hit and only waste scan time.
		static constexpr auto k_max_lagcomp_records{ 12 };
		// Scanning the extrapolated prediction, the newest record and the
		// second-newest covers the useful poses: deep rewinds are not
		// honored by the server (attacker rewind / no hitbox replay), so
		// the near-live records are the reliable backtrack window.
		static constexpr auto k_max_scan_records{ 3 };
		// Engine trace budget per scan pass: every scanned point costs a
		// trace_bullet call (the hottest ragebot cost). With a crowded
		// server the unthrottled scan runs hundreds of them per tick and
		// the frame time spikes; the budget keeps the pass bounded while
		// leaving single-target scans fully unthrottled.
		static constexpr auto k_max_scan_traces{ 400 };

	private:
		struct aim_context
		{
			math::vector3 view_angles{};
			math::vector3 velocity{};

			float predicted_inaccuracy{};
			float spread{};

			float weapon_max_speed{};
			float accurate_threshold{};
			bool on_ground{};
			bool is_scoped{};
		};

		struct stop_prediction
		{
			math::vector3 eye{};
			float inaccuracy{};
		};

		struct candidate
		{
			std::uintptr_t pawn{};
			int health{};
			int armor{};
			float min_damage{};
			// Live network velocity of the target: moving targets benefit
			// from the extrapolated pose (the live record lags the network
			// delay), stationary ones do not.
			math::vector3 velocity{};
			systems::hitboxes::set hitboxes{};
			std::array<shared::lagcomp::record*, k_max_lagcomp_records> records{};
			int record_count{};
		};

		struct scan_hit
		{
			math::vector3 position{};
			math::vector3 aim_angle{};
			float damage{};
			float score{};
			float fov{};
			int hitbox_index{};
			int hitgroup{};
			int bone_index{};
			systems::hitboxes::entry hitbox{};
			bool is_center{};
			bool penetrated{};
			bool is_backstab{};
			int attack_type{};
			shared::shoot_history::eye_candidate source_eye{};

			std::uintptr_t pawn{};
			int health{};
			math::vector3 target_velocity{};
			shared::lagcomp::record* record{};
		};

		struct target
		{
			scan_hit hit{};
			float hitchance{};
			float score{};
			bool valid{};

			[[nodiscard]] bool is_lethal( ) const noexcept
			{
				return this->hit.damage >= static_cast< float >( this->hit.health );
			}
		};

		struct knife_info
		{
			bool can_slash{};
			bool can_stab{};
			bool charged{};
			float armor_ratio{};
		};

		[[nodiscard]] aim_context build_context( systems::input::usercmd* cmd, const systems::local::snapshot& local ) const;
		[[nodiscard]] std::optional<stop_prediction> predict_stop( const aim_context& ctx, const math::vector3& current_eye, const systems::local::snapshot& local ) const;
		[[nodiscard]] std::vector<candidate> gather_candidates( const systems::local::snapshot& local, float max_distance_sq = 0.0f ) const;

		bool run_gun( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local, bool allow_fire = true );
		void run_taser( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local );
		void run_knife( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local );
		void auto_revolver( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local );

		[[nodiscard]] std::vector<scan_hit> scan_players( const math::vector3& eye, float inaccuracy, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const;
		[[nodiscard]] std::vector<scan_hit> scan_player( const math::vector3& eye, float inaccuracy, const aim_context& ctx, candidate& cand, shared::lagcomp::record* record, const systems::local::snapshot& local, std::atomic<int>& trace_budget ) const;
		[[nodiscard]] target select_best( const aim_context& aim_ctx, const std::vector<scan_hit>& hits, float eval_inaccuracy ) const;
		[[nodiscard]] float evaluate_hitchance( const scan_hit& hit, const aim_context& ctx, float inaccuracy ) const;
		[[nodiscard]] float get_standing_inaccuracy( const systems::local::snapshot& local, const aim_context& ctx ) const;

		[[nodiscard]] std::vector<scan_hit> scan_taser( const math::vector3& eye, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const;

		[[nodiscard]] knife_info get_knife_info( const systems::local::snapshot& local ) const;
		[[nodiscard]] std::vector<scan_hit> scan_knife( const math::vector3& eye, const aim_context& ctx, const knife_info& info, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const;

		void fire_gun( systems::input::usercmd* cmd, const target& tgt, bool was_forced, const math::vector3& shoot_eye, const systems::local::snapshot& local, bool subtick_attack = false );
		void fire_melee( systems::input::usercmd* cmd, const target& tgt, const systems::local::snapshot& local );

		[[nodiscard]] std::vector<math::vector3> generate_multipoints( const systems::hitboxes::entry& hitbox, const math::vector3& center, const math::quaternion& bone_rot, float pointscale, const math::vector3& shoot_pos, float inaccuracy ) const;
		[[nodiscard]] bool should_stop_movement( const aim_context& ctx ) const;
		[[nodiscard]] float get_min_damage( const settings::combat::ragebot::weapon_group& config, int target_health, bool override_active ) const;
		[[nodiscard]] float get_knife_damage( float raw, int armor, float armor_ratio ) const;
		[[nodiscard]] systems::tracing::result trace_taser_hit( const math::vector3& origin, const math::vector3& forward, float range, std::uintptr_t target_pawn, std::uintptr_t local_pawn ) const;
		[[nodiscard]] systems::tracing::result trace_knife_hit( const math::vector3& origin, const math::vector3& forward, float reach, std::uintptr_t target_pawn, std::uintptr_t local_pawn ) const;

		enum class penetration_crosshair_state : std::uint8_t
		{
			unavailable,
			blocked,
			penetrable
		};

		void update_penetration_crosshair( const systems::local::snapshot& local );
		void draw_penetration_crosshair( xdraw::draw_list& draw_list ) const;

		void clear_attack_button( systems::input::usercmd* cmd ) const;

		bool m_should_stop{};
		bool m_firing_this_tick{};
		// Auto-scope session: set when the scope press edge was issued for
		// the current engagement; reset when the target disappears. The
		// edge fires once per session (not every tick), the held zoom bit
		// keeps the scope up, and it only arms while the weapon can attack.
		bool m_scope_open{};
		// Last aim point the ragebot fired at (for the ESP display).
		std::uintptr_t m_last_aim_pawn{};
		math::vector3 m_last_aim_position{};
		bool m_release_duck_for_shot{};
		bool m_duckpeek_reduck{};

		std::uint8_t m_knife_attack{};
		bool m_zeus_fired{};

		int m_revolver_cock_ticks{};
		std::atomic<penetration_crosshair_state> m_penetration_crosshair_state{ penetration_crosshair_state::unavailable };

		std::vector<shared::lagcomp::record> m_extrapolated_records{};
		mutable std::shared_mutex m_extrapolated_mtx{};

		// Pawn selected by the last target scan - with several viable
		// targets whose scores are close, keeping the current one prevents
		// the aim from re-aiming at a different target every tick (each
		// switch re-aligns pose/claim and tanks the hit rate mid-fight).
		mutable std::uintptr_t m_last_select_pawn{ 0 };

		struct debug_point
		{
			math::vector3 position{};
			int hitbox_index{};
			bool is_center{};
		};

		mutable std::vector<debug_point> m_debug_points{};
		mutable std::mutex m_debug_mtx{};
	};

	class legit
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		void on_render( xdraw::draw_list& draw_list );
		void invalidate_if_needed( );

		[[nodiscard]] bool has_target( ) const noexcept { return this->m_target.has_target( ); }

	private:
		struct scan_point
		{
			math::vector3 position{};
			float damage{};
			float fov{};
			int hitgroup{};
			std::size_t cfg_index{};
			int bone_index{};
			systems::hitboxes::entry hitbox{};
			bool visible{};
			bool is_center{};
			bool valid{};
		};

		struct target_result
		{
			std::uintptr_t pawn{};
			scan_point best_point{};
			math::vector3 aim_angle{};
			float hitchance{};
			float score{};
			float fov{};
			int health{};
			shared::lagcomp::record* record{};

			[[nodiscard]] bool has_target( ) const noexcept { return this->best_point.valid; }
		};

		[[nodiscard]] target_result find_target( const math::vector3& shoot_position, const math::vector3& view_angles, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local ) const;
		[[nodiscard]] scan_point scan_player( std::uintptr_t pawn, shared::lagcomp::record* record, const math::vector3& shoot_position, const math::vector3& view_angles, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local ) const;

		void apply_aimbot( systems::input::usercmd* cmd, const target_result& tgt, const math::vector3& view_angles, const math::vector3& aim_punch, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local );
		void apply_triggerbot( systems::input::usercmd* cmd, const math::vector3& shoot_position, const math::vector3& view_angles, const math::vector3& aim_punch, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local );
		void apply_rcs( math::vector3& aim_angle, const math::vector3& aim_punch, int rand_min, int rand_max ) const;

		void update_standalone_rcs( const math::vector3& view_angles, const math::vector3& aim_punch, int amount, int rand_min, int rand_max, bool apply, const systems::local::snapshot& local );
		[[nodiscard]] float compute_rcs_factor( int rand_min, int rand_max ) const;

		void draw_fov( xdraw::draw_list& draw_list, const math::vector3& view_angles, const math::vector3& aim_punch, float fov_degrees, const config::col& color, bool rcs_active ) const;

		[[nodiscard]] static int hitgroup_to_cfg( int hitgroup );

		target_result m_target{};
		math::vector3 m_old_punch{};
		mutable float m_last_significant_punch_time{};

		float m_remainder_x{};
		float m_remainder_y{};

		float m_trigger_delay_start{};
		float m_trigger_release_time{};
		float m_trigger_next_time{};
		std::uintptr_t m_trigger_pending_pawn{};

		math::vector3 m_cached_view_angles{};
		math::vector3 m_cached_aim_punch{};
	};

	class vac_bypass
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );

	private:
		void suppress_cheat_cvars( ) const;
	};

} // namespace features::combat
