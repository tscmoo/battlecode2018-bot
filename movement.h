
using distgrid_t = std::array<int16_t, 2500>;

const int16_t inf_distance = 25000;

const int relpos_x[9] = {-1, 0, 1, -1, 0, 1, -1, 0, 1};
const int relpos_y[9] = {-1, -1, -1, 0, 0, 0, 1, 1, 1};

const std::array<xy, 9> relpos{{{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {0, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};

const bc_Direction direction_to_bc_Direction[9] =  {Southwest, South, Southeast, West, Center, East, Northwest, North, Northeast};

int direction_from_relpos(xy pos) {
	auto i = std::find(relpos.begin(), relpos.end(), pos);
	if (i == relpos.end()) error("direction_from_relpos: pos is not an adjacent position");
	return i - relpos.begin();
}

bc_Direction bc_Direction_from_relpos(xy pos) {
	return direction_to_bc_Direction[direction_from_relpos(pos)];
}

struct open_entry {
	size_t index;
	int distance;
};
static constexpr size_t opensize = 2048;

void generate_distance_grid(planet* p, std::array<open_entry, opensize>& open, size_t open_top, const distgrid_t& movecost, distgrid_t& distance_grid) {

	std::array<open_entry, opensize> open2;
	size_t open2_top = 0;

	size_t open_bot = 0;
	size_t open2_bot = 0;

	size_t last_x = p->width - 1;
	size_t last_y = p->height - 1;

	if (last_x >= 50 || last_y >= 50) error("map is too big!");

	int iterations = 0;
	while ((open_bot != open_top || open2_bot != open2_top) && iterations < 5000) {
		++iterations;
		open_entry* a = &open[open_bot % opensize];
		open_entry* b = &open2[open2_bot % opensize];
		size_t cur_index;
		int cur_distance;
		if (open2_bot == open2_top || (a->distance <= b->distance && open_bot != open_top)) {
			cur_index = a->index;
			cur_distance = a->distance;
			++open_bot;
		} else {
			cur_index = b->index;
			cur_distance = b->distance;
			++open2_bot;
		}

		auto add = [&](size_t index) {
			int cost = movecost[index];
			int next_distance = cur_distance + cost;
			if (distance_grid[index] - 1 <= next_distance) return;
			distance_grid[index] = next_distance;

			if (cost == 1) open[open_top++ % opensize] = { index, next_distance };
			else open2[open2_top++ % opensize] = { index, next_distance };
		};
		int x = cur_index % 50;
		int y = cur_index / 50;
		if (x != last_x) {
			add(cur_index + 1);
			if (y != last_y) add(cur_index + 1 + 50);
			if (y != 0) add(cur_index + 1 - 50);
		}
		if (y != last_y) {
			add(cur_index + 50);
			if (x != 0) add(cur_index - 1 + 50);
		}
		if (x != 0) {
			add(cur_index - 1);
			if (y != 0) add(cur_index - 1 - 50);
		}
		if (y != 0) add(cur_index - 50);

	}

}


template<typename starts_T>
void generate_distance_grid(starts_T&& starts, const distgrid_t& movecost, distgrid_t& distance_grid) {
	generate_distance_grid(current_planet, std::forward<starts_T>(starts), movecost, distance_grid);
}

template<typename starts_T>
void generate_distance_grid(planet* p, starts_T&& starts, const distgrid_t& movecost, distgrid_t& distance_grid) {

	std::array<open_entry, opensize> open;
	size_t open_top = 0;

	for (size_t index : make_transform_range(starts, [this](auto& v) {
		return this->distgrid_index(v);
	})) {
		if (distance_grid[index] == 0) continue;
		open[open_top++ % opensize] = { index, 0 };
		distance_grid[index] = 0;
	}

	generate_distance_grid(p, open, open_top, movecost, distance_grid);
}

template<typename F>
void for_each_neighbor_index(xy pos, size_t index, F&&f) {
	size_t last_x = current_planet->width - 1;
	size_t last_y = current_planet->height - 1;
	if (pos.x != last_x) {
		if (!f(index + 1)) return;
		if (pos.y != last_y && !f(index + 1 + 50)) return;
		if (pos.y != 0 && !f(index + 1 - 50)) return;
	}
	if (pos.y != last_y) {
		if (!f(index + 50)) return;
		if (pos.x != 0 && !f(index - 1 + 50)) return;
	}
	if (pos.x != 0) {
		if (!f(index - 1)) return;
		if (pos.y != 0 && !f(index - 1 - 50)) return;
	}
	if (pos.y != 0 && !f(index - 50)) return;
}

template<typename F>
void for_each_neighbor_pos_index(xy pos, size_t index, F&&f) {
	size_t last_x = current_planet->width - 1;
	size_t last_y = current_planet->height - 1;
	if (pos.x != last_x) {
		if (!f(pos + xy(1, 0), index + 1)) return;
		if (pos.y != last_y && !f(pos + xy(1, 1), index + 1 + 50)) return;
		if (pos.y != 0 && !f(pos + xy(1, -1), index + 1 - 50)) return;
	}
	if (pos.y != last_y) {
		if (!f(pos + xy(0, 1), index + 50)) return;
		if (pos.x != 0 && !f(pos + xy(-1, 1), index - 1 + 50)) return;
	}
	if (pos.x != 0) {
		if (!f(pos + xy(-1, 0), index - 1)) return;
		if (pos.y != 0 && !f(pos + xy(-1, -1), index - 1 - 50)) return;
	}
	if (pos.y != 0 && !f(pos + xy(0, -1), index - 50)) return;
}

template<typename F>
void for_each_neighbor_index(xy pos, F&&f) {
	for_each_neighbor_index(pos, distgrid_index(pos), std::forward<F>(f));
}

template<typename F>
void for_each_neighbor_index(size_t index, F&&f) {
	for_each_neighbor_index(xy(index % 50, index / 50), index, std::forward<F>(f));
}

template<typename F>
void for_each_neighbor_pos_index(xy pos, F&&f) {
	for_each_neighbor_pos_index(pos, distgrid_index(pos), std::forward<F>(f));
}

template<typename F>
void for_each_range_index(xy pos, size_t index, int range, F&&f) {
	size_t width = current_planet->width;
	size_t height = current_planet->height;
	for (int y = -range; y <= range; ++y) {
		for (int x = -range; x <= range; ++x) {
			int rx = pos.x + y;
			int ry = pos.y + x;
			if ((size_t)rx >= width) continue;
			if ((size_t)ry >= height) continue;
			f((size_t)rx + (size_t)ry * 50u);
		}
	}
}

void move_scores_from_distance_grid(std::array<int, 9>& move_scores, size_t index, distgrid_t& distance_grid) {
	size_t last_x = current_planet->width - 1;
	size_t last_y = current_planet->height - 1;
	int x = index % 50;
	int y = index / 50;
	move_scores[4] = distance_grid[index];
	if (x != last_x) {
		move_scores[5] = distance_grid[index + 1];
		if (y != last_y) move_scores[8] = distance_grid[index + 1 + 50];
		else move_scores[8] = inf_distance;
		if (y != 0) move_scores[2] = distance_grid[index + 1 - 50];
		else move_scores[2] = inf_distance;
	}
	if (y != last_y) {
		move_scores[7] = distance_grid[index + 50];
		if (x != 0) move_scores[6] = distance_grid[index - 1 + 50];
		else move_scores[6] = inf_distance;
	}
	if (x != 0) {
		move_scores[3] = distance_grid[index - 1];
		if (y != 0) move_scores[0] = distance_grid[index - 1 - 50];
		else move_scores[0] = inf_distance;
	}
	if (y != 0) move_scores[1] = distance_grid[index - 50];
	else move_scores[1] = inf_distance;
}

void move_scores_add_from_distance_grid(std::array<int, 9>& move_scores, size_t index, distgrid_t& distance_grid) {
	size_t last_x = current_planet->width - 1;
	size_t last_y = current_planet->height - 1;
	int x = index % 50;
	int y = index / 50;
	move_scores[4] += distance_grid[index];
	if (x != last_x) {
		move_scores[5] += distance_grid[index + 1];
		if (y != last_y) move_scores[8] += distance_grid[index + 1 + 50];
		else move_scores[8] += inf_distance;
		if (y != 0) move_scores[2] += distance_grid[index + 1 - 50];
		else move_scores[2] += inf_distance;
	}
	if (y != last_y) {
		move_scores[7] += distance_grid[index + 50];
		if (x != 0) move_scores[6] += distance_grid[index - 1 + 50];
		else move_scores[6] += inf_distance;
	}
	if (x != 0) {
		move_scores[3] += distance_grid[index - 1];
		if (y != 0) move_scores[0] += distance_grid[index - 1 - 50];
		else move_scores[0] += inf_distance;
	}
	if (y != 0) move_scores[1] += distance_grid[index - 50];
	else move_scores[1] += inf_distance;
}

void move_scores_add_sqrt_from_distance_grid(std::array<int, 9>& move_scores, size_t index, distgrid_t& distance_grid) {
	size_t last_x = current_planet->width - 1;
	size_t last_y = current_planet->height - 1;
	int x = index % 50;
	int y = index / 50;
	auto f = [&](auto v) {
		if (v == inf_distance) return inf_distance;
		return (int16_t)sqrt_table[v * 16];
	};
	move_scores[4] += f(distance_grid[index]);
	if (x != last_x) {
		move_scores[5] += f(distance_grid[index + 1]);
		if (y != last_y) move_scores[8] += f(distance_grid[index + 1 + 50]);
		else move_scores[8] += inf_distance;
		if (y != 0) move_scores[2] += f(distance_grid[index + 1 - 50]);
		else move_scores[2] += inf_distance;
	}
	if (y != last_y) {
		move_scores[7] += f(distance_grid[index + 50]);
		if (x != 0) move_scores[6] += f(distance_grid[index - 1 + 50]);
		else move_scores[6] += inf_distance;
	}
	if (x != 0) {
		move_scores[3] += f(distance_grid[index - 1]);
		if (y != 0) move_scores[0] += f(distance_grid[index - 1 - 50]);
		else move_scores[0] += inf_distance;
	}
	if (y != 0) move_scores[1] += f(distance_grid[index - 50]);
	else move_scores[1] += inf_distance;
}

void move_scores_sub_sqrt_from_distance_grid(std::array<int, 9>& move_scores, size_t index, distgrid_t& distance_grid) {
	size_t last_x = current_planet->width - 1;
	size_t last_y = current_planet->height - 1;
	int x = index % 50;
	int y = index / 50;
	auto f = [&](auto v) {
		if (v == inf_distance) return inf_distance;
		return (int16_t)sqrt_table[v * 16];
	};
	move_scores[4] -= f(distance_grid[index]);
	if (x != last_x) {
		move_scores[5] -= f(distance_grid[index + 1]);
		if (y != last_y) move_scores[8] -= f(distance_grid[index + 1 + 50]);
		else move_scores[8] -= inf_distance;
		if (y != 0) move_scores[2] -= f(distance_grid[index + 1 - 50]);
		else move_scores[2] -= inf_distance;
	}
	if (y != last_y) {
		move_scores[7] -= f(distance_grid[index + 50]);
		if (x != 0) move_scores[6] -= f(distance_grid[index - 1 + 50]);
		else move_scores[6] -= inf_distance;
	}
	if (x != 0) {
		move_scores[3] -= f(distance_grid[index - 1]);
		if (y != 0) move_scores[0] -= f(distance_grid[index - 1 - 50]);
		else move_scores[0] -= inf_distance;
	}
	if (y != 0) move_scores[1] -= f(distance_grid[index - 50]);
	else move_scores[1] -= inf_distance;
}


void move_scores_sub_from_distance_grid(std::array<int, 9>& move_scores, size_t index, distgrid_t& distance_grid) {
	size_t last_x = current_planet->width - 1;
	size_t last_y = current_planet->height - 1;
	int x = index % 50;
	int y = index / 50;
	move_scores[4] -= distance_grid[index];
	if (x != last_x) {
		move_scores[5] -= distance_grid[index + 1];
		if (y != last_y) move_scores[8] -= distance_grid[index + 1 + 50];
		else move_scores[8] -= inf_distance;
		if (y != 0) move_scores[2] -= distance_grid[index + 1 - 50];
		else move_scores[2] -= inf_distance;
	}
	if (y != last_y) {
		move_scores[7] -= distance_grid[index + 50];
		if (x != 0) move_scores[6] -= distance_grid[index - 1 + 50];
		else move_scores[6] -= inf_distance;
	}
	if (x != 0) {
		move_scores[3] -= distance_grid[index - 1];
		if (y != 0) move_scores[0] -= distance_grid[index - 1 - 50];
		else move_scores[0] -= inf_distance;
	}
	if (y != 0) move_scores[1] -= distance_grid[index - 50];
	else move_scores[1] -= inf_distance;
}

void move_scores_sub_if_not_inf_from_distance_grid(std::array<int, 9>& move_scores, size_t index, distgrid_t& distance_grid) {
	size_t last_x = current_planet->width - 1;
	size_t last_y = current_planet->height - 1;
	int x = index % 50;
	int y = index / 50;
	auto not_inf = [&](int v) {
		return v == inf_distance ? 0 : v;
	};
	move_scores[4] -= not_inf(distance_grid[index]);
	if (x != last_x) {
		move_scores[5] -= not_inf(distance_grid[index + 1]);
		if (y != last_y) move_scores[8] -= not_inf(distance_grid[index + 1 + 50]);
		else move_scores[8] -= inf_distance;
		if (y != 0) move_scores[2] -= not_inf(distance_grid[index + 1 - 50]);
		else move_scores[2] -= inf_distance;
	}
	if (y != last_y) {
		move_scores[7] -= not_inf(distance_grid[index + 50]);
		if (x != 0) move_scores[6] -= not_inf(distance_grid[index - 1 + 50]);
		else move_scores[6] -= inf_distance;
	}
	if (x != 0) {
		move_scores[3] -= not_inf(distance_grid[index - 1]);
		if (y != 0) move_scores[0] -= not_inf(distance_grid[index - 1 - 50]);
		else move_scores[0] -= inf_distance;
	}
	if (y != 0) move_scores[1] -= not_inf(distance_grid[index - 50]);
	else move_scores[1] -= inf_distance;
}

size_t distgrid_index(size_t index) {
	return index;
}
size_t distgrid_index(xy pos) {
	return pos.y * 50u + pos.x;
}
size_t distgrid_index(const unit* u) {
	return distgrid_index(u->pos);
}
size_t distgrid_index(const tile& t) {
	return distgrid_index(t.pos);
}

void movement_init() {


}

void movement_update() {

}
