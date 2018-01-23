
const int worker = 0;
const int knight = 1;
const int ranger = 2;
const int mage = 3;
const int healer = 4;
const int factory = 5;
const int rocket = 6;

struct unit_controller;
struct unit {
	int id;
	planet* p;
	xy pos;
	size_t index = 0;
	bool is_on_map = false;

	unit* loaded_into = nullptr;
	a_vector<unit*> loaded_units;

	bool visible = false;
	bool dead = false;
	bool gone = false;
	int creation_frame = 0;
	int last_seen = 0;
	int last_invisible = 0;
	int last_full_update = -1;

	int type = 0;
	int health = 0;
	int max_health = 0;
	int prev_health = 0;

	int last_took_damage = 0;

	int team = 0;
	bool is_mine = false;
	bool is_enemy = false;

	bool is_building = false;
	bool is_completed = false;

	int vision_range = 0;
	int damage = 0;
	int attack_range = 0;
	int movement_heat = 0;
	int attack_heat = 0;
	int movement_cooldown = 0;
	int attack_cooldown = 0;
	int ability_heat = 0;
	int ability_cooldown = 0;
	int ability_range = 0;

	bool factory_busy = false;
	int max_capacity = 0;

	int armor = 0;
	int combat_visit_n = 0;

	unit_controller* controller = nullptr;

	std::array<size_t, 11> container_index;

	unit() {
		container_index.fill((size_t)-1);
	}
};

a_list<unit> all_units;

a_unordered_map<int, unit*> unit_map;

std::array<a_vector<unit*>, 11> unit_containers;

a_vector<unit*>& my_units = unit_containers[0];
a_vector<unit*>& my_buildings = unit_containers[1];
a_vector<unit*>& my_workers = unit_containers[2];
a_vector<unit*>& my_planetless_units = unit_containers[3];

a_vector<unit*>& visible_units = unit_containers[4];
a_vector<unit*>& visible_buildings = unit_containers[5];
a_vector<unit*>& invisible_units = unit_containers[6];
a_vector<unit*>& live_units = unit_containers[7];

a_vector<unit*>& enemy_units = unit_containers[8];
a_vector<unit*>& enemy_buildings = unit_containers[9];
a_vector<unit*>& visible_enemy_units = unit_containers[10];

a_unordered_map<int, a_vector<unit*>> my_units_of_type;
a_unordered_map<int, a_vector<unit*>> my_completed_units_of_type;

void update_group(unit* u, a_vector<unit*>& group, bool contain) {
	size_t group_index = &group - unit_containers.data();
	bool contained = u->container_index[group_index] != (size_t)-1;
	if (contain == contained) return;
	if (contain) {
		u->container_index[group_index] = group.size();
		group.push_back(u);
	} else {
		size_t unit_index = std::exchange(u->container_index[group_index], (size_t)-1);
		if (u != group.back()) {
			group.back()->container_index[group_index] = unit_index;
			std::swap(group[unit_index], group.back());
		}
		group.pop_back();
	}
}

bool unit_is_in_group(unit* u, a_vector<unit*>& group) {
	size_t group_index = &group - unit_containers.data();
	return u->container_index[group_index] != (size_t)-1;
}

void update_groups(unit* u) {
	log("update_groups()\n");
	update_group(u, my_units, !u->dead && u->p == current_planet && u->is_mine);
	update_group(u, my_buildings, !u->dead && u->p == current_planet && u->is_mine && u->is_building);
	update_group(u, my_workers, !u->dead && u->p == current_planet && u->is_mine && u->type == worker);
	update_group(u, my_planetless_units, !u->dead && !u->p && u->is_mine);
	update_group(u, visible_units, !u->dead && u->p == current_planet && u->visible);
	update_group(u, visible_buildings, !u->dead && u->p == current_planet && u->visible && u->is_building);
	update_group(u, invisible_units, !u->dead && u->p == current_planet && !u->visible);
	update_group(u, live_units, !u->dead && u->p == current_planet);
	update_group(u, enemy_units, !u->dead && u->p == current_planet && u->is_enemy);
	update_group(u, enemy_buildings, !u->dead && u->p == current_planet && u->is_enemy && u->is_building);
	update_group(u, visible_enemy_units, !u->dead && u->visible && u->p == current_planet && u->is_enemy);
	log("~update_groups()\n");
}

void update_unit(unit* u, bc_Unit* src) {
	u->type = (int)bc_Unit_unit_type(src);

	u->visible = true;
	u->gone = false;
	u->dead = false;
	u->last_seen = current_frame;

	auto* loc = bc_Unit_location(src);

	u->is_on_map = bc_Location_is_on_map(loc);

	if (u->is_on_map) {
		auto* maploc = bc_Location_map_location(loc);
		u->p = &planets.at((int)bc_MapLocation_planet_get(maploc));
		u->pos.x = bc_MapLocation_x_get(maploc);
		u->pos.y = bc_MapLocation_y_get(maploc);
		u->index = distgrid_index(u->pos);
	} else {
		u->p = nullptr;
	}

	if (bc_Location_is_in_garrison(loc)) {
		auto* lsrc = bc_GameController_unit(gc, bc_Location_structure(loc));
		u->loaded_into = get_unit(lsrc);
		delete_bc_Unit(lsrc);
	} else u->loaded_into = nullptr;

	delete_bc_Location(std::exchange(loc, nullptr));

	u->team = (int)bc_Unit_team(src);
	u->is_mine = u->team == my_team;
	u->is_enemy = !u->is_mine;

	int health = bc_Unit_health(src);

	if (health < u->prev_health) {
		u->last_took_damage = current_frame;
		if (u->is_mine) total_damage_taken += u->prev_health - health;
	}

	u->prev_health = u->health;

	u->health = health;
	u->max_health = bc_Unit_max_health(src);

	u->is_building = u->type == factory || u->type == rocket;

	u->vision_range = bc_Unit_vision_range(src);
	u->loaded_units.clear();
	if (!u->is_building) {
		u->is_completed = true;
		u->damage = bc_Unit_damage(src);
		u->attack_range = bc_Unit_attack_range(src);
		u->movement_heat = bc_Unit_movement_heat(src);
		u->attack_heat = bc_Unit_attack_heat(src);
		u->movement_cooldown = bc_Unit_movement_cooldown(src);
		u->attack_cooldown = bc_Unit_attack_cooldown(src);
		u->ability_heat = bc_Unit_ability_heat(src);
		u->ability_cooldown = bc_Unit_ability_cooldown(src);
		u->ability_range = bc_Unit_ability_range(src);
		u->max_capacity = 0;
	} else {
		u->is_completed = bc_Unit_structure_is_built(src);

		auto* loaded_vec = bc_Unit_structure_garrison(src);
		if (loaded_vec) {
			size_t len = bc_VecUnitID_len(loaded_vec);
			for (size_t i = 0; i != len; ++i) {
				bc_Unit* src = bc_GameController_unit(gc, bc_VecUnitID_index(loaded_vec, i));
				u->loaded_units.push_back(full_update_unit(src));
				delete_bc_Unit(src);
			}
			delete_bc_VecUnitID(loaded_vec);
		}

		u->max_capacity = bc_Unit_structure_max_capacity(src);
	}

	u->factory_busy = u->type == factory && bc_Unit_is_factory_producing(src);
	u->armor = u->type == knight ? bc_Unit_knight_defense(src) : 0;

}

a_vector<unit*> new_units;

unit* new_unit(int id, bc_Unit* src) {
	all_units.emplace_back();
	unit* r = &all_units.back();
	unit_map[id] = r;
	r->id = id;
	r->creation_frame = current_frame;
	update_unit(r, src);
	return r;
}

unit* get_unit(bc_Unit* src) {
	int id = bc_Unit_id(src);
	auto*& r = unit_map[id];
	if (!r) {
		r = new_unit(id, src);
		new_units.push_back(r);
	}
	return r;
}

void units_init() {


}

void on_unit_destroyed(bc_Unit* src) {
	get_unit(src)->dead = true;
}

unit* full_update_unit(bc_Unit* src) {
	unit* u = get_unit(src);
	update_unit(u, src);
	update_groups(u);

	if (u->p) {
		u->p->get_tile(u->pos).u = u;
	}

	if (u->is_mine && u->last_full_update != current_frame) {
		my_units_of_type[u->type].push_back(u);
		if (u->is_completed) my_completed_units_of_type[u->type].push_back(u);

		u->controller = get_unit_controller(u);
	}
	u->last_full_update = current_frame;
	return u;
}

a_vector<unit*> previously_visible_units;

void units_update() {

	for (auto& v : my_units_of_type) {
		v.second.clear();
	}
	for (auto& v : my_completed_units_of_type) {
		v.second.clear();
	}

	previously_visible_units.clear();
	for (unit* u : visible_units) {
		if (!u->visible) error("visible unit is invisible");
		u->visible = false;
		previously_visible_units.push_back(u);
	}

	auto* units = bc_GameController_units(gc);
	size_t units_len = bc_VecUnit_len(units);
	for (size_t i = 0; i != units_len; ++i) {
		bc_Unit* src = bc_VecUnit_index(units, i);
		full_update_unit(src);

		delete_bc_Unit(src);
	}
	delete_bc_VecUnit(units);

	for (unit* x : visible_units) {
		int n = 0;
		for (unit* x2 : visible_units) {
			if (x == x2) ++n;
		}
		if (n != 1) error("found unit %d times\n", n);
	}

	for (unit* u : new_units) {
		log("found a new unit of type %d\n", u->type);
	}
	new_units.clear();

	for (unit* u : previously_visible_units) {
		if (!u->visible) {
			if (u->is_mine) {
				total_damage_taken += u->health;
				++units_lost;
				u->dead = true;
			}
			log("%d %p is no longer visible\n", u->type, u);
			update_groups(u);
			for (unit* x : visible_units) {
				if (x == u) error("unit is still in visible_units");
			}
		}
	}

	auto invisible_units_copy = invisible_units;
	for (unit* u : invisible_units_copy) {
		u->last_invisible = current_frame;
		auto f = [&]() {
			if (u->type == worker) return false;
			if (current_frame - u->last_seen < 20) return false;
			bool r = false;
			u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
				if (t.visible && current_frame - t.last_invisible > 10) {
					r = true;
					return false;
				}
				return true;
			});
			return r;
		};
		if (!u->gone && (!u->is_on_map || u->p->get_tile(u->pos).visible || f())) {
			log("%d %p is now gone\n", u->type, u);
			u->dead = true;
			u->gone = true;
			update_groups(u);
		}
	}

}


