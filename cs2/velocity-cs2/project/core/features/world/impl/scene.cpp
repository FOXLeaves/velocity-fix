#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>
#include "../world.hpp"

namespace features::world {


	void scene::update_gradient_fog () const {
		static const auto o_fog_start = SCHEMA ("C_GradientFog", "m_flFogStartDistance"_hash);
		static const auto o_fog_end = SCHEMA ("C_GradientFog", "m_flFogEndDistance"_hash);
		static const auto o_height_fog = SCHEMA ("C_GradientFog", "m_bHeightFogEnabled"_hash);
		static const auto o_far_z = SCHEMA ("C_GradientFog", "m_flFarZ"_hash);
		static const auto o_max_opacity = SCHEMA ("C_GradientFog", "m_flFogMaxOpacity"_hash);
		static const auto o_falloff = SCHEMA ("C_GradientFog", "m_flFogFalloffExponent"_hash);
		static const auto o_vertical = SCHEMA ("C_GradientFog", "m_flFogVerticalExponent"_hash);
		static const auto o_color = SCHEMA ("C_GradientFog", "m_fogColor"_hash);
		static const auto o_strength = SCHEMA ("C_GradientFog", "m_flFogStrength"_hash);
		static const auto o_start_disabled = SCHEMA ("C_GradientFog", "m_bStartDisabled"_hash);
		static const auto o_enabled = SCHEMA ("C_GradientFog", "m_bIsEnabled"_hash);

		if (!o_fog_end || !o_max_opacity || !o_color || !o_enabled) {
			return;
		}

		const auto& w = settings::g_world.m_weather;

		const auto draw_distance = w.fog_draw_distance.value;
		const auto falloff = 0.5f + w.fog_anisotropy.value * 8.0f;
		const auto vertical_exp = 0.75f + (1.0f - w.fog_anisotropy.value) * 5.25f;

		const auto& fog_rgba = w.fog_color.value;

		for (auto i = 0; i < 2048; ++i) {
			const auto entity = systems::g_entities.get_by_index (i);
			if (!entity) {
				continue;
			}

			const auto* name = systems::g_entities.get_schema_name (entity);
			if (!name) {
				continue;
			}

			if (fnv1a::runtime_hash (name) != "C_GradientFog"_hash) {
				continue;
			}

			if (o_fog_start) {
				memory::write<float> (entity + o_fog_start, 0.0f);
			}

			memory::write<float> (entity + o_fog_end, draw_distance);

			if (o_height_fog) {
				memory::write<bool> (entity + o_height_fog, false);
			}

			if (o_far_z) {
				memory::write<float> (entity + o_far_z, draw_distance);
			}

			memory::write<float> (entity + o_max_opacity, w.fog_density.value);

			if (o_falloff) {
				memory::write<float> (entity + o_falloff, falloff);
			}

			if (o_vertical) {
				memory::write<float> (entity + o_vertical, vertical_exp);
			}

			memory::write<xdraw::color> (entity + o_color, fog_rgba);

			if (o_strength) {
				memory::write<float> (entity + o_strength, 1.0f);
			}

			if (o_start_disabled) {
				memory::write<bool> (entity + o_start_disabled, false);
			}

			memory::write<bool> (entity + o_enabled, true);
		}
	}

	void scene::discover_skyboxes () {
		this->m_skyboxes.clear ();

		constexpr auto skybox_directory {"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Counter-Strike Global Offensive\\game\\csgo\\materials\\skybox"};

		WIN32_FIND_DATAA find_data {};
		const auto handle = FindFirstFileA ((std::string (skybox_directory) + xs ("\\*.vtex_c")).c_str (), &find_data);

		if (handle == INVALID_HANDLE_VALUE) {
			logging::console::print (xs ("failed to enumerate skybox directory."));
			return;
		}

		const auto strip_suffix = [] (std::string& name, std::initializer_list<const char*> suffixes) {
			for (const auto* suffix : suffixes) {
				if (name.size () >= strlen (suffix) && name.compare (name.size () - strlen (suffix), strlen (suffix), suffix) == 0) {
					name.resize (name.size () - strlen (suffix));
					return;
				}
			}
		};

		const auto strip_hash = [] (std::string& name) {
			const auto pos = name.rfind ('_');
			if (pos != std::string::npos && name.size () - pos - 1 == 8) {
				name.resize (pos);
			}
		};

		do {
			std::string filename = find_data.cFileName;

			auto resource_path = xs ("materials/skybox/") + filename;
			if (resource_path.size () > 2 && resource_path.compare (resource_path.size () - 2, 2, xs ("_c")) == 0) {
				resource_path.resize (resource_path.size () - 2);
			}

			auto display_name = filename;
			strip_suffix (display_name, {".vtex_c"});
			strip_hash (display_name);
			strip_suffix (display_name, {"_cube_tga", "_cube_pfm", "_png", "_exr", "_pfm", "_tga"});

			if (display_name.empty ()) {
				continue;
			}

			this->m_skyboxes.push_back ({std::move (display_name), std::move (resource_path)});

		} while (FindNextFileA (handle, &find_data));

		FindClose (handle);

		std::ranges::sort (this->m_skyboxes, {}, &skybox_entry::display_name);

		const auto to_remove = std::ranges::unique (this->m_skyboxes, {}, &skybox_entry::display_name);
		this->m_skyboxes.erase (to_remove.begin (), to_remove.end ());
	}

	void scene::reset_skybox_state () {
		this->m_custom_sky_resource = 0;
		this->m_original_sky_resource = 0;
		this->m_loaded_skybox_index = -1;
	}

	void scene::on_frame_stage_notify () {
		const auto local = systems::g_local.get ();

		if (settings::g_world.m_weather.fog_enabled.value && local.pawn && local.is_alive) {
			this->update_gradient_fog ();
		}

		if (!local.pawn || !local.is_alive) {
			this->reset_skybox_state ();
			return;
		}

		const auto& config = settings::g_world.m_scene.skybox;
		if (!config.custom_skybox || this->m_skyboxes.empty ()) {
			return;
		}

		if (config.selected_skybox != this->m_loaded_skybox_index) {
			const auto idx = std::clamp (config.selected_skybox.value, 0, static_cast<int>(this->m_skyboxes.size ()) - 1);
			this->load_skybox_material (this->m_skyboxes [idx].resource_path.c_str ());
			this->m_loaded_skybox_index = idx;
		}
	}

	void scene::on_draw_skybox_array_pre (std::uintptr_t mesh_array, int mesh_count) {
		this->m_active_texture_binding = 0;
		this->m_active_original_resource = 0;

		if (!mesh_array || mesh_count <= 0 || !systems::g_local.get ().pawn) {
			return;
		}

		if (!this->m_custom_sky_resource) {
			return;
		}

		const auto& config = settings::g_world.m_scene.skybox;
		if (!config.custom_skybox) {
			return;
		}

		const auto our_pdata = memory::read<std::uintptr_t> (this->m_custom_sky_resource);
		if (!our_pdata) {
			return;
		}

		const auto skybox_object = memory::read<std::uintptr_t> (mesh_array + (static_cast<std::size_t>(mesh_count) * 0x68) - 0x50);
		if (!skybox_object) {
			return;
		}

		const auto material_binding = memory::read<std::uintptr_t> (skybox_object + 0xD0);
		if (!material_binding) {
			return;
		}

		const auto material = memory::read<std::uintptr_t> (material_binding);
		if (!material) {
			return;
		}

		const auto texture_binding = memory::read<std::uintptr_t> (material + 0x210);
		if (!texture_binding) {
			return;
		}

		const auto current_resource = memory::read<std::uintptr_t> (texture_binding);
		if (!current_resource || current_resource == our_pdata) {
			return;
		}

		this->m_active_texture_binding = texture_binding;
		this->m_active_original_resource = current_resource;

		memory::write<std::uintptr_t> (texture_binding, our_pdata);
	}

	void scene::on_draw_skybox_array_post (std::uintptr_t mesh_array, int mesh_count) {
		if (this->m_active_texture_binding && this->m_active_original_resource) {
			const auto current = memory::read<std::uintptr_t> (this->m_active_texture_binding);
			const auto our_pdata = this->m_custom_sky_resource ? memory::read<std::uintptr_t> (this->m_custom_sky_resource) : 0;

			if (current == our_pdata && our_pdata != 0) {
				memory::write<std::uintptr_t> (this->m_active_texture_binding, this->m_active_original_resource);
			}

			this->m_active_texture_binding = 0;
			this->m_active_original_resource = 0;
		}

		if (!mesh_array || mesh_count <= 0 || !settings::g_world.m_scene.skybox.custom_color.value) {
			return;
		}

		const auto skybox_object = memory::read<std::uintptr_t> (mesh_array + (static_cast<std::size_t>(mesh_count) * 0x68) - 0x50);
		if (!skybox_object) {
			return;
		}

		const auto color = settings::g_world.m_scene.skybox.skybox_color.value.to_float ();

		memory::write<float> (skybox_object + 0xE8, color [0]);
		memory::write<float> (skybox_object + 0xEC, color [1]);
		memory::write<float> (skybox_object + 0xF0, color [2]);
	}

	void scene::on_light_scene_object_pre (std::uintptr_t object) const {
		if (!object || !settings::g_world.m_scene.lighting.value) {
			return;
		}

		const auto color = settings::g_world.m_scene.lighting_color.value.to_float ();

		memory::write<float> (object + 0xe4, color [0] * settings::g_world.m_scene.lighting_intensity);
		memory::write<float> (object + 0xe8, color [1] * settings::g_world.m_scene.lighting_intensity);
		memory::write<float> (object + 0xec, color [2] * settings::g_world.m_scene.lighting_intensity);
	}

	void scene::on_light_scene_object_post (std::uintptr_t object) const {
		if (!object || !settings::g_world.m_scene.lighting.value) {
			return;
		}

		//auto rotation = settings::g_world.m_scene.lighting_rotation;
		//rotation.normalize( );

		//memory::write<math::vector3>( object + 0x184, rotation );
	}

	void scene::on_draw_scene_object_array (std::uintptr_t object_array) const {
		if (!object_array || !settings::g_world.m_scene.world_setting.value) {
			return;
		}

		const auto object_data = memory::read<std::uintptr_t> (object_array + 0x8);
		if (!object_data) {
			return;
		}

		const auto light_data_queue = memory::read<std::uintptr_t> (addresses::globals::light_data_queue);
		if (!light_data_queue) {
			return;
		}

		const auto light_data_base = memory::read<std::uintptr_t> (light_data_queue + 0x18);
		if (!light_data_base) {
			return;
		}

		const auto count = memory::read<int> (object_data + 0x4);
		const auto index = memory::read<int> (object_data + 0x30);

		for (auto i = 0; i < count; ++i) {
			const auto color_addr = light_data_base + ((static_cast<std::size_t> (index) + i) << 5);
			memory::write<xdraw::color> (color_addr, {settings::g_world.m_scene.world_color.value.r,  settings::g_world.m_scene.world_color.value.g,  settings::g_world.m_scene.world_color.value.b, 255});
		}
	}

	void scene::on_draw_scene_object (std::uintptr_t batch, int batch_count) const {
		if (!batch) {
			return;
		}

		const auto& config = settings::g_world.m_scene.skybox;
		if (!config.custom_color.value && !settings::g_world.m_scene.world_setting.value) {
			return;
		}

		constexpr std::array cloud_materials
		{
			"materials/effects/clouds_001.vmat"_hash,
			"materials/de_vertigo/vertigo_clouds_001.vmat"_hash,
			"materials/models/props/de_nuke/hr_nuke/nuke_skydome_001/nuke_clouds_003.vmat"_hash,
			"materials/models/props/de_nuke/hr_nuke/nuke_skydome_001/nuke_clouds_002.vmat"_hash,
			"materials/models/props/de_nuke/hr_nuke/nuke_skydome_001/nuke_clouds_001.vmat"_hash
		};

		constexpr std::array sun_materials
		{
			"materials/sun/overlay.vmat"_hash,
			"materials/effects/glows/sun_glow_001.vmat"_hash,
			"materials/effects/glows/sun_disc_glow_001.vmat"_hash,
			"materials/effects/glows/sun_disc_glow_003.vmat"_hash,
			"materials/effects/glows/sun_disc_glow_004.vmat"_hash,
			"materials/de_train/hr_train_s2/effects/sun_disc_glow_01_clouded.vmat"_hash
		};

		for (auto i = 0; i < batch_count; ++i) {
			const auto mesh = batch + (static_cast<std::size_t> (i) * 0x68);
			const auto material = memory::read<std::uintptr_t> (mesh + 0x20);

			if (!material) {
				continue;
			}

			const auto material_hash = fnv1a::runtime_hash (memory::call_vfunc<const char*> (material, 0));
			if (!material_hash) {
				continue;
			}

			const auto is_cloud = std::ranges::contains (cloud_materials, material_hash);
			const auto is_sun = std::ranges::contains (sun_materials, material_hash);

			if ((is_cloud || is_sun) && config.custom_color.value) {
				const auto& color = is_cloud ? config.cloud_color.value : config.sun_color.value;
				memory::write<std::uint32_t> (mesh + 0x50, color);
			} else if (!is_cloud && !is_sun && settings::g_world.m_scene.world_setting.value) {
				memory::write<std::uint32_t> (mesh + 0x50, settings::g_world.m_scene.world_color.value);
			}
		}
	}

	void scene::on_get_scene_param (__m128* out_buffer, std::uint32_t hash) const {
		if (settings::g_world.m_scene.dof.value && hash == 0x2ACAB07C) {
			float* f = reinterpret_cast<float*>(out_buffer);
			f [0] = settings::g_world.m_scene.dof_near_blurry;
			f [1] = settings::g_world.m_scene.dof_near_crisp;
			f [2] = settings::g_world.m_scene.dof_far_crisp;
			f [3] = settings::g_world.m_scene.dof_far_blurry;
		}

		if (settings::g_world.m_scene.ambient.value && hash == 0xFF90C40E) {
			const auto& c = settings::g_world.m_scene.ambient_color;
			const float i = settings::g_world.m_scene.ambient_intensity;

			float* f = reinterpret_cast<float*>(out_buffer);
			f [0] = (c.value.r / 255.0f) * i;
			f [1] = (c.value.g / 255.0f) * i;
			f [2] = (c.value.b / 255.0f) * i;
			f [3] = (c.value.a / 255.0f) * i;
		}

		if (settings::g_world.m_weather.wind.value && hash == 0xEB0D997E) {
			float* f = reinterpret_cast<float*>(out_buffer);
			f [0] = settings::g_world.m_weather.wind_strength;
			f [1] = settings::g_world.m_weather.wind_frequency;
			f [2] = settings::g_world.m_weather.wind_strength;
			f [3] = settings::g_world.m_weather.wind_frequency;
		}
	}

	void scene::on_set_shader_param (__m128i*& value, std::uint32_t hash) const {
		static __m128 bloom_scale_val;
		static __m128 bloom_threshold_val;
		static __m128 bloom_width_val;
		static __m128 bloom_strength_val;
		static __m128 bloom_skybox_val;
		static __m128 gamma_val;
		static __m128 wetness_sky_val;
		static __m128 wetness_density_val;
		static __m128 wetness_timer_val;


		if (settings::g_world.m_scene.bloom.value) {
			const auto t = settings::g_world.m_scene.bloom_value;
			if (hash == 0x565EAF76) {
				bloom_scale_val = _mm_set_ps1 (0.3f + t * 1.2f);
				value = (__m128i*) & bloom_scale_val;
			} else if (hash == 0xBA98A9B0) {
				bloom_threshold_val = _mm_set_ps1 (1.5f - t * 1.2f);
				value = (__m128i*) & bloom_threshold_val;
			} else if (hash == 0x2AE72B37) {
				bloom_width_val = _mm_set_ps1 (0.5f + t * 1.5f);
				value = (__m128i*) & bloom_width_val;
			} else if (hash == 0xB692902E) {
				bloom_strength_val = _mm_set_ps1 (0.2f + t * 0.6f);
				value = (__m128i*) & bloom_strength_val;
			} else if (hash == 0x1313A424) {
				bloom_skybox_val = _mm_set_ps1 (0.1f + t * 0.4f);
				value = (__m128i*) & bloom_skybox_val;
			}
		}

		if (hash == 0x24470A87) {
			if (settings::g_world.m_scene.gamma.value)
				gamma_val = _mm_set_ps1 (settings::g_world.m_scene.gamma_value);
			else
				gamma_val = _mm_set_ps1 (5.0f); // default gamma
			value = (__m128i*) & gamma_val;
		}

		// always handle wetness hashes, write defaults when disabled
		if (hash == 0x374C1B3C) {
			wetness_sky_val = _mm_set_ps1 (settings::g_world.m_scene.wetness.value ? 1.0f : 0.0f);
			value = (__m128i*) & wetness_sky_val;
		} else if (hash == 0x0F592812) {
			wetness_density_val = _mm_set_ps1 (settings::g_world.m_scene.wetness.value
				? settings::g_world.m_scene.wetness_density : 0.0f);
			value = (__m128i*) & wetness_density_val;
		} else if (hash == 0x0A7F9FA6) {
			if (settings::g_world.m_scene.wetness.value) {
				const auto global_vars = memory::read<std::uintptr_t> (addresses::globals::global_vars);
				const auto current_time = memory::read<float> (global_vars + 0x30);
				wetness_timer_val = _mm_set_ps1 (current_time * settings::g_world.m_scene.wetness_speed);
			} else {
				wetness_timer_val = _mm_set_ps1 (0.0f);
			}
			value = (__m128i*) & wetness_timer_val;
		}
	}

	void scene::load_skybox_material (const char* path) {
		struct buffer_string {
			std::uint32_t m_unknown1 {};
			std::uint32_t m_unknown2 {0xc00000c8};

			union {
				std::uintptr_t m_str_ptr;
				std::uint8_t data [0xc8];
			};

			std::uintptr_t m_unknown3 {};
			std::uintptr_t m_unknown4 {};
		} buffer;

		memory::call<void> (PATTERN (patterns::init_particle_path_buffer), &buffer, path);
		buffer.m_unknown4 = 'xetv';

		memory::call<void> (PATTERN (patterns::resource_system_precache), addresses::globals::resource_system, &buffer, "");

		memory::call<void> (PATTERN (patterns::init_particle_path_buffer), &buffer, path);
		buffer.m_unknown4 = 'xetv';

		const auto binding = memory::call_vfunc<std::uintptr_t> (addresses::globals::resource_system, 79, &buffer, 0ll);
		if (binding && memory::read<std::uintptr_t> (binding)) {
			this->m_custom_sky_resource = binding;
		}
	}

} // namespace features::world