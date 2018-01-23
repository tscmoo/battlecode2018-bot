extern "C" {
#define this this_
#include <bc.h>
#undef this
}

#include <cstdlib>
#include <cmath>
#include <string>
#include <memory>
#include <utility>
#include <algorithm>
#include <queue>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include "strf.h"
#include "containers.h"
#include "common.h"

struct botimpl {

int current_frame = 0;

template<typename... T>
a_string format(const char* fmt, T&&... args) {
	a_string str;
	strf::format(str, fmt, std::forward<T>(args)...);
	return str;
}

template<typename... T>
void log(const char* fmt, T&&... args) {
	return;
	a_string str;
	strf::format(str, fmt, std::forward<T>(args)...);
	fprintf(stdout, "%05d: %s", current_frame, str.c_str());
	fflush(stdout);
}


template<typename... T>
void error(const char* fmt, T&&... args) {
	return;
	a_string str;
	strf::format(str, fmt, std::forward<T>(args)...);
	log("throwing exception %s\n", str);

	fflush(stdout);
	throw std::runtime_error(str);
}

template<typename... T>
bool check_error(const char* fmt, T&&... args) {
	if (!bc_has_err()) return false;
	return true;
	char *err;
	bc_get_last_err(&err);
	a_string str;
	strf::format(str, fmt, std::forward<T>(args)...);
	error("error: %s: %s\n", str, err);
	bc_free_string(err);
	return true;
}

bc_GameController *gc = nullptr;

int my_team = 0;
int heal_hits = 0;
int heal_misses = 0;
int total_mage_damage = 0;
int total_damage_healed = 0;
int total_damage_taken = 0;
int total_damage_dealt = 0;
int overcharges_used = 0;
int total_javelin_damage = 0;
int overcharge_javelins = 0;

int units_lost = 0;
int units_killed = 0;

struct tile;
struct planet;
struct unit;

#include "movement.h"
#include "grid.h"
#include "units.h"
#include "unit_controls.h"
#include "action.h"

void run() {

	gc = new_bc_GameController();

	my_team = (int)bc_GameController_team(gc);

	grid_init();
	units_init();
	movement_init();
	action_init();

	bc_GameController_next_turn(gc);

	double longest_frame = 0;
	double frame_time_sum = 0.0;
	int frames = 0;

	while (true) {

		auto start = std::chrono::high_resolution_clock::now();

		current_frame = bc_GameController_round(gc);

		grid_update();
		units_update();
		movement_update();
		action_update();
		unit_controls_update();

		if (heal_hits + heal_misses) {
			log("heal hits: %d misses: %d  %d%%\n", heal_hits, heal_misses, heal_hits * 100 / (heal_hits + heal_misses));
		}

		if (mages_made) {
			log("mages: %d damage %d  %.02f per mage\n", mages_made, total_mage_damage, (double)total_mage_damage / mages_made);
		}

		log("damage dealt: %d taken: %d healed: %d (%.02f%%)\n", total_damage_dealt, total_damage_taken, total_damage_healed, (double)total_damage_healed * 100 / total_damage_taken);
		log("units killed: %d lost: %d\n", units_killed, units_lost);
		log("overcharges used: %d\n", overcharges_used);

		double t = std::chrono::duration_cast<std::chrono::duration<double, std::ratio<1, 1000>>>(std::chrono::high_resolution_clock::now() - start).count();
		log("frame took %gms\n", t);

		if (t > longest_frame) longest_frame = t;
		frame_time_sum += t;
		++frames;

		//printf("%dms left, longest frame: %gms, average: %gms\n", bc_GameController_get_time_left_ms(gc), longest_frame, frame_time_sum / frames);
		//fflush(stdout);

		//if (current_frame == 538) break;

		bc_GameController_next_turn(gc);
	}
}

};

int main() {

	auto bot = std::make_unique<botimpl>();

	bot->run();

	return 0;
}
