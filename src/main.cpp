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

#include "rayNEAT/rayNEAT.h"

TGameData g_game;

thread_local std::unique_ptr<Network> current_network = nullptr;

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
	float turn_fact = outputs.size() > 0 ? outputs[0] : 0.0f;
	bool paddling = outputs.size() > 1 ? outputs[1] > 0.5f : false;
	bool braking = outputs.size() > 2 ? outputs[2] > 0.5f : false;
	bool charging = outputs.size() > 3 ? outputs[3] > 0.5f : false;
	
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

	// Initialize NEAT with 8 inputs (pos x,y,z, vel x,y,z, time_step, airborne) and 4 outputs (turn, paddle, brake, charge)
	Neat_Instance neat(8, 4, 100);
	
	// Set single thread mode due to global game state
	neat.thread_count = 1;
	
	// Set training parameters
	neat.generation_target = 15;  // Train for 50 generations
	neat.repetitions = 1;         // Evaluate each network 3 times and average
	neat.folderpath = "neat_saves"; // Folder to save networks
	
	std::cout << "Starting NEAT training..." << std::endl;
	
	// Run NEAT training
	neat.run_neat(evaluate_network);
	
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
