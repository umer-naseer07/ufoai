/**
 * @file cl_tutorials.c
 */

/*
Copyright (C) 2002-2007 UFO: Alien Invasion team.

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

#include "client.h"
#include "cl_tutorials.h"

typedef struct tutorial_s {
	char name[MAX_VAR];
	char *sequence;
} tutorial_t;

#define MAX_TUTORIALS 16
static tutorial_t tutorials[MAX_TUTORIALS];
static int numTutorials;

#define MAX_TUTORIALLIST 512
static char tutorialList[MAX_TUTORIALLIST];

static void TUT_GetTutorials_f (void)
{
	int i;

	mn.menuText[TEXT_LIST] = tutorialList;
	tutorialList[0] = 0;
	for (i = 0; i < numTutorials; i++) {
		const tutorial_t *t = &tutorials[i];
		Q_strcat(tutorialList, va("%s\n", _(t->name)), sizeof(tutorialList));
	}
}

static void TUT_List_f (void)
{
	int i;

	Com_Printf("Tutorials: %i\n", numTutorials);
	for (i = 0; i < numTutorials; i++) {
		Com_Printf("tutorial: %s\n", tutorials[i].name);
		Com_Printf("..sequence: %s\n", tutorials[i].sequence);
	}
}

/**
 * @brief click function for text tutoriallist in menu_tutorials.ufo
 */
static void TUT_ListClick_f (void)
{
	int num;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}

	num = atoi(Cmd_Argv(1));
	if (num < 0 || num >= numTutorials)
		return;

	Cmd_ExecuteString(va("seq_start %s", tutorials[num].sequence));
}

void TUT_InitStartup (void)
{
	/* tutorial stuff */
	Cmd_AddCommand("listtutorials", TUT_List_f, "Show all tutorials");
	Cmd_AddCommand("gettutorials", TUT_GetTutorials_f, NULL);
	Cmd_AddCommand("tutoriallist_click", TUT_ListClick_f, NULL);
}


static const value_t tutValues[] = {
	{"name", V_TRANSLATION_MANUAL_STRING, offsetof(tutorial_t, name), 0},
	{"sequence", V_CLIENT_HUNK_STRING, offsetof(tutorial_t, sequence), 0},
	{NULL, 0, 0, 0}
};

/**
 * @sa CL_ParseClientData
 */
void TUT_ParseTutorials (const char *name, const char **text)
{
	tutorial_t *t;
	const char *errhead = "TUT_ParseTutorials: unexpected end of file (tutorial ";
	const char *token;
	const value_t *v;

	/* get name list body body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("TUT_ParseTutorials: tutorial \"%s\" without body ignored\n", name);
		return;
	}

	/* parse tutorials */
	if (numTutorials >= MAX_TUTORIALS) {
		Com_Printf("Too many tutorials, '%s' ignored.\n", name);
		numTutorials = MAX_TUTORIALS;
		return;
	}

	t = &tutorials[numTutorials++];
	memset(t, 0, sizeof(*t));
	do {
		/* get the name type */
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;
		for (v = tutValues; v->string; v++)
			if (!Q_strncmp(token, v->string, sizeof(v->string))) {
				/* found a definition */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				switch (v->type) {
				case V_CLIENT_HUNK_STRING:
					Mem_PoolStrDupTo(token, (char**) ((char*)t + (int)v->ofs), cl_menuSysPool, CL_TAG_MENU);
					break;
				default:
					Com_ParseValue(t, token, v->type, v->ofs, v->size);
				}
				break;
			}
		if (!v->string)
			Com_Printf("TUT_ParseTutorials: unknown token \"%s\" ignored (tutorial %s)\n", token, name);
	} while (*text);
}

