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

TGameData g_game;

void InitGame(int argc, char **argv) {
	g_game.toolmode = NONE;
	g_game.argument = 0;
	if (argc == 4) {
		if (std::strcmp("--char", argv[1]) == 0)
			g_game.argument = 4;
		Tools.SetParameter(argv[2], argv[3]);
	} else if (argc == 2) {
		if (std::strcmp(argv[1], "9") == 0)
			g_game.argument = 9;
	}

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

int run_game_once(SteeringFunc custom = {}) {
	// Directly set up a default race and go to Loading state (bypass SplashScreen)
	init_ui_snow();

	Course.MakeStandardPolyhedrons();
	Sound.LoadSoundList();
	(void)Char.LoadCharacterList();
	Course.LoadObjectTypes();
	(void)Course.LoadTerrainTypes();

	if (Env.LoadEnvironmentList()) {
		(void)Course.LoadCourseList();
		Score.LoadHighScore();
		Events.LoadEventList();
	}

	if (Players.LoadAvatars()) { // before LoadPlayers !!!
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
		g_game.course = &(*Course.currentCourseList)[0];
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

int main(int argc, char **argv) {
	std::cout << "\n----------- Extreme Tux Racer " ETR_VERSION_STRING " ----------------";
	std::cout << "\n----------- (C) 2010-2024 Extreme Tux Racer Team  --------\n\n";
	std::srand(std::time(nullptr));
	InitConfig();
	InitGame(argc, argv);

	Winsys.Init();
	InitOpenglExtensions();

	// For checking the joystick and the OpgenGL version (the info is written on the console):
	//Winsys.PrintJoystickInfo();
	//PrintGLInfo ();

	// theses resources must or should be loaded before splashscreen starts
	if (!Tex.LoadTextureList()) {
		Winsys.Quit();
		return -1;
	}
	FT.LoadFontlist();
	FT.SetFontFromSettings();
	Music.LoadMusicList();
	Music.SetVolume(param.music_volume);

	g_game.custom_steering = {};
	int score = run_game_once();
	score = run_game_once();

	Winsys.Quit();

	return 0;
}
