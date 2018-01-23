
std::array<int, 0x10000> sqrt_table;

distgrid_t wall_distgrid;

void action_init() {
	for (int i = 0; i != 0x10000; ++i) {
		sqrt_table[i] = isqrt((unsigned)i);
	}

	a_vector<size_t> wall_positions;
	auto add_wall_pos = [&](tile& t) {
		if (!t.walkable) return;
		wall_positions.push_back(distgrid_index(t.pos));
	};
	for (int i = 0; i != current_planet->width; ++i) {
		add_wall_pos(current_planet->get_tile(xy(i, 0)));
		add_wall_pos(current_planet->get_tile(xy(i, current_planet->height - 1)));
	}
	for (int i = 0; i != current_planet->width; ++i) {
		add_wall_pos(current_planet->get_tile(xy(0, i)));
		add_wall_pos(current_planet->get_tile(xy(current_planet->width - 1, i)));
	}
	for (auto& t : current_planet->tiles) {
		if (!t.walkable) {
			current_planet->for_each_neighbor_tile(t, [&](tile& nt) {
				add_wall_pos(nt);
				return true;
			});
		}
	}
	log("wall_positions.size() is %d\n", wall_positions.size());
	wall_distgrid.fill(inf_distance);
	generate_distance_grid(wall_positions, current_planet->movecost, wall_distgrid);
}

distgrid_t default_gather_distgrid;
distgrid_t default_attack_distgrid;
distgrid_t knight_attack_distgrid;
distgrid_t default_build_distgrid;
distgrid_t rocket_distgrid;
distgrid_t best_rocket_distgrid;
distgrid_t fow_distgrid;
distgrid_t near_factory_distgrid;
distgrid_t enemy_factory_distgrid;
distgrid_t any_unit_distgrid;

int karbonite = 0;

int workers = 0;
int factories = 0;
int usable_factories = 0;
int rockets = 0;

int rangers = 0;
int healers = 0;
int knights = 0;
int mages = 0;

int enemy_rangers = 0;
int enemy_mages = 0;
int enemy_knights = 0;
int enemy_healers = 0;
int enemy_rockets = 0;

int mages_made = 0;
int healers_made = 0;
int rangers_made = 0;
int knights_made = 0;

int rocket_age_mages_made = 0;
int rocket_age_rangers_made = 0;
int rocket_age_healers_made = 0;
int rocket_age_knights_made = 0;

int mage_research_level = 0;
int healer_research_level = 0;
int rocket_research_level = 0;
int knight_research_level = 0;

int blink_done_frame = std::numeric_limits<int>::max();
int overcharge_done_frame = std::numeric_limits<int>::max();

int next_combat_visit_n = 1;


void unit_attack(unit* u, unit* target, const char* debug_str, bool use_ability = false) {
	if (u->type == ranger) {
		bc_GameController_attack(gc, u->id, target->id);
		check_error("attack (ranger, %s)", debug_str);
		u->controller->last_action_frame = current_frame;
		u->attack_heat += u->attack_cooldown;
		int damage = std::min(u->damage - target->armor, target->health);
		total_damage_dealt += damage;
		//total_ranger_damage += damage;
		target->health -= damage;
		if (target->health <= 0) {
			++units_killed;
			target->health = 0;
			target->dead = true;
			update_groups(target);
			target->p->get_tile(target->pos).u = nullptr;
		}
	} else if (u->type == mage) {
		bc_GameController_attack(gc, u->id, target->id);
		check_error("attack (mage, %s)", debug_str);
		u->controller->last_action_frame = current_frame;
		u->attack_heat += u->attack_cooldown;
		auto f = [&](tile& t) {
			if (t.u) {
				int damage = std::min(u->damage - t.u->armor, t.u->health);
				total_mage_damage += damage;
				total_damage_dealt += damage;
				t.u->health -= damage;
				if (t.u->health <= 0) {
					if (t.u->is_mine) ++units_lost;
					else ++units_killed;
					t.u->health = 0;
					t.u->dead = true;
					if (t.u->is_mine) {
						find_and_erase_if_exists(my_units_of_type[t.u->type], t.u);
						find_and_erase_if_exists(my_completed_units_of_type[t.u->type], t.u);
					}
					update_groups(t.u);
					t.u = nullptr;
				}
			}
			return true;
		};
		target->p->for_each_neighbor_tile(target->pos, f);
		f(target->p->get_tile(target->pos));
	} else if (u->type == knight) {
		if (use_ability) {
			bc_GameController_javelin(gc, u->id, target->id);
			check_error("javelin (knight, %s)");
			u->controller->last_action_frame = current_frame;
			u->ability_heat += u->ability_cooldown;
			int damage = std::min(u->damage - target->armor, target->health);
			total_damage_dealt += damage;
			total_javelin_damage += damage;
			target->health -= damage;
			if (target->health <= 0) {
				++units_killed;
				target->health = 0;
				target->dead = true;
				update_groups(target);
				target->p->get_tile(target->pos).u = nullptr;
			}
		} else {
			bc_GameController_attack(gc, u->id, target->id);
			check_error("attack (knight, %s)", debug_str);
			u->controller->last_action_frame = current_frame;
			u->attack_heat += u->attack_cooldown;
			int damage = std::min(u->damage - target->armor, target->health);
			total_damage_dealt += damage;
			target->health -= damage;
			if (target->health <= 0) {
				++units_killed;
				target->health = 0;
				target->dead = true;
				update_groups(target);
				target->p->get_tile(target->pos).u = nullptr;
			}
		}
	}
}

void overchargeattack() {
	if (healer_research_level < 3) return;

	a_vector<std::pair<unit*, xy>> retreat_moves;

	while (true) {
		unit* best_unit = nullptr;
		int best_score = std::numeric_limits<int>::max();
		unit* best_target = nullptr;
		static_vector<xy, 4> best_path;
		a_vector<unit*> best_healers;
		bool best_is_ability = false;

		a_vector<unit*> healers;

		for (unit* u : my_units) {
			if (!u->is_on_map) continue;
			if (u->type != ranger && u->type != mage) continue;
			healers.clear();
			for (unit* h : my_units_of_type[healer]) {
				if (!h->is_on_map) continue;
				if (h->ability_heat < 10 && lengthsq(u->pos - h->pos) <= 48) {
					h->combat_visit_n = 0;
					healers.push_back(h);
				}
			}
			if (healers.size() < 2) continue;

			unit* target = get_best_score_copy(visible_enemy_units, [&](unit* e) {
				return lengthsq(u->pos - e->pos) * 110 + e->health;
			});
			if (!target) continue;
			int target_d = lengthsq(u->pos - target->pos);
			if (target_d > 98) continue;

			int moves = 0;
			bool has_javelin = u->type == knight && knight_research_level >= 3;
			int attack_heat = has_javelin ? u->ability_heat : u->attack_heat;
			int movement_heat = u->movement_heat;
			xy pos = u->pos;
			static_vector<xy, 4> path;
			int max_moves = 2;
			if (u->type == mage) max_moves = 4;
			if (u->type == knight) max_moves = has_javelin ? 4 : 6;
			int attack_range = has_javelin ? 10 : u->attack_range;
			for (int i = 0; i != max_moves; ++i) {
				if (target_d <= attack_range) break;
				if (movement_heat >= 10) {
					bool any_in_range = false;
					for (unit* h : healers) {
						if (h->combat_visit_n == 0 && lengthsq(h->pos - pos) <= 30) {
							any_in_range = true;
							h->combat_visit_n = 1;
							break;
						}
					}
					if (!any_in_range) break;
					++moves;
					attack_heat = 0;
					movement_heat = 0;
				} else {
					movement_heat += u->movement_cooldown;
				}
				int prev_d = target_d;
				u->p->for_each_neighbor_tile(pos, [&](tile& t) {
					if (!t.walkable || !t.visible || t.u) return true;
					int d = lengthsq(t.pos - target->pos);
					if (d < target_d) {
						target_d = d;
						pos = t.pos;
					}
					return true;
				});
				if (target_d >= prev_d) break;
				path.push_back(pos);
			}
			if (target_d > attack_range) continue;
			if (u->type == ranger && target_d <= 10) continue;
			int healers_in_range = 0;
			for (unit* h : healers) {
				if (h->combat_visit_n == 0 && lengthsq(h->pos - pos) <= 30) {
					++healers_in_range;
				}
			}
			int n_attacks = (attack_heat < 10 ? 1 : 0) + healers_in_range - moves;
			log("n_attacks is %d\n", n_attacks);
			log("healers.size() is %d, healers_in_range %d\n", healers.size(), healers_in_range);
			int damage = u->damage - target->armor;
			if (target->health > n_attacks * damage) continue;
			n_attacks = (target->health + damage - 1) / damage;
			log("moves %d, n_attacks %d\n", moves, n_attacks);
			int s = moves + n_attacks;

			if (u->type == mage) {
				int kills = 0;
				auto f = [&](tile& t) {
					if (t.u) {
						if (u->damage - t.u->armor >= t.u->health) {
							++kills;
						}
					}
					return true;
				};
				target->p->for_each_neighbor_tile(target->pos, f);
				f(target->p->get_tile(target->pos));

				if (kills > 1) {
					s -= (kills - 1) * 2;
				}
				if (moves > kills) continue;
			}

			if (s < best_score) {
				log("score %d\n", s);
				best_score = s;
				best_unit = u;
				best_target = target;
				best_path = path;
				best_healers = healers;
				best_is_ability = has_javelin;
			}
		}

		if (best_target) {
			log("overcharge snipe with %d moves and %d score\n", best_path.size(), best_score);

			auto overcharge = [&]() {
				unit* target = best_unit;
				unit* u = nullptr;
				for (unit* h : best_healers) {
					if (h->ability_heat < 10 && lengthsq(h->pos - target->pos) <= 30) {
						u = h;
						break;
					}
				}
				//if (!u) error("failed to find healer :(");
				if (!u) return false;
				bc_GameController_overcharge(gc, u->id, target->id);
				check_error("overcharge");
				u->ability_heat += u->ability_cooldown;
				target->movement_heat = 0;
				target->attack_heat = 0;
				target->ability_heat = 0;
				++overcharges_used;
				log("overcharged!\n");
				return true;
			};

			unit* u = best_unit;
			unit* target = best_target;
			xy src_pos = u->pos;
			bool failed = false;
			for (xy pos : best_path) {
				if (u->movement_heat >= 10) {
					if (!overcharge()) {
						failed = true;
						break;
					}
				}
				bc_GameController_move_robot(gc, u->id, bc_Direction_from_relpos(pos - u->pos));
				check_error("move (overcharge)");
				change_unit_position(u, pos);
				u->movement_heat += u->movement_cooldown;
				log("moved to %d %d\n", pos.x, pos.y);
			}
			while (!failed && target->health > 0) {
				log("attack! target health is %d\n", target->health);
				if (u->attack_heat >= 10) {
					if (!overcharge()) break;
				}
				unit_attack(u, target, "overcharge", best_is_ability);
			}
			log("done yey\n");

			if (best_is_ability) ++overcharge_javelins;

			if (u->movement_heat < 10 && u->pos != src_pos) {
				int best_d = lengthsq(u->pos - src_pos);
				xy move_pos = u->pos;
				u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
					if (!t.walkable || !t.visible || t.u) return true;
					int d = lengthsq(t.pos - src_pos);
					if (d < best_d) {
						best_d = d;
						move_pos = t.pos;
					}
					return true;
				});
				if (move_pos != u->pos) {
					retreat_moves.push_back({u, move_pos});
				}
			}

		} else break;
	}

//	for (auto& v : retreat_moves) {
//		unit* u = std::get<0>(v);
//		xy pos = std::get<1>(v);
//		if (u->movement_heat < 10 && lengthsq(u->pos - pos) <= 2) {
//			bc_GameController_move_robot(gc, u->id, bc_Direction_from_relpos(pos - u->pos));
//			check_error("move (post overcharge)");
//			change_unit_position(u, pos);
//			u->movement_heat += u->movement_cooldown;
//		}
//	}

}

void attackstuff() {

	a_vector<std::tuple<int, unit*, size_t>> allies_in_range;
	a_vector<std::pair<unit*, size_t>> best_allies;
	a_vector<size_t> indices_used;

	size_t width = current_planet->width;
	size_t height = current_planet->height;

	std::function<void()> attack = [&]() {
		unit* best_target = nullptr;
		int best_allies_score = 0;

		std::array<bool, 2500> spot_taken{};

		for (unit* u : visible_units) {
			spot_taken[u->index] = true;
		}

		for (unit* e : visible_enemy_units) {
			if (e->type != ranger) continue;
			int visit_n = next_combat_visit_n++;
			allies_in_range.clear();
			auto add = [&](unit* a) {
				if (a->type != ranger) return;
				if (a->attack_heat >= 10) return;
				int r = lengthsq(a->pos - e->pos);
				if (r <= a->attack_range && r > 10) {
					int d = damage_grid[a->index];
					if (d < a->health) {
						allies_in_range.push_back(std::make_tuple(d, a, a->index));
					}
				};
				if (a->movement_heat >= 10) return;
				e->p->for_each_neighbor_tile(a->pos, [&](tile& t) {
					size_t index = distgrid_index(t.pos);
					if (t.walkable && t.visible) {
						int r = lengthsq(t.pos - e->pos);
						if (r <= a->attack_range && r > 10) {
							int d = damage_grid[index];
							if (d < a->health) {
								allies_in_range.push_back(std::make_tuple(d, a, index));
							}
						}
					}
					return true;
				});
			};
			auto visit = [&](xy src_pos, int sqrange) {
				int range = sqrt_table[sqrange] + 1;
				for (int y = -range; y <= range; ++y) {
					for (int x = -range; x <= range; ++x) {
						xy pos = src_pos + xy(x, y);
						if ((size_t)pos.x >= width || (size_t)pos.y >= height) continue;
						xy npos(x, y);
						if (lengthsq(npos) > sqrange) continue;
						e->p->for_each_neighbor_tile(pos, [&](tile& t) {
							if (t.u && t.u->is_mine && t.u->damage > 0) {
								unit* a = t.u;
								if (a->combat_visit_n != visit_n) {
									a->combat_visit_n = visit_n;
									add(a);
								}
							}
							return true;
						});
					}
				}
			};
			visit(e->pos, e->attack_range);
			e->p->for_each_neighbor_tile(e->pos, [&](tile& t) {
				if (t.walkable && (!t.u || !t.u->is_building)) {
					visit(t.pos, e->attack_range);
				}
				return true;
			});
			std::sort(allies_in_range.begin(), allies_in_range.end());
			visit_n = next_combat_visit_n++;
			int damage_in = 0;
			int damage_out = 0;
			int best_n = 0;
			int best_score = 0;
			indices_used.clear();
			for (size_t i = 0; i != allies_in_range.size(); ++i) {
				int d = std::get<0>(allies_in_range[i]);
				unit* u = std::get<1>(allies_in_range[i]);
				size_t index = std::get<2>(allies_in_range[i]);
				if (u->combat_visit_n == visit_n) continue;
				if (spot_taken[index]) continue;
				indices_used.push_back(i);
				spot_taken[u->index] = false;
				spot_taken[index] = true;
				u->combat_visit_n = visit_n;
				if (d > damage_in) damage_in = d;
				damage_out += u->damage;
				int s = damage_in - damage_out;
				if (s < best_score) {
					best_score = s;
					best_n = indices_used.size();
				}
			}

			for (size_t i : reverse(indices_used)) {
				spot_taken[std::get<2>(allies_in_range[i])] = false;
				spot_taken[std::get<1>(allies_in_range[i])->index] = true;
			}

			if (best_n != 0) {
				if (best_score < best_allies_score) {
					best_allies_score = best_score;
					best_target = e;
					best_allies.clear();
					for (size_t i = 0; i != best_n; ++i) {
						auto& v = allies_in_range[indices_used[i]];
						best_allies.push_back({std::get<1>(v), std::get<2>(v)});
					}
				}
			}
		}
		if (best_target && best_allies_score <= 0) {
			log("best_allies_score is %d with %d allies\n", best_allies_score, best_allies.size());

			for (auto& v : best_allies) {
				unit* u = v.first;
				size_t index = v.second;
				log("move %d from %d %d to %d %d\n", u->id, u->pos.x, u->pos.y, index % 50, index / 50);
				if (u->index != index) {
					spot_taken[u->index] = false;
					spot_taken[index] = true;
					xy pos(index % 50, index / 50);
					if (current_planet->get_tile(pos).u) error("tile already taken :(");
					bc_GameController_move_robot(gc, u->id, bc_Direction_from_relpos(pos - u->pos));
					check_error("move (combat attack)");

					change_unit_position(u, pos);
					u->movement_heat += u->movement_cooldown;
				} else {
					u->movement_heat += u->movement_cooldown;
				}
				if (best_target->health > 0) {
					if (lengthsq(u->pos - best_target->pos) <= u->attack_range) {
						unit_attack(u, best_target, "combat attack");
					}
				}
			}

			log("next attack!\n");
			return attack();
		}
	};
	attack();
}

bool replicate_to_the_far_reaches_of_the_world(bool is_mars_spam) {

	struct open_node {
		size_t index;
		int cost;
		int moves;
	};

	struct open_node_cmp {
		bool operator()(open_node& a, open_node& b) {
			return a.cost > b.cost;
		}
	};

	std::priority_queue<open_node, a_vector<open_node>, open_node_cmp> open;
	distgrid_t costs;
	costs.fill(inf_distance);
	distgrid_t movecost;
	movecost.fill(inf_distance);

	for (tile& t : current_planet->tiles) {
		if (t.walkable) {
			int karbonite = 0;
			current_planet->for_each_neighbor_tile(t.pos, [&](tile& n) {
				karbonite += n.karbonite;
				return true;
			});
			size_t index = distgrid_index(t.pos);
			movecost[index] = 31 - std::min(t.karbonite / 3, 30);
			if (karbonite) {
				if (get_replicate_score(t.pos, is_mars_spam) != std::numeric_limits<int>::max()) {
					int s = any_unit_distgrid[index] * 30;
					if (s < inf_distance) {
						open.push({index, -s, 0});
					}
				}
			}
		}
	}

	size_t last_x = current_planet->width - 1;
	size_t last_y = current_planet->height - 1;

	while (!open.empty()) {
		auto n = open.top();
		open.pop();

		if (n.moves >= 50) break;
		//log("n.moves for %d %d is %d\n", n.index % 50, n.index / 50, n.moves);

		auto add = [&](size_t index) {
			int m = movecost[index];
			if (m == inf_distance) return false;
			int next_cost = n.cost + m;
			if (costs[index] <= next_cost) return false;
			//if (next_cost >= (n.moves + 1) * 10) return false;
			if (next_cost > 0) return false;
			costs[index] = next_cost;
			//log("visit %d %d with cost %d\n", index % 50, index / 50, next_cost);

			tile& nt = *tile_map[index];
			if (nt.u && nt.u->is_mine && nt.u->type == worker) {
				//error("found worker");
				tile&t = *tile_map[n.index];
				if (t.walkable && t.visible && !t.u) {
					unit* u = nt.u;
					if (default_build_distgrid[u->index] > 3) {
						if (u->movement_heat < 10) {
							bc_GameController_move_robot(gc, u->id, bc_Direction_from_relpos(t.pos - u->pos));
							check_error("move (world)");
							change_unit_position(u, t.pos);
							u->movement_heat += u->movement_cooldown;
							return true;
						}
						if (u->ability_heat < 10 && karbonite >= 30) {
							bc_GameController_replicate(gc, u->id, bc_Direction_from_relpos(t.pos - u->pos));
							check_error("replicate (world)");
							u->ability_heat += u->ability_cooldown;
							u->controller->last_replicate = current_frame;
							karbonite -= 30;
							++workers;
							bc_Unit* src = bc_GameController_sense_unit_at_location(gc, t.loc);
							if (src) {
								full_update_unit(src);
								delete_bc_Unit(src);
							}
							return true;
						}
					}
				}
			}

			open.push({index, next_cost, n.moves + 1});
			return false;
		};
		int x = n.index % 50;
		int y = n.index / 50;
		if (x != last_x) {
			if (add(n.index + 1)) return true;
			if (y != last_y && add(n.index + 1 + 50)) return true;
			if (y != 0 && add(n.index + 1 - 50)) return true;
		}
		if (y != last_y) {
			if (add(n.index + 50)) return true;
			if (x != 0 && add(n.index - 1 + 50)) return true;
		}
		if (x != 0) {
			if (add(n.index - 1)) return true;
			if (y != 0 && add(n.index - 1 - 50)) return true;
		}
		if (y != 0 && add(n.index - 50)) return true;

	}

	return false;
}

int get_replicate_score(xy src_pos, bool is_mars_spam) {
	if (damage_grid[distgrid_index(src_pos)]) return std::numeric_limits<int>::max();
	int k = 0;
	int w = 0;
	int sqrange = 80;
	int range = sqrt_table[sqrange] + 1;
	size_t width = current_planet->width;
	size_t height = current_planet->height;
	int s = 0;
	int reachability_index = current_planet->get_tile(src_pos).reachability_index;
	for (int y = -range; y <= range; ++y) {
		for (int x = -range; x <= range; ++x) {
			xy pos = src_pos + xy(x, y);
			if ((size_t)pos.x >= width || (size_t)pos.y >= height) continue;
			int d = lengthsq(xy(x, y));
			if (d > sqrange) continue;
			tile& t = current_planet->get_tile(pos);
			if (t.reachability_index != reachability_index) continue;
			k += std::min(t.karbonite, 200);
			if (t.u && t.u->is_mine && t.u->type == worker) {
				++w;
				s += (int)(std::sqrt(1.0 / d) * 1000);
			}
			if (t.karbonite) {
				s -= (int)(std::sqrt(d) * 1000) / t.karbonite;
			}
		}
	}
	return (is_mars_spam ? s : k >= w * 45 ? s : std::numeric_limits<int>::max());
}

bool go_for_knights = false;

int workers_sent = 0;
int last_launch = 0;

void actions() {

	if (current_planet == earth) {
		for (unit* u : my_units) {
			if (u->is_building || !u->is_on_map) continue;
			if (u->movement_heat >= 10) continue;
			if (u->type == worker && workers_sent >= 6) continue;
			int r = rocket_distgrid[u->index];
			if (r != inf_distance && (r <= 1 || u->controller->go_to_rocket_frame == current_frame)) {
				int best_r = best_rocket_distgrid[u->index];
				if ((r <= 3 || current_frame + r * 3 >= 700) && (best_r > 3 || best_r <= 1)) {
					unit* load_into = nullptr;
					u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
						if (t.u && t.u->is_mine && t.u->type == rocket && t.u->is_completed && t.u->loaded_units.size() < t.u->max_capacity) {
							if (u->type == worker) {
								for (unit* n : t.u->loaded_units) {
									if (n->type == worker) {
										return true;
									}
								}
							}
							load_into = t.u;
							return false;
						}
						return true;
					});
					if (load_into) {
						log("load %d into %d\n", u->id, load_into->id);
						bc_GameController_load(gc, load_into->id, u->id);
						check_error("rocket load");
						load_into->loaded_units.push_back(u);
						u->p->get_tile(u->pos).u = nullptr;
						u->is_on_map = false;
						u->loaded_into = load_into;
						if (u->type == worker) ++workers_sent;
						return actions();
					}
				}
			}
		}
	}

	overchargeattack();
	attackstuff();

	int total_karbonite = 0;
	for (auto& t : current_planet->tiles) {
		total_karbonite += std::min(t.karbonite, 120);
	}
	
	bool save_for_rocket = false;
	if (rocket_research_level && rockets < (rangers + healers + mages + 7) / 8 && current_planet == earth) {
		int visible_tiles = 0;
		for (tile& t : current_planet->tiles) {
			if (t.visible) ++visible_tiles;
		}
		int map_control = visible_tiles * 100 / (int)current_planet->tiles.size();
		if (map_control > 66 || map_control < 20 || current_frame >= 550 || rangers + healers + mages + knights >= 100) {
			int best_rocket_score = std::numeric_limits<int>::min();
			tile* best_rocket_tile = nullptr;
			unit* best_rocket_unit = nullptr;
			for (unit* u : my_workers) {
				if (!u->is_on_map) continue;
				if (u->controller->last_action_frame == current_frame) continue;
				u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
					if (t.pos == u->pos) return true;
					if (!t.walkable) return true;
					if (t.u && (!t.u->is_mine || t.u->is_building)) return true;
					size_t index = distgrid_index(t.pos);
					int a = wall_distgrid[index];
					int b = default_attack_distgrid[index];
					int score = (int)(1000 * (std::sqrt(a) + std::sqrt(b)));
					if (t.u) score -= 10000;
					for_each_range_index(u->pos, u->index, 3, [&](size_t index) {
						int x = index % 50;
						int y = index / 50;
						tile& t = u->p->get_tile(xy(x, y));
						if (t.u && t.u->is_mine && t.u->type == worker && u->pos != t.u->pos) {
							score += 100 / lengthsq(u->pos - t.u->pos);
						}
						if (t.u && t.u->is_building) score -= 1000;
						return true;
					});
					if (u->ability_heat < 10) score += 100;
					score += std::max(200 - damage_grid[index], 0);
					if (score > best_rocket_score) {
						best_rocket_score = score;
						best_rocket_tile = &t;
						best_rocket_unit = u;
					}
					return true;
				});
			}
			if (best_rocket_tile) {
				unit* u = best_rocket_unit;
				tile& t = *best_rocket_tile;
				if (t.u) {
					if (karbonite >= 100) {
						bc_GameController_disintegrate_unit(gc, t.u->id);
						check_error("disintegrate");
						unit* u = t.u;
						t.u = nullptr;
						u->dead = true;
						update_groups(u);
						return actions();
					} else save_for_rocket = true;
				}
				if (workers < (rangers + healers + mages >= 30 ? 8 : 4) && karbonite >= 30 && u->ability_heat < 10) {
					bc_GameController_replicate(gc, u->id, bc_Direction_from_relpos(t.pos - u->pos));
					check_error("replicate (rocket)");
					u->ability_heat += u->ability_cooldown;
					u->controller->last_replicate = current_frame;
					karbonite -= 30;
					++workers;
					bc_Unit* src = bc_GameController_sense_unit_at_location(gc, t.loc);
					if (src) {
						full_update_unit(src);
						delete_bc_Unit(src);
					}
					return actions();
				} else {
					if (karbonite < 75) save_for_rocket = true;
					else {
						if (!t.u) {
							bc_GameController_blueprint(gc, u->id, Rocket, bc_Direction_from_relpos(t.pos - u->pos));
							check_error("blueprint rocket");
							u->controller->last_action_frame = current_frame;
							u->controller->nomove_frame = current_frame;
							karbonite -= 75;
							++rockets;
							bc_Unit* src = bc_GameController_sense_unit_at_location(gc, t.loc);
							if (src) {
								full_update_unit(src);
								delete_bc_Unit(src);
							}
							return actions();
						}
					}
				}
			}
		}
	}

	int best_factory_score = std::numeric_limits<int>::min();
	tile* best_factory_tile = nullptr;
	unit* best_factory_unit = nullptr;

	for (unit* u : my_workers) {
		if (!u->is_on_map) continue;
		if (u->controller->last_action_frame == current_frame) continue;
		int n = 3 + (karbonite - 500) / 400;
		auto lost_karbonite = [&]() {
			int k = karbonite;
			int r = 0;
			while (k > 120) {
				r += std::min(k / 40, 10);
				int d = std::max(10 - (k / 40), 0) - 20 * usable_factories / 5;
				if (d > 0) return std::numeric_limits<int>::max();
				k += d;
			}
			return r;
		};
		if ((usable_factories < n || lost_karbonite() >= 120) && current_planet == earth && karbonite >= 100) {
			u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
				if (t.pos == u->pos) return true;
				if (!t.walkable || t.u) return true;
				int n = 0;
				u->p->for_each_neighbor_tile(t.pos, [&](tile& t) {
					if (t.walkable && !t.u) ++n;
					return true;
				});
				if (!n) return true;
				size_t index = distgrid_index(t.pos);
				int a = std::min((int)wall_distgrid[index], 2);
				int b = default_attack_distgrid[index];
				if (go_for_knights) b = 100 - b;
				int score = (int)(1000 * (std::sqrt(a) + std::sqrt(b)));
				for_each_range_index(u->pos, u->index, 3, [&](size_t index) {
					int x = index % 50;
					int y = index / 50;
					tile& t = u->p->get_tile(xy(x, y));
					if (t.u && t.u->is_mine && t.u->type == worker && u->pos != t.u->pos) {
						score += 100 / lengthsq(u->pos - t.u->pos);
					}
					if (t.u && t.u->is_building) score -= 1000;
					return true;
				});
				if (u->ability_heat < 10) score += 100;
				score += std::max(200 - damage_grid[index], 0);
				if (score > best_factory_score) {
					best_factory_score = score;
					best_factory_tile = &t;
					best_factory_unit = u;
				}
				return true;
			});
		}
	}

	bool want_factories = ((go_for_knights ? workers >= 8 || current_frame >= 6 : workers >= 20 || current_frame >= 20) || (current_frame >= 1 && karbonite > 100));
	bool save_for_factory = factories == 0 && want_factories && current_planet == earth && karbonite < 250;

	if (best_factory_tile && want_factories) {
		unit* u = best_factory_unit;
		tile& t = *best_factory_tile;
		bc_GameController_blueprint(gc, u->id, Factory, bc_Direction_from_relpos(t.pos - u->pos));
		check_error("blueprint factory");
		u->controller->last_action_frame = current_frame;
		u->controller->nomove_frame = current_frame;
		karbonite -= 100;
		++factories;
		++usable_factories;
		bc_Unit* src = bc_GameController_sense_unit_at_location(gc, t.loc);
		if (src) {
			full_update_unit(src);
			delete_bc_Unit(src);
		}
		return actions();
	}

	for (int i = 0; i != 2; ++i) {
		for (unit* u : my_workers) {
			if (!u->is_on_map) continue;

			tile* harvest_tile = nullptr;
			int harvest_score = 0;
			u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
				if (t.pos == u->pos) return true;
				if (t.u && t.u->is_mine && t.u->is_building && !t.u->is_completed) {

					return false;
				}
				if (t.karbonite) {
					int score = std::min(t.karbonite, 3);
					if (score > harvest_score) {
						harvest_tile = &t;
						harvest_score = score;
					}
				}
				return true;
			});

			unit* build_unit = nullptr;
			int build_score = 0;
			unit* repair_unit = nullptr;
			int repair_score = 0;
			u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
				if (t.pos == u->pos) return true;
				if (t.u && t.u->is_mine && t.u->is_building) {
					if (!t.u->is_completed) {
						int score = t.u->health;
						if (score > build_score) {
							build_score = score;
							build_unit = t.u;
						}
					} else {
						int score = t.u->health;
						if (score > repair_score) {
							repair_score = score;
							repair_unit = t.u;
						}
					}
				}
				return true;
			});
			if (build_unit && (!harvest_tile || current_frame >= 6)) {
				bool need_more_builders = workers < (go_for_knights ? 7 : 3);
				if (!need_more_builders) {
					int builders = 0;
					for (unit* u : my_workers) {
						if (!u->is_on_map) continue;
						if (default_build_distgrid[u->index] <= 3) ++builders;
					}
					if (builders < 9) {
						int incomplete_factories = my_completed_units_of_type[factory].size() - my_units_of_type[factory].size();
						if (builders < incomplete_factories * 3) need_more_builders = true;
						if (karbonite >= 300) need_more_builders = true;
					}
				}
				if (need_more_builders && u->ability_heat < 10 && factories && karbonite >= 30) {
					tile* best_tile = nullptr;
					int best_score = 0;
					u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
						if (t.pos == u->pos) return true;
						if (t.walkable && !t.u) {
							int s = isqrt((unsigned)default_attack_distgrid[distgrid_index(t.pos)] * 4) + isqrt((unsigned)lengthsq(t.pos - build_unit->pos) * 4);
							if (s > best_score) {
								best_score = s;
								best_tile = &t;
							}
						}
						return true;
					});
					if (best_tile) {
						auto& t = *best_tile;
						bc_GameController_replicate(gc, u->id, bc_Direction_from_relpos(t.pos - u->pos));
						check_error("replicate (build)");
						u->ability_heat += u->ability_cooldown;
						u->controller->last_replicate = current_frame;
						karbonite -= 30;
						++workers;
						bc_Unit* src = bc_GameController_sense_unit_at_location(gc, t.loc);
						if (src) {
							full_update_unit(src);
							delete_bc_Unit(src);
						}
						return actions();
					}
				}

				if (u->controller->last_action_frame == current_frame) continue;
				if (u->controller->last_replicate == current_frame) continue;

				bc_GameController_build(gc, u->id, build_unit->id);
				check_error("build");
				u->controller->last_action_frame = current_frame;
				bc_Unit* src = bc_GameController_sense_unit_at_location(gc, build_unit->p->get_tile(build_unit->pos).loc);
				if (src) {
					unit* b = full_update_unit(src);
					delete_bc_Unit(src);

					if (b && b->health < b->max_health) {
						if (u != get_early_harvest_worker()) {
							u->controller->nomove_frame = current_frame;

							if (u->movement_heat < 10) {
								bool has_neighbor = false;
								tile* best_tile = nullptr;
								u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
									if (t.u && t.u->is_mine && !t.u->is_building) {
										has_neighbor = true;
									}
									if (t.walkable && t.visible && !t.u && lengthsq(t.pos - build_unit->pos) <= 2) {
										best_tile = &t;
									}
									return true;
								});
								if (has_neighbor && best_tile) {
									bc_GameController_move_robot(gc, u->id, bc_Direction_from_relpos(best_tile->pos - u->pos));
									check_error("move (build)");
									change_unit_position(u, best_tile->pos);
									u->movement_heat += u->movement_cooldown;
								}
							}
						}
					}
				}
				return actions();
			}

			if (repair_unit) {
				if (u->controller->last_action_frame == current_frame) continue;
				if (u->controller->last_replicate == current_frame) continue;

				bc_GameController_repair(gc, u->id, repair_unit->id);
				check_error("repair");
				u->controller->last_action_frame = current_frame;
				bc_Unit* src = bc_GameController_sense_unit_at_location(gc, repair_unit->p->get_tile(repair_unit->pos).loc);
				if (src) {
					full_update_unit(src);
					delete_bc_Unit(src);
				}
				return actions();
			}

			if (u->controller->last_action_frame == current_frame) continue;


			if (harvest_tile) {
				auto& t = *harvest_tile;
				//log("harvest from %d %d to %d %d yey!\n", u->pos.x, u->pos.y, t.pos.x, t.pos.y);
				//log("t.karbonite is %d\n", t.karbonite);
				bc_GameController_harvest(gc, u->id, bc_Direction_from_relpos(t.pos - u->pos));
				check_error("harvest");
				u->controller->last_action_frame = current_frame;
				t.karbonite -= 3;
				if (t.karbonite <= 0) t.karbonite = 0;
				u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
					if (t.karbonite >= 3) u->controller->harvest_nomove_frame = current_frame;
					if (u->movement_heat < 10) {
						if (t.u && t.u->is_mine && t.u->type == worker) {
							xy r = t.pos - u->pos;
							xy n = u->pos - r;
							if ((size_t)n.x < (size_t)u->p->width && (size_t)n.y < (size_t)u->p->height) {
								auto& nt = u->p->get_tile(n);
								if (nt.walkable && nt.visible && !nt.u) {
									bool has_karbonite = false;
									u->p->for_each_neighbor_tile(nt.pos, [&](tile& nnt) {
										if (nnt.karbonite >= 3) {
											has_karbonite = true;
											return false;
										}
										return true;
									});
									if (has_karbonite) {
										bc_GameController_move_robot(gc, u->id, bc_Direction_from_relpos(n - u->pos));
										check_error("move (harvest)");
										change_unit_position(u, n);
										u->movement_heat += u->movement_cooldown;
									}
								}
							}
						}
					}
					return true;
				});
			}
		}
	}


	bool mars_spam = current_planet == mars && (current_frame >= 700 || karbonite > 100);

	if ((current_planet == earth || current_frame < 680 || mars_spam) && !save_for_factory) {
		if (factories >= 3 || workers < 30 || mars_spam) {
			if (replicate_to_the_far_reaches_of_the_world(mars_spam)) return actions();
		}
		int best_replicate_score = std::numeric_limits<int>::max();
		tile* best_replicate_tile = nullptr;
		unit* best_replicate_unit = nullptr;
		for (unit* u : my_workers) {
			if (!u->is_on_map) continue;
			//if (u->controller->last_action_frame == current_frame) continue;

			//if (total_karbonite >= workers * 42 && karbonite >= 30 && u->ability_heat < 10 && factories) {
			if (karbonite >= 30 && u->ability_heat < 10 && (factories >= 3 || workers < 30 || mars_spam)) {
				int ud = default_gather_distgrid[u->index];
				u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
					if (t.pos == u->pos) return true;
					if (!t.walkable || t.u || !t.visible) return true;
					size_t index = distgrid_index(t.pos);
					int d = default_gather_distgrid[index];
					if (d && d >= ud && !mars_spam) return true;
					//int score = (d + 1) * 1000 - wall_distgrid[index];
					int score = get_replicate_score(t.pos, mars_spam);
					log("replicate score: %d\n", score);
					if (score < best_replicate_score) {
						best_replicate_score = score;
						best_replicate_tile = &t;
						best_replicate_unit = u;
					}
					return true;
				});
			}
		}
		if (best_replicate_tile) {
			unit* u = best_replicate_unit;
			tile& t = *best_replicate_tile;
			bc_GameController_replicate(gc, u->id, bc_Direction_from_relpos(t.pos - u->pos));
			check_error("replicate");
			u->ability_heat += u->ability_cooldown;
			u->controller->last_replicate = current_frame;
			karbonite -= 30;
			++workers;
			bc_Unit* src = bc_GameController_sense_unit_at_location(gc, t.loc);
			if (src) {
				full_update_unit(src);
				delete_bc_Unit(src);
			}
			return actions();
		}
	}

	auto unload = [&](unit* u) {
		if (!u->loaded_units.empty()) {
			if (u->loaded_units.front()->movement_heat >= 10) return false;
			tile* unload_tile = nullptr;
			int best_score = std::numeric_limits<int>::max();
			int type = u->loaded_units.front()->type;
			u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
				if (t.pos == u->pos) return true;
				if (!t.walkable || t.u) return true;
				int s = default_attack_distgrid[distgrid_index(t.pos)];
				if (type != knight && s <= 3) s = 3 - s;
				if (s < best_score) {
					best_score = s;
					unload_tile = &t;
				}
				return true;
			});
			if (unload_tile) {
				auto& t = *unload_tile;
				bc_GameController_unload(gc, u->id, bc_Direction_from_relpos(t.pos - u->pos));
				check_error("unload");
				u->loaded_units.front()->loaded_into = nullptr;
				u->loaded_units.erase(u->loaded_units.begin());
				bc_Unit* src = bc_GameController_sense_unit_at_location(gc, t.loc);
				if (src) {
					full_update_unit(src);
					delete_bc_Unit(src);
				}
				return true;
			}
		}
		return false;
	};

	for (unit* u : my_completed_units_of_type[rocket]) {
		if (!u->is_on_map) continue;
		if (current_planet == mars) {
			if (unload(u)) return actions();
		} else {
			if (u->loaded_units.size() >= u->max_capacity || (u->loaded_units.size() >= 2 && damage_grid[u->index] > u->health)) {
				++u->controller->launch_counter;
				bool launch = current_frame >= 700 || damage_grid[u->index];
				for (unit* u : my_completed_units_of_type[rocket]) {
					if (!u->is_on_map) continue;
					if (u->controller->launch_counter >= 10 || current_frame - last_launch <= 10) {
						launch = true;
						break;
					}
				}
				if (launch) {
					a_vector<tile*> tiles;
					for (auto& t : mars->tiles) {
						if (t.walkable) {
							tiles.push_back(&t);
						}
					}
					if (!tiles.empty()) {
						rand_state = rand_state * 22695477 + 1;
						size_t index = (rand_state >> 16) % tiles.size();

						bc_GameController_launch_rocket(gc, u->id, tiles[index]->loc);
						check_error("launch rocket");

						u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
							if (t.u) {
								unit* target = t.u;
								int damage = 100;
								target->health -= damage;
								if (target->health <= 0) {
									target->health = 0;
									target->dead = true;
									update_groups(target);
									target->p->get_tile(target->pos).u = nullptr;
								}
							}
							return true;
						});

						u->is_on_map = false;
						u->p->get_tile(u->pos).u = nullptr;
						last_launch = current_frame;

						log("LAUNCH %d!\n", u->id);
					}
				}
			}
		}
	}

	a_vector<unit*> facts = my_completed_units_of_type[factory];
	std::sort(facts.begin(), facts.end(), [&](unit* a, unit* b) {
		return default_attack_distgrid[a->index] < default_attack_distgrid[b->index];
	});

	for (unit* u : facts) {

		if (unload(u)) return actions();

		if (u->controller->last_action_frame == current_frame) continue;

		if (workers < (rocket_research_level ? 4 : 1)) {
			if (karbonite >= 25 && !u->factory_busy && u->loaded_units.size() < u->max_capacity) {
				bc_GameController_produce_robot(gc, u->id, Worker);
				check_error("produce");
				u->controller->last_action_frame = current_frame;
				karbonite -= 25;
				++workers;
				continue;
			}
			continue;
		}

		if (workers && !save_for_rocket && karbonite >= 20 && !u->factory_busy && u->loaded_units.size() < u->max_capacity) {
			if (go_for_knights && knights_made < 16) {
				if (mages_made < knights_made - 4) {
					++mages_made;
					++mages;
					bc_GameController_produce_robot(gc, u->id, Mage);
				} else {
					++knights;
					++knights_made;
					bc_GameController_produce_robot(gc, u->id, Knight);
				}
			} else if (rocket_research_level) {
				if (rocket_age_mages_made < rocket_age_healers_made / 2) {
					++rocket_age_mages_made;
					++mages;
					bc_GameController_produce_robot(gc, u->id, Mage);
				//} else if (rocket_age_knights_made < std::min(rocket_age_healers_made / 3, 6)) {
				} else if (rocket_age_knights_made < rocket_age_healers_made) {
					++rocket_age_knights_made;
					++knights;
					bc_GameController_produce_robot(gc, u->id, Knight);
				} else if (rocket_age_rangers_made < rocket_age_healers_made) {
					++rocket_age_rangers_made;
					++rangers;
					bc_GameController_produce_robot(gc, u->id, Ranger);
				} else {
					++rocket_age_healers_made;
					++healers;
					bc_GameController_produce_robot(gc, u->id, Healer);
				}
			} else if (knights_made < 1 || knights < (knight_research_level >= 2 ? rangers : 0)) {
				++knights;
				++knights_made;
				bc_GameController_produce_robot(gc, u->id, Knight);
			} else if (mages_made < enemy_knights || mages < (overcharge_done_frame - current_frame <= 60 && healers >= 16 ? std::min(rangers * 1 / 3, 8) : 0) + (current_frame >= blink_done_frame ? rangers / 3 + std::min(30, rangers) : current_frame + 40 >= blink_done_frame ? rangers / 5 : 0)) {
				++mages_made;
				++mages;
				bc_GameController_produce_robot(gc, u->id, Mage);
			} else if (healers < (overcharge_done_frame - current_frame <= 60 && rangers >= 16 ? (rangers + mages) * 4 / 3 : 0) + rangers * (rangers >= 30 ? 3 : 1) / 5 + std::max(-40 + (rangers + mages / 3) * 1 / 3, 0) && healers_made < (rangers_made + mages_made) * 5 / 4) {
				++healers_made;
				++healers;
				bc_GameController_produce_robot(gc, u->id, Healer);
			} else {
				++rangers_made;
				++rangers;
				bc_GameController_produce_robot(gc, u->id, Ranger);
			}
			check_error("produce");
			u->controller->last_action_frame = current_frame;
			karbonite -= 20;
			continue;
		}

	}

	a_vector<std::pair<int, unit*>> sorted_enemy_targets;
	for (unit* e : visible_enemy_units) {
		int d = 0;
		for (unit* u : my_units) {
			if (!u->is_on_map) continue;
			if (u->type == ranger || u->type == mage || u->type == knight) {
				int l = lengthsq(e->pos - u->pos);
				if (l <= u->attack_range) d += u->damage;
			}
		}
		sorted_enemy_targets.push_back({d, e});
	}
	std::sort(sorted_enemy_targets.begin(), sorted_enemy_targets.end(), [&](auto& a, auto& b) {
		return a.first > b.first;
	});

	auto enemy_targets_transform_f = [&](auto& v) {
		return v.second;
	};
	auto enemy_targets = make_transform_range(sorted_enemy_targets, std::ref(enemy_targets_transform_f));

	for (unit* u : my_units) {
		if (!u->is_on_map) continue;
		if (u->type == worker || u->is_building) continue;
		//if (u->controller->last_action_frame == current_frame) continue;

		if (u->type == knight && knight_research_level >= 3 && u->ability_heat < 10) {
			unit* target = get_best_score_copy(enemy_targets, [&](unit* e) {
				if (e->health == 0) return std::numeric_limits<int>::max();
				int d = lengthsq(e->pos - u->pos);
				if (d > 10) return std::numeric_limits<int>::max();
				int damage = u->damage - e->armor;
				int hp = e->health - damage;
				int r = std::max(hp, 0);
				if (e->type == mage) r /= 8;
				if (e->type == worker) r += 250;
				return r;
			}, std::numeric_limits<int>::max());

			if (target) {
				bc_GameController_javelin(gc, u->id, target->id);
				check_error("javelin (default knight)");
				u->controller->last_action_frame = current_frame;
				u->ability_heat += u->ability_cooldown;
				int damage = std::min(u->damage - target->armor, target->health);
				total_damage_dealt += damage;
				total_javelin_damage += damage;
				target->health -= damage;
				if (target->health <= 0) {
					++units_killed;
					target->health = 0;
					target->dead = true;
					update_groups(target);
					target->p->get_tile(target->pos).u = nullptr;
				}
			}
		}

		if (u->attack_heat >= 10) continue;

		if (u->type == ranger) {
			unit* target = get_best_score_copy(enemy_targets, [&](unit* e) {
				if (e->health == 0) return std::numeric_limits<int>::max();
				int d = lengthsq(e->pos - u->pos);
				if (d <= 10) return std::numeric_limits<int>::max();
				if (d > 50) return std::numeric_limits<int>::max();
				int damage = u->damage - e->armor;
				int hp = e->health - damage;
				int r = std::max(hp, 0);
				if (e->type == mage) r /= 8;
				if (e->type == worker) r += 250;
				return r;
			}, std::numeric_limits<int>::max());

			if (target) {
				bc_GameController_attack(gc, u->id, target->id);
				check_error("attack (default ranger)");
				u->controller->last_action_frame = current_frame;
				u->attack_heat += u->attack_cooldown;
				int damage = std::min(u->damage - target->armor, target->health);
				total_damage_dealt += damage;
				target->health -= damage;
				if (target->health <= 0) {
					++units_killed;
					target->health = 0;
					target->dead = true;
					update_groups(target);
					target->p->get_tile(target->pos).u = nullptr;
				}
			}

		} else if (u->type == healer) {
			unit* target = get_best_score_copy(my_units, [&](unit* e) {
				if (!e->is_on_map) return std::numeric_limits<int>::max();
				if (e->is_building) return std::numeric_limits<int>::max();
				if (e->health == e->max_health) {
					return 1000000 + default_attack_distgrid[e->index];
				}
				int l = lengthsq(e->pos - u->pos);
				int d = std::max(l - 30, 0);
				int r = (std::max(e->health, e->max_health) / 30 * 30 / (u->type + 1) + d * 10) * 100 - std::min(l, 30);
				if (damage_grid[e->index] == 0) r += 100;
				return r;
			}, std::numeric_limits<int>::max());

			u->controller->heal_target = target;
			if (target && target->health < target->max_health && lengthsq(u->pos - target->pos) <= 30) {
				bc_GameController_heal(gc, u->id, target->id);
				check_error("heal");
				u->controller->last_action_frame = current_frame;
				u->attack_heat += u->attack_cooldown;
				int heal = std::min(-u->damage, target->max_health - target->health);
				total_damage_healed += heal;
				target->health += heal;
				++heal_hits;
			} else ++heal_misses;
		} else if (u->type == knight) {
			unit* target = get_best_score_copy(enemy_targets, [&](unit* e) {
				if (e->health == 0) return std::numeric_limits<int>::max();
				int d = lengthsq(e->pos - u->pos);
				if (d > 2) return std::numeric_limits<int>::max();
				int damage = u->damage - e->armor;
				int hp = e->health - damage;
				int r = std::max(hp, 0);
				if (e->type == mage) r /= 8;
				if (e->type == worker) r += 250;
				return r;
			}, std::numeric_limits<int>::max());

			if (target) {
				if (u->ability_heat < 10 && knight_research_level >= 3) error("should've javelined");
				bc_GameController_attack(gc, u->id, target->id);
				check_error("attack (default knight)");
				u->controller->last_action_frame = current_frame;
				u->attack_heat += u->attack_cooldown;
				int damage = std::min(u->damage - target->armor, target->health);
				total_damage_dealt += damage;
				target->health -= damage;
				if (target->health <= 0) {
					++units_killed;
					target->health = 0;
					target->dead = true;
					update_groups(target);
					target->p->get_tile(target->pos).u = nullptr;
				}
			}
		} else if (u->type == mage) {
			unit* best_target = nullptr;
			int best_target_score = std::numeric_limits<int>::max();
			xy best_target_pos;
			xy best_src_pos;
			int best_target_damage = 0;
			int best_target_kills = 0;
			bool best_is_blink = false;
			auto find_target = [&](xy tpos, bool is_blink) {
				auto f = [&](xy src_pos, xy pos) {
					for (unit* e : enemy_targets) {
						if (e->gone || e->health == 0) continue;
						int d = lengthsq(e->pos - pos);
						if (d > 30) continue;
						int kills = 0;
						int damage = std::min(e->health - u->damage + e->armor, e->health);
						if (damage >= e->health) ++kills;
						e->p->for_each_neighbor_tile(e->pos, [&](tile& t) {
							if (t.u) {
								int d = std::min(t.u->health - u->damage + t.u->armor, t.u->health);
								if (t.u->is_enemy) {
									damage += t.u->type == worker ? d / 2 : d;
									if (d >= t.u->health) ++kills;
								} else {
									damage -= d / 2;
								}
							}
							return true;
						});
						int score = -damage * 10000 + damage_grid[distgrid_index(pos)];
						if (score < best_target_score) {
							best_target_score = score;
							best_target = e;
							best_target_pos = pos;
							best_src_pos = src_pos;
							best_target_damage = damage;
							best_target_kills = kills;
							best_is_blink = is_blink;
						}
					}
				};
				f(tpos, tpos);
				if (u->movement_heat < 10) {
					u->p->for_each_neighbor_tile(tpos, [&](tile& t) {
						if (t.walkable && !t.u) {
							f(tpos, t.pos);
						}
						return true;
					});
				}
			};

			auto attackfrom = [&](xy src_pos) {
				if (mage_research_level >= 4 && u->ability_heat < 10) {
					size_t width = u->p->width;
					size_t height = u->p->height;
					int range = sqrt_table[8];
					for (int y = -range; y <= range; ++y) {
						for (int x = -range; x <= range; ++x) {
							xy pos = src_pos + xy(x, y);
							if ((size_t)pos.x >= width || (size_t)pos.y >= height) continue;
							if (lengthsq(xy(x, y)) > 8) continue;
							auto& t = u->p->get_tile(pos);
							if (!t.walkable || t.u) continue;
							find_target(pos, true);
						}
					}
				} else {
					find_target(src_pos, false);
				}
			};
			attackfrom(u->pos);

			if (best_target) {
				unit* target = best_target;
				if (best_target_pos != u->pos) {
					int prev_damage = damage_grid[u->index];
					int next_damage = damage_grid[distgrid_index(best_target_pos)];
					if (next_damage > prev_damage && next_damage >= u->health && prev_damage < u->health) {
						//if (best_target_damage < 180 && best_target_kills == 0) {
						if (best_target_damage < 180) {
							target = nullptr;
						}
					}
					if (target) {
						if (best_is_blink) {
							bc_GameController_blink(gc, u->id, u->p->get_tile(best_src_pos).loc);
							check_error("blink");
							change_unit_position(u, best_src_pos);
							u->ability_heat += u->ability_cooldown;
						}
						if (best_src_pos != best_target_pos || !best_is_blink) {
							bc_GameController_move_robot(gc, u->id, bc_Direction_from_relpos(best_target_pos - u->pos));
							check_error("move (blink)");
							change_unit_position(u, best_target_pos);
							u->movement_heat += u->movement_cooldown;
						}
					}
				}
				if (target) {
					unit_attack(u, target, "mage");
					return actions();
				}
			}
		}

	}

}


bool overcharge() {

	unit* best_u = nullptr;
	unit* best_target = nullptr;
	int best_score = std::numeric_limits<int>::max();

	for (unit* u : my_units_of_type[healer]) {
		if (!u->is_on_map) continue;
		if (u->ability_heat >= 10) continue;

		for (unit* target : my_units) {
			if (!target->is_on_map) continue;
			if (target->type != ranger) continue;

			int d = lengthsq(target->pos - u->pos);
			if (d > 30) continue;
			if (target->attack_heat != target->attack_cooldown) continue;

			int s = -d + damage_grid[target->index] + default_attack_distgrid[target->index];
			if (s < best_score) {
				best_score = s;
				best_target = target;
				best_u = u;
			}

		}

	}

	if (best_target) {
		unit* u = best_u;
		unit* target = best_target;
		bc_GameController_overcharge(gc, u->id, target->id);
		check_error("overcharge");
		u->ability_heat += u->ability_cooldown;
		target->movement_heat = 0;
		target->attack_heat = 0;
		target->ability_heat = 0;
		++overcharges_used;
		overcharge();
		return true;
	}

	return false;
}

distgrid_t ranger_attack_distgrid;
distgrid_t mage_attack_distgrid;
distgrid_t heal_distgrid;

std::array<int, 2500> damage_grid;

void combat_distgrids() {

	std::array<bool, 2500> ranger_too_close;
	ranger_too_close.fill(false);

	damage_grid.fill(0);

	a_vector<size_t> ranger_attack_locations;
	a_vector<size_t> mage_attack_locations;

	size_t width = current_planet->width;
	size_t height = current_planet->height;

	for (unit* e : enemy_units) {
		if (e->gone) continue;

		auto set_inside_range = [&](int sqrange, auto& grid) {
			int range = sqrt_table[sqrange] + 1;
			for (int y = -range; y <= range; ++y) {
				for (int x = -range; x <= range; ++x) {
					xy pos = e->pos + xy(x, y);
					if ((size_t)pos.x >= width || (size_t)pos.y >= height) continue;
					xy npos(x, y);
					if (x > 0) --npos.x;
					else if (x < 0) ++npos.x;
					if (y > 0) --npos.y;
					else if (y < 0) ++npos.y;
					if (lengthsq(npos) > sqrange) continue;
					size_t index = distgrid_index(pos);
					grid[index] = true;
				}
			}
		};

		auto add_damage = [&](int sqrange, int damage, auto& grid) {
			int range = sqrt_table[sqrange] + 1;
			for (int y = -range; y <= range; ++y) {
				for (int x = -range; x <= range; ++x) {
					xy pos = e->pos + xy(x, y);
					if ((size_t)pos.x >= width || (size_t)pos.y >= height) continue;
					xy npos(x, y);
					if (x > 0) --npos.x;
					else if (x < 0) ++npos.x;
					if (y > 0) --npos.y;
					else if (y < 0) ++npos.y;
					if (lengthsq(npos) > sqrange) continue;
					size_t index = distgrid_index(pos);
					grid[index] += damage;
				}
			}
		};

		set_inside_range(10, ranger_too_close);

		if (e->damage) {
			//set_inside_range(e->attack_range, ranger_too_close);
			add_damage(e->attack_range, e->damage, damage_grid);
		}

	}

	for (unit* e : enemy_units) {
		if (e->gone) continue;

		auto add = [&](int sqrange, auto& exclude, auto& vec) {
			int range = sqrt_table[sqrange];
			for (int y = -range; y <= range; ++y) {
				for (int x = -range; x <= range; ++x) {
					xy pos = e->pos + xy(x, y);
					if ((size_t)pos.x >= width || (size_t)pos.y >= height) continue;
					size_t index = distgrid_index(pos);
					if (exclude[index]) continue;
					if (lengthsq(xy(x, y)) > sqrange) continue;
					tile& t = current_planet->get_tile(xy(x, y));
					if (!t.u) {
						vec.push_back(index);
					}
				}
			}
		};

		add(50, ranger_too_close, ranger_attack_locations);

		add(30, ranger_too_close, mage_attack_locations);

	}

//	log("%d ranger_attack_locations\n", ranger_attack_locations.size());
//	for (auto& v : ranger_attack_locations) {
//		log(" %d %d\n", v % 50, v / 50);
//	}

	ranger_attack_distgrid.fill(inf_distance);
	generate_distance_grid(ranger_attack_locations, current_planet->movecost, ranger_attack_distgrid);

	mage_attack_distgrid.fill(inf_distance);
	generate_distance_grid(mage_attack_locations, current_planet->movecost, mage_attack_distgrid);

	heal_distgrid.fill(inf_distance);
	generate_distance_grid(make_filter_range(my_units, [&](unit* u) {
		return u->type == ranger || u->type == knight;
	}), current_planet->movecost, heal_distgrid);

	knight_attack_distgrid.fill(inf_distance);
	generate_distance_grid(make_filter_range(enemy_units, [&](unit* u) {
		return u->type == ranger || u->type == factory;
	}), current_planet->movecost, knight_attack_distgrid);

}

void call_units_to_rockets() {
	if (current_planet != earth) return;
	a_vector<unit*> vec;
	vec.reserve(my_units.size());
	for (unit* u : my_units) {
		if (!u->is_on_map) continue;
		if (u->type == ranger || u->type == healer || u->type == mage || u->type == knight) {
			vec.push_back(u);
		}
	}
	std::sort(vec.begin(), vec.end(), [&](unit* a, unit* b) {
		return rocket_distgrid[a->index] < rocket_distgrid[b->index];
	});
	size_t space = 0;
	for (unit* u : my_completed_units_of_type[rocket]) {
		space += u->max_capacity - u->loaded_units.size();
	}
	if (space > vec.size()) space = vec.size();
	for (size_t i = 0; i != space; ++i) {
		vec[i]->controller->go_to_rocket_frame = current_frame;
	}
}

void update_distgrids() {

	a_vector<size_t> gather_positions;
	for (int i = 0; i != 2; ++i) {
		if (i && !gather_positions.empty()) break;
		for (auto& t : current_planet->tiles)  {
			if (t.karbonite >= (i ? 1 : 3)) {
				auto& movecost = current_planet->movecost;
				for_each_neighbor_index(t.pos, [&](size_t index) {
					if (movecost[index] != inf_distance) {
						gather_positions.push_back(index);
					}
					return true;
				});
			}
		}
	}
	default_gather_distgrid.fill(inf_distance);
	generate_distance_grid(gather_positions, current_planet->movecost, default_gather_distgrid);

	default_attack_distgrid.fill(inf_distance);
	generate_distance_grid(enemy_units, current_planet->movecost, default_attack_distgrid);

	enemy_factory_distgrid.fill(inf_distance);
	generate_distance_grid(make_filter_range(enemy_units, [&](unit* u) {
		return u->type == factory;
	}), current_planet->movecost, enemy_factory_distgrid);

	any_unit_distgrid.fill(inf_distance);
	generate_distance_grid(make_filter_range(live_units, [&](unit* u) {
		return u->is_on_map && !u->gone;
	}), current_planet->movecost, any_unit_distgrid);

	std::array<bool, 2500> next_to_factory{};
	a_vector<size_t> near_factory_locations;
	for (unit* u : my_completed_units_of_type[factory]) {
		for_each_range_index(u->pos, u->index, 1, [&](size_t index) {
			next_to_factory[index] = true;
		});
	}
	for (unit* u : my_completed_units_of_type[factory]) {
		for_each_range_index(u->pos, u->index, 2, [&](size_t index) {
			if (!next_to_factory[index]) {
				near_factory_locations.push_back(index);
			}
		});
	}

	near_factory_distgrid.fill(inf_distance);
	generate_distance_grid(near_factory_locations, current_planet->movecost, near_factory_distgrid);

	rocket_distgrid.fill(inf_distance);
	generate_distance_grid(my_completed_units_of_type[rocket], current_planet->movecost, rocket_distgrid);

	size_t best_rocket_size = -get_best_score_value(my_units_of_type[rocket], [&](unit* u) {
		return -(int)u->loaded_units.size();
	});

	best_rocket_distgrid.fill(inf_distance);
	generate_distance_grid(make_filter_range(my_units_of_type[rocket], [&](unit* u) {
		return u->is_completed && u->loaded_units.size() >= best_rocket_size;
	}), current_planet->movecost, best_rocket_distgrid);

	call_units_to_rockets();

	fow_distgrid.fill(inf_distance);
	if (current_planet == mars) {
		generate_distance_grid(make_filter_range(current_planet->tiles, [&](tile& t) {
			return !t.visible;
		}), current_planet->movecost, fow_distgrid);
	} else {
		generate_distance_grid(make_filter_range(current_planet->tiles, [&](tile& t) {
			return t.last_seen == 0 || current_frame - t.last_seen >= 10;
		}), current_planet->movecost, fow_distgrid);
	}

	a_vector<size_t> build_positions;
	a_vector<size_t> to_add;
	for (unit* u : my_buildings)  {
		if (!u->is_completed) {
			auto& movecost = current_planet->movecost;
			to_add.clear();
			int n_builders = 0;
			for_each_neighbor_index(u->pos, [&](size_t index) {
				if (movecost[index] != inf_distance) {
					auto& t = current_planet->get_tile(index % 50, index / 50);
					if (!t.u || !t.u->is_building) {
						to_add.push_back(index);
					}
					if (t.u && t.u->is_mine && t.u->type == worker) {
						++n_builders;
					}
				}
				return true;
			});
			if (n_builders < 5 || go_for_knights) {
				for (size_t index : to_add) {
					build_positions.push_back(index);
				}
			}
		}
	}
	default_build_distgrid.fill(inf_distance);
	generate_distance_grid(build_positions, current_planet->movecost, default_build_distgrid);

	combat_distgrids();
}

int best_neighbor_distance(distgrid_t& grid, size_t index) {
	int r = inf_distance;
	for_each_neighbor_index(index, [&](size_t nindex) {
		r = std::min(r, (int)grid[nindex]);
		return true;
	});
	return r;
}

void research() {

	bool prev_go_for_knights = go_for_knights;
	go_for_knights = false;
	for (unit* u : (factories ? my_units_of_type[factory] : my_workers)) {
		int d = best_neighbor_distance(enemy_factory_distgrid, u->index);
		if (d == inf_distance) d = best_neighbor_distance(default_attack_distgrid, u->index);
		if (d <= 16 && enemy_rangers + enemy_mages * 2 <= knights + 2) {
			go_for_knights = true;
			break;
		}
	}
	if (current_frame >= 10 && !prev_go_for_knights) go_for_knights = false;
	log("go_for_knights is %d\n", go_for_knights);

	auto* re = bc_GameController_research_info(gc);

	auto* q = bc_ResearchInfo_queue(re);

	mage_research_level = bc_ResearchInfo_get_level(re, Mage);
	healer_research_level = bc_ResearchInfo_get_level(re, Healer);
	rocket_research_level = bc_ResearchInfo_get_level(re, Rocket);
	knight_research_level = bc_ResearchInfo_get_level(re, Knight);

	log("mage_research_level is %d\n", mage_research_level);
	log("healer_research_level is %d\n", healer_research_level);
	log("rocket_research_level is %d\n", rocket_research_level);
	log("knight_research_level is %d\n", knight_research_level);

	if (current_planet == mars) {
		delete_bc_ResearchInfo(re);
		return;
	}

	auto any_reachable_enemies = [&]() {
		for (unit* u : (my_workers.empty() ? my_units : my_workers)) {
			if (!u->is_on_map) continue;
			if (!u->is_building && default_attack_distgrid[u->index] != inf_distance) {
				return true;
			}
		}
		return false;
	};

	bool is_researching = false;
	auto research = [&](bc_UnitType t, int level) {
		if (is_researching) return;
		if (bc_ResearchInfo_get_level(re, t) >= level) return;
		is_researching = true;
		bc_GameController_queue_research(gc, t);
		check_error("research");
	};

	if (bc_VecUnitType_len(q) == 0) {

		if (!any_reachable_enemies() || enemy_rockets) research(Rocket, 1);
		if (go_for_knights) research(Knight, 1);
		research(Healer, 3);
		//research(Ranger, 1);
		research(Rocket, 1);
		research(Knight, 3);
		research(Ranger, 2);
		//research(Knight, 3);
		research(Mage, 4);
		research(Rocket, 1);
		research(Ranger, 2);
		research(Healer, 2);
		research(Rocket, 3);
	} else {
		auto t = bc_VecUnitType_index(q, 0);
		if (t == Mage) {
			if (mage_research_level == 3) {
				blink_done_frame = current_frame + bc_ResearchInfo_rounds_left(re);
			}
		}
		if (t == Healer) {
			if (healer_research_level == 2) {
				overcharge_done_frame = current_frame + bc_ResearchInfo_rounds_left(re);
			}
		}
		if (t != Rocket && rocket_research_level == 0) {
//			if (!any_reachable_enemies() || enemy_rockets) {
//				bc_GameController_reset_research(gc);
//				research(Rocket, 1);
//			}
		}
	}

	log("blink_done_frame is %d\n", blink_done_frame);

	delete_bc_VecUnitType(q);

	delete_bc_ResearchInfo(re);

}

unit* get_early_harvest_worker() {
	unit* best_worker = nullptr;
	int best_s = std::numeric_limits<int>::max();
	if (current_frame < 25 && workers >= 3) {
		for (unit* n : my_workers) {
			if (!n->is_on_map) continue;
			int s = std::min(default_gather_distgrid[n->index], default_attack_distgrid[n->index]);
			if (n->ability_heat < 10) s -= 10;
			if (s < best_s) {
				best_s = s;
				best_worker = n;
			}
		}
	}
	return best_worker;
}

uint32_t rand_state;

void action_update() {

	rand_state = current_frame;

	karbonite = bc_GameController_karbonite(gc);

	workers = my_workers.size();
	factories = my_units_of_type[factory].size();
	rockets = my_units_of_type[rocket].size();
	usable_factories = 0;
	for (unit* u : my_units_of_type[factory]) {
		if (!u->p) continue;
		int n = 0;
		u->p->for_each_neighbor_tile(u->pos, [&](tile& t) {
			if (t.walkable && !t.u) ++n;
			return true;
		});
		if (n || !u->is_completed) u->controller->usable_counter = 0;
		else ++u->controller->usable_counter;
		if (u->controller->usable_counter < 15) {
			++usable_factories;
		}
	}

	rangers = my_units_of_type[ranger].size();
	healers = my_units_of_type[healer].size();
	knights = my_units_of_type[knight].size();
	mages = my_units_of_type[mage].size();

	enemy_rangers = 0;
	enemy_knights = 0;
	enemy_healers = 0;
	enemy_mages = 0;
	enemy_rockets = 0;
	for (unit* u : enemy_units) {
		if (u->type == ranger) ++enemy_rangers;
		if (u->type == knight) ++enemy_knights;
		if (u->type == healer) ++enemy_healers;
		if (u->type == mage) ++enemy_mages;
		if (u->type == rocket) ++enemy_rockets;
	}

	//if (current_frame < 2) update_distgrids();
	update_distgrids();

	research();

	actions();

	update_distgrids();

	for (unit* u : my_units) {
		if (u->is_building) continue;
		if (!u->is_on_map) continue;
		unit_controller* c = u->controller;
		if (c->nomove_frame == current_frame) c->distgrid = nullptr;
		else if (u->type == worker) {
			c->distgrid = &default_gather_distgrid;

			int gather_d = default_gather_distgrid[u->index];
			int build_d = default_build_distgrid[u->index];

			if (build_d <= gather_d && build_d <= 3) {
				if (get_early_harvest_worker() != u) {
					c->distgrid = &default_build_distgrid;
				}
			} else {
				if (c->harvest_nomove_frame == current_frame) c->distgrid = nullptr;
			}

			if (c->distgrid && (*c->distgrid)[u->index] == inf_distance) {
				c->distgrid = &default_attack_distgrid;
			}

		} else if (u->type == ranger) {
			c->distgrid = &ranger_attack_distgrid;
			if (enemy_units.size() <= my_units.size() / 10) {
				c->distgrid = &default_attack_distgrid;
			}
		} else if (u->type == healer) {
			c->distgrid = &ranger_attack_distgrid;
			if (heal_distgrid[u->index] >= 4) c->distgrid = &heal_distgrid;
			if (enemy_units.size() <= my_units.size() / 10) {
				c->distgrid = &default_attack_distgrid;
			}
		} else if (u->type == mage) {
			c->distgrid = &mage_attack_distgrid;
			if (mage_research_level >= 4) {
				c->distgrid = &ranger_attack_distgrid;
			}
		} else if (u->type == knight) {
			if (knight_attack_distgrid[u->index] != inf_distance) {
				c->distgrid = &knight_attack_distgrid;
			} else {
				c->distgrid = &default_attack_distgrid;
			}
		} else {
			c->distgrid = &default_attack_distgrid;
		}
		if ((u->type == ranger || u->type == healer || u->type == mage || u->type == knight) && current_planet == earth) {
			int r = rocket_distgrid[u->index];
			if (r != inf_distance && u->controller->go_to_rocket_frame == current_frame) {
				if ((r <= 3 || current_frame + r * 3 >= 700) || default_attack_distgrid[u->index] == inf_distance || my_units_of_type[rocket].size() >= 3) {
					if (best_rocket_distgrid[u->index] <= 3) {
						c->distgrid = &best_rocket_distgrid;
					} else {
						c->distgrid = &rocket_distgrid;
					}
				}
			}
		}
		if (c->distgrid) {
			c->goal_distance = (*c->distgrid)[u->index] - damage_grid[u->index];
			move_scores_from_distance_grid(c->move_scores, c->u->index, *c->distgrid);
			if (u->type != worker && damage_grid[u->index]) {
				c->goal_distance = -1000 + default_attack_distgrid[u->index];
			}
		} else {
			c->goal_distance = inf_distance;
			c->move_scores.fill(99);
			c->move_scores[4] = 0;
		}

		//if (u->type == ranger && u->attack_heat >= 10 && damage_grid[u->index] >= u->health) {
		if (u->type == ranger && damage_grid[u->index] >= u->health) {
			move_scores_sub_from_distance_grid(c->move_scores, c->u->index, default_attack_distgrid);
		}
		if (u->type == healer && c->heal_target && damage_grid[u->index]) {
			if (default_attack_distgrid[u->index] <= default_attack_distgrid[c->heal_target->index] || c->heal_target->type == healer) {
				move_scores_sub_from_distance_grid(c->move_scores, c->u->index, default_attack_distgrid);
				move_scores_sub_from_distance_grid(c->move_scores, c->u->index, default_attack_distgrid);
			}
		}
		if (u->type == worker && c->distgrid == &default_attack_distgrid && workers <= 8) {
			for (auto& v : c->move_scores) v = 0;
			move_scores_sub_sqrt_from_distance_grid(c->move_scores, c->u->index, default_attack_distgrid);
			move_scores_sub_sqrt_from_distance_grid(c->move_scores, c->u->index, wall_distgrid);
			move_scores_add_sqrt_from_distance_grid(c->move_scores, c->u->index, near_factory_distgrid);
		}

		bool all_inf = true;
		for (int v : c->move_scores) {
			if (v != inf_distance) {
				all_inf = false;
			}
		}
		if (all_inf) {
			if (u->id % 8 && fow_distgrid[u->index] != inf_distance) {
				move_scores_from_distance_grid(c->move_scores, u->index, fow_distgrid);
			} else {
				for (int& v : c->move_scores) {
					rand_state = rand_state * 22695477 + 1;
					v = (rand_state >> 16) & 0x7fff;
				}
				c->move_scores.at(c->move_n) /= 256;
				c->move_scores[4] += 0x10000;
			}
		}

		//log("c->goal_distance is %d\n", c->goal_distance);
		//log("movecost is %d\n", current_planet->movecost[u->index]);

//		if (healer_research_level >= 3) {
//			if (overcharge()) {
//				return action_update();
//			}
//		}
	}

	for (unit* u : my_units) {
		if (u->is_building) continue;
		unit_controller* c = u->controller;
	}

	move_units();

	actions();

}

