/* --------------------------------------------------------------------
EXTREME TUXRACER

Copyright (C) 1999-2001 Jasmin F. Patry (Tuxracer)
Copyright (C) 2010 Extreme Tux Racer Team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
---------------------------------------------------------------------*/

#ifdef HAVE_CONFIG_H
#include <etr_config.h>
#endif

#include "bh.h"
#include "textures.h"
#include "ogl.h"
#include "audio.h"
#include "font.h"
#include "tools.h"
#include "ogl_test.h"
#include "winsys.h"
// Added for direct race launch setup
#include "particles.h"
#include "course.h"
#include "env.h"
#include "game_ctrl.h"
#include "score.h"
#include "regist.h"
#include "loading.h"
#include <iostream>
#include <ctime>
#include <cstring>
#include <functional>
#include <memory>
#include <filesystem>
#include <optional>

#include "rayNEAT/rayNEAT.h"

TGameData g_game;

thread_local std::unique_ptr<Network> current_network = nullptr;

// Helper: find the most recent .neat file in a directory
static std::optional<std::string> find_latest_neat_file(const std::string &dir) {
	try {
		namespace fs = std::filesystem;
		if (!fs::exists(dir)) return std::nullopt;
		std::optional<std::string> best_path;
		fs::file_time_type best_time{};
		for (const auto &entry : fs::directory_iterator(dir)) {
			if (entry.is_regular_file() && entry.path().extension() == ".neat") {
				auto t = fs::last_write_time(entry);
				if (!best_path || t > best_time) {
					best_path = entry.path().string();
					best_time = t;
				}
			}
		}
		return best_path;
	} catch (...) {
		return std::nullopt;
	}
}

void InitGame() {
	g_game.toolmode = NONE;
	g_game.argument = 0;

	g_game.player = nullptr;
	g_game.start_player = 0;
	g_game.course = nullptr;
	g_game.mirrorred = false;
	g_game.character = nullptr;
	g_game.location_id = 0;
	g_game.light_id = 0;
	g_game.snow_id = 0;
	g_game.cup = 0;
	g_game.theme_id = 0;
	g_game.force_treemap = false;
	g_game.treesize = 3;
	g_game.treevar = 3;
}

void init_graphics() {
	Winsys.Init();
	InitOpenglExtensions();

	// theses resources must or should be loaded before splashscreen starts
	if (!Tex.LoadTextureList()) {
		Winsys.Quit();
		exit(1);
	}
	FT.LoadFontlist();
	FT.SetFontFromSettings();
	Music.LoadMusicList();
	Music.SetVolume(param.music_volume);
}

int run_game_once(bool simulated_only = false, SteeringFunc custom = {}) {
	g_game.simulated_only = simulated_only;
	// Directly set up a default race and go to Loading state (bypass SplashScreen)
	if (!simulated_only) {
		init_ui_snow();
	}

	Course.MakeStandardPolyhedrons();
	if (!simulated_only) {
		Sound.LoadSoundList();
	}
	(void)Char.LoadCharacterList();
	Course.LoadObjectTypes();
	(void)Course.LoadTerrainTypes();

	if (Env.LoadEnvironmentList()) {
		(void)Course.LoadCourseList();
		Score.LoadHighScore();
		Events.LoadEventList();
	}

	if (!simulated_only) {
		if (Players.LoadAvatars()) { // before LoadPlayers !!!
			Players.LoadPlayers();
		}
	} else {
		Players.LoadPlayers();
	}

	// Auto-configure default player/character
	Players.ResetControls();
	Players.AllocControl(g_game.start_player);
	g_game.player = Players.GetPlayer(g_game.start_player);
	if (!Char.CharList.empty())
		g_game.character = &Char.CharList[0];
	Char.FreeCharacterPreviews();

	// Default race conditions
	g_game.mirrorred = false;
	g_game.light_id = 0;
	g_game.snow_id = 0;
	g_game.wind_id = 0;
	g_game.game_type = PRACTICING;

	if (Course.currentCourseList && Course.currentCourseList->size() > 0) {
		g_game.course = &(*Course.currentCourseList)[1];
		g_game.theme_id = (*Course.currentCourseList)[0].music_theme;
		State::manager.Run(Loading);
	} else {
		// Fallback to original registration flow if no course is available
		State::manager.Run(Regist);
	}

	State::manager.ResetQuit();
	Course.ResetCourse();

	return g_game.score;
}

void quit_graphics() {
	Winsys.Quit();
}

// Network-based steering function
SteeringAction network_steering(const TVector3d& pos, const TVector3d& vel, float time_step, bool airborne) {
	if (!current_network) {
		return { 0.0f, false, false, false }; // Default action if no network
	}
	
	// Prepare inputs: pos.x, pos.y, pos.z, vel.x, vel.y, vel.z, time_step, airborne
	std::vector<float> inputs = {
		static_cast<float>(pos.x),
		static_cast<float>(pos.y), 
		static_cast<float>(pos.z),
		static_cast<float>(vel.x),
		static_cast<float>(vel.y),
		static_cast<float>(vel.z),
		time_step,
		airborne ? 1.0f : 0.0f
	};
	
	// Get network outputs
	std::vector<float> outputs = current_network->calculate(inputs);
	
	// Interpret outputs: turn_fact, paddling, braking, charging
	float turn_fact = (float)outputs[0] - (float)outputs[1];
	bool paddling = outputs[1] > 0.5f;
	bool braking = outputs[2] > 0.5f;
	bool charging = outputs[3] > 0.5f;
	
	return { turn_fact, paddling, braking, charging };
}

// Evaluation function for NEAT
float evaluate_network(Network network) {
	// Set the current network for steering
	current_network = std::make_unique<Network>(network);
	
	// Set up custom steering to use the network
	g_game.custom_steering = network_steering;
	
	// Run the game simulation
	int score = run_game_once(true, {});
	
	// Return score as fitness (higher is better)
	return static_cast<float>(score);
}

int main(int argc, char **argv) {
	std::cout << "\n----------- Extreme Tux Racer " ETR_VERSION_STRING " ----------------";
	std::cout << "\n----------- (C) 2010-2024 Extreme Tux Racer Team  --------\n\n";

	std::srand(std::time(nullptr));
	InitConfig();
	InitGame();

	// Try to resume from the latest saved NEAT checkpoint if available
	std::string neat_save_dir = "neat_saves";
	auto resume_file = find_latest_neat_file(neat_save_dir);
	std::unique_ptr<Neat_Instance> neat_uptr;
	if (resume_file) {
		std::cout << "Found NEAT checkpoint: " << *resume_file << "\nResuming training..." << std::endl;
		neat_uptr = std::make_unique<Neat_Instance>(*resume_file);
	} else {
		// Initialize NEAT with 8 inputs (pos x,y,z, vel x,y,z, time_step, airborne) and 4 outputs (turn, paddle, brake, charge)
		neat_uptr = std::make_unique<Neat_Instance>(8, 5, 100);
	}
	Neat_Instance &neat = *neat_uptr;
	
	// Configure NEAT parameters
	neat.thread_count = 1;        // Set single thread mode due to global game state
	neat.repetitions = 1;         // Evaluate each network once
	neat.folderpath = neat_save_dir; // Folder to save/load networks
	
	// If resuming, extend the previous target by N generations; else set a fresh target
	const unsigned int extra_generations = 5;
	if (resume_file) {
		neat.generation_target = neat.generation_target + extra_generations;
	} else {
		neat.generation_target = extra_generations;
	}
	
	std::cout << "Starting NEAT training..." << std::endl;
	
	// Run NEAT training
	neat.run_neat(evaluate_network);
	
	// Always save a final checkpoint at the end of training
	neat.save();
	
	std::cout << "Training complete!" << std::endl;
	
	// Get the best network
	auto networks = neat.get_networks_sorted();
	if (!networks.empty()) {
		std::cout << "Best network fitness: " << networks[0].getFitness() << std::endl;
		
		// Set the best network for steering
		current_network = std::make_unique<Network>(networks[0]);
		g_game.custom_steering = network_steering;
		
		// Initialize graphics for non-simulated mode
		init_graphics();
		
		// Run the game once in non-simulated mode to see the results
		std::cout << "Running game with best network..." << std::endl;
		run_game_once(false);
		
		// Clean up graphics
		quit_graphics();
	}

	return 0;
}
