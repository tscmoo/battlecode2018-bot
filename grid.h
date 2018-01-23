

struct tile {
	xy pos;
	bool walkable = false;
	bool visible = false;
	int karbonite = 0;

	unit* u = nullptr;
	int last_seen = 0;
	int last_invisible = 0;

	bc_MapLocation* loc = nullptr;
	int reachability_index = 0;
};

struct planet {
	int n = 0;
	int width = 0;
	int height = 0;
	std::vector<tile> tiles;
	int last_update = 0;

	distgrid_t movecost;

	size_t tile_index(unsigned x, unsigned y) {
		if (x >= (unsigned)width) x = width - 1;
		if (y >= (unsigned)height) y = height - 1;
		return y * (unsigned)width + x;
	}

	size_t tile_index(xy pos) {
		unsigned x = pos.x < 0 ? 0 : (unsigned)pos.x;
		unsigned y = pos.y < 0 ? 0 : (unsigned)pos.y;
		if (x >= (unsigned)width) x = width - 1;
		if (y >= (unsigned)height) y = height - 1;
		return y * (unsigned)width + x;
	}

	tile& get_tile(unsigned x, unsigned y) {
		return tiles[tile_index(x, y)];
	}

	tile& get_tile(xy pos) {
		return tiles[tile_index(pos)];
	}

	template<typename F>
	void for_each_neighbor_tile(xy pos, size_t index, F&& f) {
		const int width = this->width;
		const int height = this->height;
		if (pos.x != width - 1) {
			if (!f(tiles[index + 1])) return;
			if (pos.y != height - 1 && !f(tiles[index + 1 + width])) return;
			if (pos.y != 0 && !f(tiles[index + 1 - width])) return;
		}
		if (pos.y != height - 1) {
			if (!f(tiles[index + width])) return;
			if (pos.x != 0 && !f(tiles[index - 1 + width])) return;
		}
		if (pos.x != 0) {
			if (!f(tiles[index - 1])) return;
			if (pos.y != 0 && !f(tiles[index - 1 - width])) return;
		}
		if (pos.y != 0 && !f(tiles[index - width])) return;
	}

	template<typename F>
	void for_each_neighbor_tile(xy pos, F&& f) {
		for_each_neighbor_tile(pos, tile_index(pos), std::forward<F>(f));
	}
	template<typename F>
	void for_each_neighbor_tile(tile& t, F&& f) {
		for_each_neighbor_tile(t.pos, &t - tiles.data(), std::forward<F>(f));
	}

};

std::array<planet, 2> planets;

planet* earth = &planets[0];
planet* mars = &planets[1];

planet* current_planet = nullptr;

inline void update_tile(tile& t) {
	t.visible = bc_GameController_can_sense_location(gc, t.loc) != 0;
	if (t.visible) t.last_seen = current_frame;
	else t.last_invisible = current_frame;
	if (t.visible) {
		t.karbonite = bc_GameController_karbonite_at(gc, t.loc);
	}
}

std::array<tile*, 2500> tile_map{};

void grid_init() {

	current_planet = &planets[(int)bc_GameController_planet(gc)];

	earth->n = 0;
	mars->n = 1;
	for (auto& v : planets) {
		auto* planetmap = bc_GameController_starting_map(gc, (bc_Planet)v.n);

		v.width = bc_PlanetMap_width_get(planetmap);
		v.height = bc_PlanetMap_height_get(planetmap);

		log("map width %d height %d\n", v.width, v.height);

		v.tiles.resize(v.width * v.height);
		for (size_t i = 0; i != v.tiles.size(); ++i) {
			int x = (int)i % v.width;
			int y = (int)i / v.width;
			auto& t = v.tiles[i];
			t.pos.x = x;
			t.pos.y = y;
			t.loc = new_bc_MapLocation((bc_Planet)v.n, x, y);
			t.walkable = bc_PlanetMap_is_passable_terrain_at(planetmap, t.loc) != 0;
			t.karbonite = bc_PlanetMap_initial_karbonite_at(planetmap, t.loc);
			update_tile(t);
			int c = t.walkable && (!t.u || !t.u->is_building) ? 1 : inf_distance;
			v.movecost[distgrid_index(t.pos)] = c;

			if (&v == current_planet) {
				tile_map[distgrid_index(t.pos)] = &t;
			}
		}

		auto* units = bc_PlanetMap_initial_units_get(planetmap);
		size_t units_len = bc_VecUnit_len(units);
		for (size_t i = 0; i != units_len; ++i) {
			bc_Unit* src = bc_VecUnit_index(units, i);
			full_update_unit(src);
			delete_bc_Unit(src);
		}
		delete_bc_VecUnit(units);

		delete_bc_PlanetMap(planetmap);
	}

	for (auto& v : planets) {
		distgrid_t tmp;
		int next_index = 1;
		for (tile& t : v.tiles) {
			if (!t.walkable) continue;
			if (t.reachability_index == 0) {
				tmp.fill(inf_distance);
				generate_distance_grid(&v, std::array<xy, 1>({t.pos}), v.movecost, tmp);
				for (tile& t2 : v.tiles) {
					if (tmp[distgrid_index(t2.pos)] != inf_distance) t2.reachability_index = next_index;
				}
				++next_index;
			}
		}
	}

}

int last_update_tiles = 0;

void grid_update() {

	if (current_frame - last_update_tiles >= 1) {
		last_update_tiles = current_frame;

		for (auto& p : planets) {
			if (&p != current_planet) {
				if (p.last_update && current_frame - p.last_update < 90) continue;
			}
			p.last_update = current_frame;
			for (auto& t : p.tiles) {
				update_tile(t);
				int c = t.walkable && (!t.u || !t.u->is_building) ? 1 : inf_distance;
				p.movecost[distgrid_index(t.pos)] = c;
				t.u = nullptr;
			}
		}

	}

}

