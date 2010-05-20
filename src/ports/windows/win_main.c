/**
 * @file win_main.c
 * @brief Windows system functions
 */

/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "../../common/common.h"
#include "../../common/msg.h"
#include "win_local.h"
#include <fcntl.h>
#include <float.h>
#include <direct.h>
#include <io.h>

qboolean s_win95, s_win2k, s_winxp, s_vista;

#define MAX_NUM_ARGVS 128
static int argc;
static const char *argv[MAX_NUM_ARGVS];

void Sys_Init (void)
{
	OSVERSIONINFO vinfo;
#if 0
	MEMORYSTATUS mem;
#endif

	sys_affinity = Cvar_Get("sys_affinity", "3", CVAR_ARCHIVE, "Which core to use - 1 = only first, 2 = only second, 3 = both");
	sys_priority = Cvar_Get("sys_priority", "0", CVAR_ARCHIVE, "Process priority - 0 = normal, 1 = high, 2 = realtime");

	timeBeginPeriod(1);

	vinfo.dwOSVersionInfoSize = sizeof(vinfo);

	if (!GetVersionEx(&vinfo))
		Sys_Error("Couldn't get OS info");

	if (vinfo.dwMajorVersion < 4) /* at least win nt 4 */
		Sys_Error("UFO: AI requires windows version 4 or greater");
	if (vinfo.dwPlatformId == VER_PLATFORM_WIN32s) /* win3.x with win32 extensions */
		Sys_Error("UFO: AI doesn't run on Win32s");
	else if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) /* win95, 98, me */
		s_win95 = qtrue;
	else if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT) { /* win nt, xp */
		if (vinfo.dwMajorVersion == 5 && vinfo.dwMinorVersion == 0)
			s_win2k = qtrue;
		else if (vinfo.dwMajorVersion == 5)
			s_winxp = qtrue;
		else if (vinfo.dwMajorVersion == 6)
			s_vista = qtrue;
	}

	if (s_win95)
		sys_os = Cvar_Get("sys_os", "win95", CVAR_SERVERINFO, NULL);
	else if (s_win2k)
		sys_os = Cvar_Get("sys_os", "win2K", CVAR_SERVERINFO, NULL);
	else if (s_winxp)
		sys_os = Cvar_Get("sys_os", "winXP", CVAR_SERVERINFO, NULL);
	else if (s_vista)
		sys_os = Cvar_Get("sys_os", "winVista", CVAR_SERVERINFO, NULL);
	else
		sys_os = Cvar_Get("sys_os", "win", CVAR_SERVERINFO, NULL);

#if 0
	GlobalMemoryStatus(&mem);
	Com_Printf("Memory: %u MB\n", mem.dwTotalPhys >> 20);
#endif
}

/*
========================================================================
GAME DLL
========================================================================
*/

static HINSTANCE game_library;

void Sys_UnloadGame (void)
{
	if (!FreeLibrary(game_library))
		Com_Error(ERR_FATAL, "FreeLibrary failed for game library");
	game_library = NULL;
}

/**
 * @brief Loads the game dll
 */
game_export_t *Sys_GetGameAPI (game_import_t *parms)
{
	void *(*GetGameAPI) (void *);
	char name[MAX_OSPATH];
	const char *path;

	if (game_library)
		Com_Error(ERR_FATAL, "Sys_GetGameAPI without Sys_UnloadingGame");

#ifndef HARD_LINKED_GAME
	/* run through the search paths */
	path = NULL;
	while (1) {
		path = FS_NextPath(path);
		if (!path)
			break;		/* couldn't find one anywhere */
		Com_sprintf(name, sizeof(name), "%s/game.dll", path);
		game_library = LoadLibrary(name);
		if (game_library) {
			Com_DPrintf(DEBUG_SYSTEM, "LoadLibrary (%s)\n", name);
			break;
		} else {
			Com_DPrintf(DEBUG_SYSTEM, "LoadLibrary (%s) failed\n", name);
		}
	}

	if (!game_library) {
		Com_Printf("Could not find any valid game lib\n");
		return NULL;
	}

	GetGameAPI = (void *)GetProcAddress(game_library, "GetGameAPI");
	if (!GetGameAPI) {
		Sys_UnloadGame();
		Com_Printf("Could not load game lib '%s'\n", name);
		return NULL;
	}
#endif

	return GetGameAPI(parms);
}

/**
 * @brief Switch to one processor usage for windows system with more than
 * one processor
 */
void Sys_SetAffinityAndPriority (void)
{
	SYSTEM_INFO sysInfo;
	/* 1 - use core #0
	 * 2 - use core #1
	 * 3 - use cores #0 and #1 */
	DWORD_PTR procAffinity = 0;
	HANDLE proc = GetCurrentProcess();

	if (sys_priority->modified) {
		if (sys_priority->integer < 0)
			Cvar_SetValue("sys_priority", 0);
		else if (sys_priority->integer > 2)
			Cvar_SetValue("sys_priority", 2);

		sys_priority->modified = qfalse;

		switch (sys_priority->integer) {
		case 0:
			SetPriorityClass(proc, NORMAL_PRIORITY_CLASS);
			Com_Printf("Priority changed to NORMAL\n");
			break;
		case 1:
			SetPriorityClass(proc, HIGH_PRIORITY_CLASS);
			Com_Printf("Priority changed to HIGH\n");
			break;
		default:
			SetPriorityClass(proc, REALTIME_PRIORITY_CLASS);
			Com_Printf("Priority changed to REALTIME\n");
			break;
		}
	}

	if (sys_affinity->modified) {
		GetSystemInfo(&sysInfo);
		Com_Printf("Found %i processors\n", (int)sysInfo.dwNumberOfProcessors);
		sys_affinity->modified = qfalse;
		if (sysInfo.dwNumberOfProcessors == 2) {
			switch (sys_affinity->integer) {
			case 1:
				Com_Printf("Only use the first core\n");
				procAffinity = 1;
				break;
			case 2:
				Com_Printf("Only use the second core\n");
				procAffinity = 2;
				break;
			case 3:
				Com_Printf("Use both cores\n");
				Cvar_SetValue("r_threads", 1);
				procAffinity = 3;
				break;
			}
		} else if (sysInfo.dwNumberOfProcessors > 2) {
			switch (sys_affinity->integer) {
			case 1:
				Com_Printf("Use all cores\n");
				procAffinity = (1 << sysInfo.dwNumberOfProcessors) - 1;
				break;
			case 2:
				Com_Printf("Only use two cores\n");
				procAffinity = 3;
				break;
			case 3:
				Com_Printf("Only use one core\n");
				procAffinity = 1;
				break;
			}
		} else {
			Com_Printf("...only use one processor\n");
		}
		SetProcessAffinityMask(proc, procAffinity);
	}

	CloseHandle(proc);
}


const char *Sys_SetLocale (const char *localeID)
{
	Sys_Setenv("LANG", localeID);
	Sys_Setenv("LANGUAGE", localeID);

	return localeID;
}

const char *Sys_GetLocale (void)
{
	if (getenv("LANGUAGE"))
		return getenv("LANGUAGE");
	else
		/* Setting to en will always work in every windows. */
		return "en";
}

/**
 * @sa Sys_FreeLibrary
 * @sa Sys_GetProcAddress
 */
void *Sys_LoadLibrary (const char *name, int flags)
{
	char path[MAX_OSPATH];
	HMODULE lib;

	/* first try cpu string */
	Com_sprintf(path, sizeof(path), "%s_"CPUSTRING"."SHARED_EXT, name);
	lib = LoadLibrary(path);
	if (lib)
		return lib;

	/* now the general lib */
	Com_sprintf(path, sizeof(path), "%s."SHARED_EXT, name);
	lib = LoadLibrary(path);
	if (lib)
		return lib;

#ifdef PKGLIBDIR
	Com_sprintf(path, sizeof(path), PKGLIBDIR"%s."SHARED_EXT, name);
	lib = LoadLibrary(path);
	if (lib)
		return lib;
#endif

	Com_Printf("Could not load %s\n", name);
	return NULL;
}

/**
 * @sa Sys_LoadLibrary
 */
void Sys_FreeLibrary (void *libHandle)
{
	if (!libHandle)
		Com_Error(ERR_DROP, "Sys_FreeLibrary: No valid handle given");
	if (!FreeLibrary((HMODULE)libHandle))
		Com_Error(ERR_DROP, "Sys_FreeLibrary: dlclose() failed");
}

/**
 * @sa Sys_LoadLibrary
 */
void *Sys_GetProcAddress (void *libHandle, const char *procName)
{
	if (!libHandle)
		Com_Error(ERR_DROP, "Sys_GetProcAddress: No valid libHandle given");
	return GetProcAddress((HMODULE)libHandle, procName);
}

static void ParseCommandLine (LPSTR lpCmdLine)
{
	argc = 1;
	argv[0] = "exe";

	while (*lpCmdLine && (argc < MAX_NUM_ARGVS)) {
		while (*lpCmdLine && ((*lpCmdLine <= 32) || (*lpCmdLine > 126)))
			lpCmdLine++;

		if (*lpCmdLine) {
			argv[argc] = lpCmdLine;
			argc++;

			while (*lpCmdLine && ((*lpCmdLine > 32) && (*lpCmdLine <= 126)))
				lpCmdLine++;

			if (*lpCmdLine) {
				*lpCmdLine = 0;
				lpCmdLine++;
			}
		}
	}
}

static void FixWorkingDirectory (void)
{
	char *p;
	char curDir[MAX_PATH];

	GetModuleFileName(NULL, curDir, sizeof(curDir)-1);

	p = strrchr(curDir, '\\');
	p[0] = 0;

	if (strlen(curDir) > (MAX_OSPATH - MAX_QPATH))
		Sys_Error("Current path is too long. Please move your installation to a shorter path.");

	SetCurrentDirectory(curDir);
}

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	/* previous instances do not exist in Win32 */
	if (hPrevInstance)
		return 0;

	global_hInstance = hInstance;

	ParseCommandLine(lpCmdLine);

	Sys_ConsoleInit();

	/* always change to the current working dir */
	FixWorkingDirectory();

	Qcommon_Init(argc, argv);

	/* main window message loop */
	while (1)
		Qcommon_Frame();

	/* never gets here */
	return FALSE;
}
