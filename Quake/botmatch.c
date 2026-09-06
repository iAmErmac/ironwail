// Copyright (C) 2026 Ironwail contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "quakedef.h"
#include "botmatch.h"
#include "q_ctype.h"
#include "xr_input.h"

extern qboolean cmd_wait;

static cvar_t botmatch_delay_min = {"botmatch_delay_min", "5", CVAR_ARCHIVE};
static cvar_t botmatch_delay_max = {"botmatch_delay_max", "10", CVAR_ARCHIVE};
static cvar_t botmatch_botcount = {"botmatch_botcount", "0", CVAR_NONE};
static cvar_t botmatch_autospawn_api = {"botmatch_autospawn_api", "0", CVAR_NONE};

static qboolean botmatch_requested;
static qboolean makebots_requested;
static qboolean botmatch_warned;
static qboolean botmatch_command_warned;
static qboolean botmatch_full_reported;
static int botmatch_requested_bots;
static double botmatch_next_add;
static char *makebots_script;
static char *makebots_cursor;
static qboolean makebots_script_done;

static double BotMatch_RandomDelay (void)
{
	double min_delay = botmatch_delay_min.value;
	double max_delay = botmatch_delay_max.value;

	if (min_delay < 0.0)
		min_delay = 0.0;
	if (max_delay < 0.0)
		max_delay = 0.0;
	if (max_delay < min_delay)
	{
		double temp = min_delay;
		min_delay = max_delay;
		max_delay = temp;
	}

	if (max_delay == min_delay)
		return min_delay;

	return min_delay + (double) rand () / ((double) RAND_MAX + 1.0) * (max_delay - min_delay);
}

static int BotMatch_HumanCount (void)
{
	int i, count = 0;

	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active)
			count++;
	}

	return count;
}

static qboolean BotMatch_AddCommandExists (void)
{
	return Cmd_AliasExists ("botmatch_addbot") || Cmd_Exists ("botmatch_addbot");
}

static void BotMatch_FreeMakebotsScript (void)
{
	if (makebots_script)
		free (makebots_script);
	makebots_script = NULL;
	makebots_cursor = NULL;
	makebots_script_done = true;
}

static void BotMatch_LoadMakebotsScript (void)
{
	BotMatch_FreeMakebotsScript ();
	if (!makebots_requested)
		return;
	makebots_script = (char *) COM_LoadMallocFile ("makebots.cfg", NULL);
	if (!makebots_script)
		return;
	makebots_cursor = makebots_script;
	makebots_script_done = false;
}

static char *BotMatch_TrimMakebotsLine (char *line)
{
	char *end;

	while (*line && q_isspace (*line))
		line++;
	end = line + strlen (line);
	while (end > line && q_isspace (end[-1]))
		end--;
	*end = '\0';
	return line;
}

// The stock makebots.cfg separates each skill/impulse pair with a wait.
static qboolean BotMatch_NextMakebotsBlock (char *out, size_t outsize)
{
	size_t length = 0;
	qboolean have_command = false;

	if (!makebots_cursor || !*makebots_cursor)
		return false;

	while (*makebots_cursor)
	{
		char *line = makebots_cursor;
		char *next = line;
		char *comment;

		while (*next && *next != '\n')
			next++;
		if (*next)
		{
			*next = '\0';
			makebots_cursor = next + 1;
		}
		else
			makebots_cursor = next;

		comment = strstr (line, "//");
		if (comment)
			*comment = '\0';
		line = BotMatch_TrimMakebotsLine (line);
		if (!*line)
			continue;

		if (!q_strcasecmp (line, "wait"))
		{
			if (have_command)
				return true;
			continue;
		}

		if (length + strlen (line) + 2 >= outsize)
		{
			makebots_cursor = NULL;
			return false;
		}
		memcpy (out + length, line, strlen (line));
		length += strlen (line);
		out[length++] = '\n';
		out[length] = '\0';
		have_command = true;
	}

	return have_command;
}

static void BotMatch_RunMapBeginHook (void)
{
	if (!Cmd_AliasExists ("botmatch_mapbegin") && !Cmd_Exists ("botmatch_mapbegin"))
		return;

	Cbuf_AddText ("botmatch_mapbegin\n");
	Cbuf_Execute ();
}

static void BotMatch_f (void)
{
	int target;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("botmatch <bot count> : enable bot population, or use 0 to disable\n");
		return;
	}

	target = Q_atoi (Cmd_Argv (1));
	if (target <= 0)
	{
		botmatch_requested = false;
		makebots_requested = false;
		botmatch_requested_bots = 0;
		botmatch_next_add = 0.0;
		BotMatch_FreeMakebotsScript ();
		Con_Printf ("botmatch disabled\n");
		return;
	}

	if (target > MAX_SCOREBOARD)
		target = MAX_SCOREBOARD;

	botmatch_requested = true;
	makebots_requested = false;
	botmatch_requested_bots = target;
	botmatch_next_add = 0.0;
	BotMatch_FreeMakebotsScript ();
	botmatch_warned = false;
	botmatch_command_warned = false;
	botmatch_full_reported = false;
	Con_Printf ("botmatch requested: up to %d bots per map\n", target);
}

static void BotMatch_Makebots_f (void)
{
	if (Cmd_Argc () != 1)
	{
		Con_Printf ("makebots : execute the active mod's makebots.cfg with botmatch delays\n");
		return;
	}

	botmatch_requested = false;
	makebots_requested = true;
	botmatch_requested_bots = 0;
	botmatch_next_add = 0.0;
	BotMatch_LoadMakebotsScript ();
}

void BotMatch_Init (void)
{
	Cvar_RegisterVariable (&botmatch_delay_min);
	Cvar_RegisterVariable (&botmatch_delay_max);
	Cvar_RegisterVariable (&botmatch_botcount);
	Cvar_RegisterVariable (&botmatch_autospawn_api);
	Cmd_AddCommand ("botmatch", BotMatch_f);
	Cmd_AddCommand ("makebots", BotMatch_Makebots_f);
}

void BotMatch_MapBegin (void)
{
	Cvar_SetQuick (&botmatch_botcount, "0");
	Cvar_SetQuick (&botmatch_autospawn_api, "0");
	botmatch_next_add = 0.0;
	botmatch_warned = false;
	botmatch_command_warned = false;
	botmatch_full_reported = false;
	BotMatch_LoadMakebotsScript ();
	BotMatch_RunMapBeginHook ();
}

void BotMatch_Frame (void)
{
	int human_count, target, bot_count;

	if (makebots_requested)
	{
		char command[4096];

		if (makebots_script_done)
			return;
		if (!sv.active || svs.maxclients < 2 || cls.state != ca_connected || cls.signon != SIGNONS)
			return;
		if (cl.intermission || XR_Input_TeamSelectionActive ())
			return;
		if (cmd_wait || svs.changelevel_issued)
			return;
		if (BotMatch_HumanCount () >= svs.maxclients)
		{
			makebots_script_done = true;
			return;
		}
		if (botmatch_next_add == 0.0)
		{
			botmatch_next_add = realtime + BotMatch_RandomDelay ();
			return;
		}
		if (realtime < botmatch_next_add)
			return;
		if (!BotMatch_NextMakebotsBlock (command, sizeof (command)))
		{
			makebots_script_done = true;
			botmatch_next_add = 0.0;
			return;
		}
		Cbuf_AddText (command);
		Cbuf_Execute ();
		botmatch_next_add = realtime + BotMatch_RandomDelay ();
		return;
	}

	if (!botmatch_requested || botmatch_requested_bots <= 0)
		return;
	if (!sv.active || svs.maxclients < 2 || cls.state != ca_connected || cls.signon != SIGNONS)
		return;
	if (cl.intermission)
		return;
	// Let the local player choose a CTF team before another bot impulse is sent.
	if (XR_Input_TeamSelectionActive ())
		return;

	if (botmatch_autospawn_api.value < 1.0f)
	{
		if (!botmatch_warned)
		{
			Con_Printf ("botmatch: loaded game does not advertise botmatch autospawn API v1; disabled for this map\n");
			botmatch_warned = true;
		}
		return;
	}

	if (!BotMatch_AddCommandExists ())
	{
		if (!botmatch_command_warned)
		{
			Con_Printf ("botmatch: botmatch_addbot is not defined; disabled for this map\n");
			botmatch_command_warned = true;
		}
		return;
	}

	human_count = BotMatch_HumanCount ();
	target = svs.maxclients - human_count;
	if (target > botmatch_requested_bots)
		target = botmatch_requested_bots;
	if (target <= 0)
		return;

	bot_count = (int) (botmatch_botcount.value + 0.5f);
	if (bot_count >= target)
	{
		if (!botmatch_full_reported)
		{
			Con_Printf ("botmatch: %d bots active\n", bot_count);
			botmatch_full_reported = true;
		}
		botmatch_next_add = 0.0;
		return;
	}

	botmatch_full_reported = false;
	if (cmd_wait || svs.changelevel_issued)
		return;

	if (botmatch_next_add == 0.0)
	{
		botmatch_next_add = realtime + BotMatch_RandomDelay ();
		return;
	}
	if (realtime < botmatch_next_add)
		return;

	Cbuf_AddText ("botmatch_addbot\n");
	Cbuf_Execute ();
	botmatch_next_add = realtime + BotMatch_RandomDelay ();
}
