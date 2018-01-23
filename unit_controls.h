

struct unit_controller {
	unit* u;

	xy target_pos;
	unit* target_unit = nullptr;
	int target_type = 0;

	int nomove_frame = 0;
	int last_action_frame = 0;
	int last_replicate = 0;
	int harvest_nomove_frame = 0;

	int goal_distance = 0;
	distgrid_t* distgrid = nullptr;

	int move_index = 0;
	int move_n = 4;
	std::array<int, 9> move_scores{};

	unit* heal_target = nullptr;
	int usable_counter = 0;

	int launch_counter = 0;
	int go_to_rocket_frame = 0;
};

std::list<unit_controller> all_unit_controllers;

unit_controller* get_unit_controller(unit* u) {
	if (u->controller) return u->controller;
	all_unit_controllers.emplace_back();
	unit_controller* c = &all_unit_controllers.back();
	u->controller = c;
	c->u = u;
	return c;
}


void change_unit_position(unit* u, xy to) {
	if ((size_t)to.x >= current_planet->width || (size_t)to.y >= current_planet->height) error("attempt to move unit outside map");
	auto& from_tile = u->p->get_tile(u->pos);
	auto& to_tile = u->p->get_tile(to);
	from_tile.u = nullptr;
	to_tile.u = u;
	u->pos = to_tile.pos;
	u->index = distgrid_index(to_tile.pos);
}

void move_units(bool check_damage = true) {

	if (my_units.empty()) return;

	std::array<unit_controller*, 2500> move{};

	std::array<unit_controller*, 2500> move_order{};
	size_t move_order_size = 0;

	size_t vec_size = 0;
	unit_controller* vec[2500];
	for (unit* u : my_units) {
		if (u->p != current_planet || !u->is_on_map) continue;
		vec[vec_size++] = u->controller;
		move[u->index] = u->controller;

//		if (u->pos == xy(7, 8)) {
//			log("entry move scores: \n");
//			for (auto& v : u->controller->move_scores) {
//				log(" %d\n", v);
//			}
//		}
	}

	std::sort(&vec[0], &vec[vec_size], [&](unit_controller* a, unit_controller* b) {
		unit* au = a->u;
		unit* bu = b->u;
		if ((au->type == knight) != (bu->type == knight)) return au->type == knight;
		if (au->movement_heat / 10 != bu->movement_heat / 10) return au->movement_heat / 10 > bu->movement_heat / 10;
		if ((au->type == mage) != (bu->type == mage)) return au->type == mage;
		if (au->type != bu->type) return au->type > bu->type;
		if (a->goal_distance != b->goal_distance) {
			if (au->type == worker) return a->goal_distance > b->goal_distance;
			else return a->goal_distance < b->goal_distance;
		}
		if (au->health != bu->health) return au->health > bu->health;
		return false;
	});


	bool fail = false;
	for (size_t i = 0; i != vec_size; ++i) {
		unit_controller* c = vec[i];
		c->move_index = i;
		if (c->u->movement_heat >= 10) {
			c->move_n = 4;
			c->move_index = -1;
		}

		if (!enemy_units.empty()) {
			bool any_non_inf = false;
			for (auto& v : c->move_scores) {
				if (v != inf_distance) any_non_inf = true;
			}
			if (!any_non_inf) {
				fail = true;
				log("%d at %d %d has no moves\n", c->u->type, c->u->pos.x, c->u->pos.y);
			}
		}
	}
	if (fail) error("fail");

	size_t width = current_planet->width;
	size_t height= current_planet->height;

	std::function<void(size_t, bool)> do_move = [&](size_t i, bool dont_stay_still) {

		for (size_t i = 0; i != vec_size; ++i) {
			unit_controller* c = vec[i];
			if (c->move_index == -1) continue;
			if (move[c->u->index] != c) error("mismatch in i %d\n", i);
		}

		unit_controller* c = vec[i];
		if (c->move_index == -1) return;
		unit* u = c->u;
		size_t start_index = u->index;
		size_t best_index = start_index;
		size_t best_move_n = 4;
		int best_score = 1 << 16;
		unit_controller* best_nc = nullptr;
		if (move[start_index] != c) error("move[start_index] != c");
		move[start_index] = nullptr;
		const size_t order[] = { 1,7,3,5,0,8,2,6,4 };
		xy upos = u->pos;
		//log("movement heat for %d %d is %d\n", c->u->pos.x, c->u->pos.y, c->u->movement_heat);
		if (c->u->movement_heat < 10 && !c->u->is_building) {
			for (size_t i = 0; i < 9; ++i) {
				size_t n = order[i];
				xy pos = upos + relpos[n];
				if ((size_t)pos.x >= width || (size_t)pos.y >= height) continue;
				int score = c->move_scores[n];
				if (n == 4 && dont_stay_still) score += inf_distance;
//				if (u->pos == xy(7, 8)) {
//					unit_controller* nc = move[pos.x + pos.y * 50];
//					log("move %d ? score %d nc %p\n", n, score, nc);
//					log("pos %d %d\n", pos.x, pos.y);
//					if (nc) {
//						log("nc is at %d %d\n", nc->u->pos.x, nc->u->pos.y);
//						log("nc is a %d, last seen %d\n", nc->u->type, nc->u->last_seen);
//						log("nc->move_index %d, nc_move_n %d\n", nc->move_index, nc->move_n);
//					}
//				}
				if (true) {
					unit_controller* nc = move[pos.x + pos.y * 50];
					if (nc) {
						auto& t = nc->u->p->get_tile(nc->u->pos);
						if (t.u != nc->u) error("mismatch for %d", nc->u->id);
					}
				}
				if (score < best_score) {
					size_t index = pos.x + pos.y * 50;
					unit_controller* nc = move[index];
					if (!nc || nc->move_index != -1) {
						auto& t = current_planet->get_tile(pos);
						if (t.walkable && t.visible && (!t.u || (t.u->is_mine && !t.u->is_building))) {
							best_score = score;
							best_index = index;
							best_move_n = n;
							best_nc = nc;
						}
					}
				}
			}
		}
		if (best_nc) {
			move[start_index] = c;
			c->move_index = -1;
			do_move(best_nc->move_index, true);
			c->move_index = start_index;
			do_move(i, dont_stay_still);
			return;
		}
		move[best_index] = c;
		c->move_n = best_move_n;
		c->move_index = -1;
		move_order[move_order_size++] = c;
	};

	for (size_t i = 0; i != vec_size; ++i) {
		do_move(i, false);
	}

	if (check_damage) {
		int damage_out = 0;
		int damage_in = 0;

		std::array<bool, 2500> overcharge_taken;

		std::array<xy, 2500> positions;
		for (size_t i = 0; i != vec_size; ++i) {
			unit_controller* c = vec[i];
			xy pos = c->u->pos + relpos[c->move_n];
			positions[i] = pos;
			overcharge_taken[i] = c->u->ability_heat >= 10;
		}
		for (size_t i = 0; i != vec_size; ++i) {
			unit_controller* c = vec[i];
			xy pos = positions[i];

			int damage = c->u->damage;
			int range = c->u->attack_range;
			if (damage > 0 && range) {
				bool in_range = false;
				for (unit* e : enemy_units) {
					if (e->gone) continue;
					if (lengthsq(pos - e->pos) <= range) {
						in_range = true;
						break;
					}
				}
				if (in_range) {
					damage_out += damage;
					if (healer_research_level >= 3 && false) {
						for (size_t i = 0; i != vec_size; ++i) {
							unit_controller* c2 = vec[i];
							if (c2->u->type == healer && !overcharge_taken[i] && lengthsq(pos - positions[i]) <= 30) {
								damage_out += damage;
								overcharge_taken[i] = true;
								break;
							}
						}
					}
				}
			}
		}

		for (unit* e : enemy_units) {
			int damage = e->damage;
			int range = e->attack_range;
			if (damage > 0 && range) {
				bool in_range = false;
				for (size_t i = 0; i < vec_size; ++i) {
					xy pos = positions[i];
					for_each_neighbor_pos_index(e->pos, [&](xy npos, size_t index) {
						if (lengthsq(npos - pos) <= range) {
							in_range = true;
							return false;
						}
						return true;
					});
					if (in_range) break;
				}
				if (in_range) {
					damage_in += damage;
				}
			}
		}

		log("damage in: %d out: %d\n", damage_in, damage_out);

		log("enemy_units.size() is %d, my_units.size() is %d\n", enemy_units.size(), my_units.size());
		int mult = enemy_units.size() * 384 / my_units.size();
		if (my_units.size() < enemy_units.size() * 3 || current_frame < 450) {
			if (units_killed + units_lost >= 4 && units_killed) {
				mult = units_lost * 256 / units_killed;
			} else mult = 256;
		}
		if (mult > 256) mult = 256;
		if (current_frame >= 600) {
			mult = mult * (720 - current_frame) / 120;
		}

		if (damage_out < damage_in * mult / 256) {

			for (size_t i = 0; i != vec_size; ++i) {
				unit_controller* c = vec[i];
				if (c->u->type == knight || c->u->type == worker) continue;
				for (size_t i = 0; i < 9; ++i) {
					auto& s = c->move_scores[i];
					if (s == inf_distance) continue;
					xy pos = c->u->pos + relpos[i];
					if ((size_t)pos.x >= width || (size_t)pos.y >= height) continue;
					size_t index = distgrid_index(pos);
					s += damage_grid[index];
					if (damage_grid[c->u->index] >= 60) {
						s -= default_attack_distgrid[index];
					}
				}
			}

			return move_units(false);
		}
	}

	for (size_t i = 0; i != move_order_size; ++i) {
		unit_controller* c = move_order[i];
		if (c->move_n != 4) {
//			if (c->u->pos == xy(7, 8)) {
				log("move robot of type %d at %d %d direction %d\n", c->u->type, c->u->pos.x, c->u->pos.y, c->move_n);

//				log("move scores: \n");
//				for (auto& v : c->move_scores) {
//					log(" %d\n", v);
//				}
//			}
			bc_GameController_move_robot(gc, c->u->id, direction_to_bc_Direction[c->move_n]);
			check_error("move");

			change_unit_position(c->u, c->u->pos + relpos[c->move_n]);
			c->u->movement_heat += c->u->movement_cooldown;
		}
	}


}

void unit_controls_update() {

}

