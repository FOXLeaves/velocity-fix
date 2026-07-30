#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::misc {

	static constexpr const char* k_setup_script = R"PANORAMA(
(function () {

	if (typeof SClient !== "undefined") {
		SClient = undefined;
	}

	SClient = (function () {
		var handlers = {};
		return {
			register_handler: function (type, callback) { handlers[type] = callback; },
			receive: function (msg) {
				if (msg && handlers[msg.type]) handlers[msg.type](msg);
			}
		};
	})();

	SWeaponManager = (function () {

		function getScoreboard() {
			var root = $.GetContextPanel();
			return root.FindChildTraverse("Scoreboard") || (root.id === "Scoreboard" ? root : null);
		}

		function getRow(sb, xuid) {
			return sb.FindChildTraverse("player-" + xuid) || sb.FindChildTraverse("id-" + xuid);
		}

		function getSize(w) {
			var t = w.type, p = w.path;
			if (t === 1) {
				if (p.indexOf("usp_silencer") !== -1) return "42px";
				if (p.indexOf("deagle")       !== -1) return "36px";
				if (p.indexOf("revolver")     !== -1) return "32px";
				if (p.indexOf("tec9")         !== -1) return "36px";
				if (p.indexOf("elite")        !== -1) return "32px";
				return "30px";
			}
			if (t === 2 || t === 3 || t === 4 || t === 5 || t === 6)
				return p.indexOf("mac10") !== -1 ? "30px" : "52px";
			if (t === 9) {
				if (p.indexOf("incgrenade")   !== -1 || p.indexOf("smokegrenade") !== -1) return "14px";
				if (p.indexOf("molotov")      !== -1 || p.indexOf("flashbang")    !== -1) return "16px";
				return "14px";
			}
			if (t === 7)  return "16px";
			if (t === 8)  return "22px";
			if (t === 11) return p.indexOf("healthshot") !== -1 ? "18px" : "14px";
			if (t === 0)  return "0px";
			return "16px";
		}

		function createWeaponIcon(parent, w, active_path) {
			var id   = "wep_" + w.path.replace(/[^a-zA-Z0-9]/g, "_");
			var slot = $.CreatePanel("Panel", parent, id);
			slot.style.height      = "fit-children";
			slot.style.width       = "fit-children";
			slot.style.verticalAlign = "center";
			slot.style.margin      = "0px 1px";

			var img        = $.CreatePanel("Image", slot, "img");
			img.style.verticalAlign = "center";
			img.scaling    = "stretch-aspect-preserve";

			var finalPath = w.path;
			if (finalPath.indexOf("file://") !== 0) {
				if (finalPath.indexOf("icons/equipment") === -1)
					finalPath = "icons/equipment/" + finalPath;
				if (finalPath.indexOf(".svg") === -1 && finalPath.indexOf(".vsvg") === -1)
					finalPath += ".svg";
				finalPath = "file://{images}/" + finalPath;
			}
			img.SetImage(finalPath);

			var sz           = getSize(w);
			img.style.height = "16px";
			img.style.width  = sz;
			slot.style.width = sz;
			slot.style.opacity = (w.path === active_path) ? "1.0" : "0.35";
		}

		return {

			update: function (xuid, weapons, active_path) {
				var sb = getScoreboard();
				if (!sb) return;

				var row = getRow(sb, xuid);
				if (!row) return;

				var nameIcons = row.FindChildTraverse("id-sb-name__nameicons");
				if (!nameIcons) return;

				var containerId = "custom-weapons-container-" + xuid;
				var container   = nameIcons.FindChildTraverse(containerId);

				if (!weapons || weapons.length === 0) {
					if (container) container.style.visibility = "collapse";
					return;
				}

				if (!container) {
					container = $.CreatePanel("Panel", nameIcons, containerId);
					container.AddClass("custom-weapons-container");
					container.style.flowChildren  = "none";
					container.style.height        = "20px";
					container.style.width         = "fit-children";
					container.style.verticalAlign = "center";
					container.style.marginLeft    = "3px";
				}

				container.style.visibility = "visible";
				container.RemoveAndDeleteChildren();

				var bg = $.CreatePanel("Panel", container, "box-bg-" + xuid);
				bg.style.width           = "100%";
				bg.style.height          = "100%";
				bg.style.backgroundColor = "rgba(0,0,0,0.35)";
				bg.style.borderRadius    = "3px";

				var border = $.CreatePanel("Panel", container, "box-border-" + xuid);
				border.style.width           = "100%";
				border.style.height          = "100%";
				border.style.backgroundColor = "rgba(0,0,0,0)";
				border.style.borderRadius    = "3px";
				border.style.border          = "1px solid rgba(255,255,255,0.18)";

				var content = $.CreatePanel("Panel", container, "box-content-" + xuid);
				content.style.flowChildren     = "right";
				content.style.height           = "100%";
				content.style.width            = "fit-children";
				content.style.padding          = "0px 4px";
				content.style.verticalAlign    = "center";
				content.style.horizontalAlign  = "center";

				var primary = [], pistols = [], equip = [];

				weapons.forEach(function (w) {
					if (w.type === 0) return;
					if (w.type === 2 || w.type === 3 || w.type === 4 || w.type === 5 || w.type === 6)
						primary.push(w);
					else if (w.type === 1)
						pistols.push(w);
					else
						equip.push(w);
				});

				primary.concat(pistols).concat(equip).forEach(function (w) {
					createWeaponIcon(content, w, active_path);
				});
			},

			clear: function () {
				var sb = getScoreboard();
				if (!sb) return;
				var containers = sb.FindChildrenWithClassTraverse("custom-weapons-container");
				for (var i = 0; i < containers.length; i++) containers[i].DeleteAsync(0);
			}
		};

	})();

	SClient.register_handler("updateWeapons", function (msg) {
		if (msg && msg.content)
			SWeaponManager.update(msg.content.xuid, msg.content.weapons, msg.content.active_path);
	});

	SClient.register_handler("clearWeapons", function (msg) {
		if (msg && msg.content)
			SWeaponManager.update(msg.content.xuid, [], "");
	});

})();
)PANORAMA";

	void c_ui_engine::run_script (c_ui_panel* panel, const char* script) {
		memory::call_vfunc<void> (
			reinterpret_cast<std::uintptr_t>(this), 77,
			panel, script,
			static_cast<const char*>(nullptr),
			static_cast<std::uint64_t>(0));
	}

	c_ui_engine* c_panorama_ui_engine::get_ui_engine () {
		return memory::call_vfunc<c_ui_engine*> (
			reinterpret_cast<std::uintptr_t>(this), 13);
	}

	void scoreboard_weapons::on_level_change () {
		m_script_injected = false;
		m_cache.clear ();
		m_throttle = 0;
		m_init_throttle = 0;
		m_ui_engine = nullptr;
		m_scoreboard_panel = nullptr;

		logging::console::print (xs ("[scoreboard_weapons] level change — state reset\n"));
	}

	void scoreboard_weapons::on_frame_stage_notify () {
		if (!settings::g_misc.m_scoreboard_weapons.enabled.value) {
			if (m_script_injected) {
				clear_all ();
				m_script_injected = false;
			}
			return;
		}

		const auto local = systems::g_local.get ();
		if (!local.is_valid ())
			return;

		if (!m_script_injected) {
			++m_init_throttle;
			if (m_init_throttle % 120 == 0)
				try_initialize ();

			if (!m_script_injected)
				return;
		}

		++m_throttle;
		if (m_throttle % 5 != 0)
			return;

		const auto players = systems::g_entities.get_by_type (systems::entities::type::player);
		for (const auto& player : players) {
			if (!player.ptr || player.ptr == local.controller)
				continue;

			send_player_weapons (player.ptr);
		}
	}

	void scoreboard_weapons::try_initialize () {
		if (m_script_injected)
			return;

		if (!addresses::globals::panorama) {
			logging::console::print (xs ("[scoreboard_weapons] panorama address not ready\n"));
			return;
		}

		auto* panorama = reinterpret_cast<c_panorama_ui_engine*>(addresses::globals::panorama);
		auto* ui_engine = panorama->get_ui_engine ();
		if (!ui_engine) {
			logging::console::print (xs ("[scoreboard_weapons] get_ui_engine returned null\n"));
			return;
		}

		c_ui_panel* scoreboard = nullptr;

		for (int i = 0; i < ui_engine->m_panel_count; i++) {
			panel_data_t* panel_data = &ui_engine->m_panels_array [i];
			if (!panel_data || !panel_data->m_panel)
				continue;

			c_ui_panel* panel = panel_data->m_panel;
			if (!panel || !panel->m_panel_name)
				continue;
			const std::string name {panel->m_panel_name};

			logging::console::print (xs ("[scoreboard_weapons] panel[{}] = '{}' ({:p})\n"),
				i, name, static_cast<const void*> (panel));
			if (fnv1a::runtime_hash (panel->m_panel_name) == fnv1a::runtime_hash ("Scoreboard")) {
				scoreboard = panel;
				break;
			}
		}


		if (!scoreboard) {
			logging::console::print (xs ("[scoreboard_weapons] Scoreboard panel not found (count=%d)\n"), ui_engine->m_panel_count);
			return;
		}

		m_ui_engine = ui_engine;
		m_scoreboard_panel = scoreboard;

		logging::console::print (xs ("[scoreboard_weapons] injecting setup script (engine=%p panel=%p)\n"),
			static_cast<void*>(m_ui_engine),
			static_cast<void*>(m_scoreboard_panel));

		run_script (k_setup_script);

		m_script_injected = true;
		logging::console::print (xs ("[scoreboard_weapons] setup script injected\n"));
	}

	void scoreboard_weapons::run_script (const std::string& script) {
		if (!m_ui_engine || !m_scoreboard_panel)
			return;

		m_ui_engine->run_script (m_scoreboard_panel, script.c_str ());
	}

	void scoreboard_weapons::send_player_weapons (std::uintptr_t controller) {
		if (!controller)
			return;

		const auto steamid = memory::read<std::uint64_t> (
			controller + SCHEMA ("CCSPlayerController", "m_steamID"_hash));
		if (!steamid)
			return;

		const auto pawn_handle = memory::read<std::uint32_t> (
			controller + SCHEMA ("CCSPlayerController", "m_hPlayerPawn"_hash));
		if (!pawn_handle)
			return;

		const auto pawn = systems::g_entities.lookup (pawn_handle);

		player_weapon_state state {};

		if (pawn) {
			const auto health = memory::read<int> (
				pawn + SCHEMA ("C_BaseEntity", "m_iHealth"_hash));

			if (health > 0) {
				const auto weapon_services = memory::read<std::uintptr_t> (
					pawn + SCHEMA ("C_BasePlayerPawn", "m_pWeaponServices"_hash));

				if (weapon_services) {
					// active weapon name
					const auto active_handle = memory::read<std::uint32_t> (
						weapon_services + SCHEMA ("CPlayer_WeaponServices", "m_hActiveWeapon"_hash));
					const auto active_weapon = active_handle
						? systems::g_entities.lookup (active_handle) : 0;

					if (active_weapon) {
						const auto vdata = memory::read<std::uintptr_t> (
							active_weapon + SCHEMA ("C_BaseEntity", "m_nSubclassID"_hash) + 0x8);
						if (vdata) {
							const auto name_ptr = memory::read<const char*> (
								vdata + SCHEMA ("CCSWeaponBaseVData", "m_szName"_hash));
							if (name_ptr) {
								auto name = memory::read_string (
									reinterpret_cast<std::uintptr_t>(name_ptr), 64);
								if (name.starts_with (xs ("weapon_")))
									name.erase (0, 7);
								state.active_name = std::move (name);
							}
						}
					}

					// weapon list
					const auto my_weapons_base = weapon_services + SCHEMA ("CPlayer_WeaponServices", "m_hMyWeapons"_hash);
					const auto weapon_count = memory::read<int> (my_weapons_base);
					const auto weapon_array_ptr = memory::read<std::uintptr_t> (my_weapons_base + 0x8);

					if (weapon_array_ptr && weapon_count > 0) {
						for (int j = 0; j < weapon_count && j < 16; ++j) {
							const auto wep_handle = memory::read<std::uint32_t> (
								weapon_array_ptr + j * sizeof (std::uint32_t));
							if (!wep_handle)
								continue;

							const auto weapon = systems::g_entities.lookup (wep_handle);
							if (!weapon)
								continue;

							const auto vdata = memory::read<std::uintptr_t> (
								weapon + SCHEMA ("C_BaseEntity", "m_nSubclassID"_hash) + 0x8);
							if (!vdata)
								continue;

							const auto wep_name_ptr = memory::read<const char*> (
								vdata + SCHEMA ("CCSWeaponBaseVData", "m_szName"_hash));
							if (!wep_name_ptr)
								continue;

							auto wep_name = memory::read_string (
								reinterpret_cast<std::uintptr_t>(wep_name_ptr), 64);
							if (!wep_name.starts_with (xs ("weapon_")))
								continue;
							wep_name.erase (0, 7);

							const auto wep_type = memory::read<std::uint32_t> (
								vdata + SCHEMA ("CCSWeaponBaseVData", "m_WeaponType"_hash));

							if (wep_type == 0)
								continue;

							state.weapons.push_back ({std::move (wep_name), static_cast<int>(wep_type)});
						}
					}
				}
			}
		}

		// skip if nothing changed
		const auto it = m_cache.find (steamid);
		if (it != m_cache.end () && it->second == state)
			return;

		m_cache [steamid] = state;

		if (state.weapons.empty ()) {
			send_clear (steamid);
			return;
		}

		// sort: primaries → pistols → grenades/other  (mirrors JS-side sort)
		auto sort_key = [] (int type) -> int {
			if (type == 2 || type == 3 || type == 4 || type == 5 || type == 6) return 0; // primary
			if (type == 1)                                                      return 1; // pistol
			return 2;                                                                       // grenades / equipment
		};
		std::stable_sort (state.weapons.begin (), state.weapons.end (),
			[&] (const weapon_entry& a, const weapon_entry& b) { return sort_key (a.type) < sort_key (b.type); });

		// build weapons JSON
		std::string weapons_json = "[";
		for (std::size_t i = 0; i < state.weapons.size (); ++i) {
			weapons_json += std::format (R"({{path:"{}",type:{}}})",
				state.weapons [i].name, state.weapons [i].type);
			if (i + 1 < state.weapons.size ())
				weapons_json += ",";
		}
		weapons_json += "]";

		const auto script = std::format (
			R"(if(typeof(SClient)!=='undefined'){{SClient.receive({{type:"updateWeapons",content:{{xuid:"{}",weapons:{},active_path:"{}"}}}});}})",
			steamid,
			weapons_json,
			state.active_name
		);

		run_script (script);
	}

	void scoreboard_weapons::send_clear (std::uint64_t steamid) {
		const auto script = std::format (
			R"(if(typeof(SClient)!=='undefined'){{SClient.receive({{type:"clearWeapons",content:{{xuid:"{}"}}}});}})",
			steamid
		);

		run_script (script);
	}

	void scoreboard_weapons::clear_all () {
		if (!m_script_injected)
			return;

		for (const auto& [steamid, _] : m_cache)
			send_clear (steamid);

		m_cache.clear ();
	}

} // namespace features::misc