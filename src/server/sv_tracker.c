/*
 * Return to Castle Wolfenstein Multiplayer GPL Source Code
 * Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.
 *
 * Tracker integration ported from ET: Legacy
 * Copyright (C) 2012-2024 ET:Legacy team <mail@etlegacy.com>
 * Copyright (C) 2012 Konrad Moson <mosonkonrad@gmail.com>
 * RTCW-MP port: Wolffiles contributors, 2026
 *
 * GPL v3 or later.
 */
/**
 * @file sv_tracker.c
 * @brief Sends server status to a Wolffiles-compatible tracker.
 *
 * Wire protocol (connectionless UDP OOB):
 *   start | stop | map <name> | maprestart | mapend
 *   connect <slot> <guid> <name> | disconnect <slot> | name <slot> <guid> <name>
 *   p   (heartbeat, every `waittime` seconds)
 *   kill <killer> <victim> <mod>   (from the game's obituary log line)
 *   ws <slot> 1 0 \<ping>\<score>\<team>\0\<name>   (mod-independent scoreboard)
 *
 * Port of iortcw sv_tracker.c. RTCW-MP (original id) adaptation:
 *   NET_StringToAdr() has the 2-arg signature here (no NA_IP family arg).
 */

#include "server.h"
#include "sv_tracker.h"

#define TRACKER_DEFAULT_ENDPOINT ""
#define MAX_TRACKERS 8

static long t;
static int  waittime = 15;
static qboolean maprunning = qfalse;

enum { TR_BOT_NONE, TR_BOT_CONNECT };
static int catchBot    = TR_BOT_NONE;
static int catchBotNum = 0;

static netadr_t trackerAddrs[MAX_TRACKERS];
static int      numTrackerAddrs = 0;
static cvar_t   *sv_tracker_cvar = NULL;

static char *Tracker_getGUID(client_t *cl);

static void Tracker_AddAddress(const char *addr_str)
{
	if (numTrackerAddrs >= MAX_TRACKERS)
	{
		Com_Printf("Tracker: max trackers (%i) reached, ignoring: %s\n", MAX_TRACKERS, addr_str);
		return;
	}

	Com_Printf("Tracker: resolving %s\n", addr_str);
	/* RTCW-MP: NET_StringToAdr takes only (str, *adr) */
	if (!NET_StringToAdr(addr_str, &trackerAddrs[numTrackerAddrs]))
	{
		Com_Printf("Tracker: couldn't resolve address: %s\n", addr_str);
		return;
	}

	Com_Printf("Tracker: %s resolved to %i.%i.%i.%i:%i\n", addr_str,
	           trackerAddrs[numTrackerAddrs].ip[0],
	           trackerAddrs[numTrackerAddrs].ip[1],
	           trackerAddrs[numTrackerAddrs].ip[2],
	           trackerAddrs[numTrackerAddrs].ip[3],
	           BigShort(trackerAddrs[numTrackerAddrs].port));
	numTrackerAddrs++;
}

static void Tracker_ParseAddressList(const char *list)
{
	char buf[MAX_CVAR_VALUE_STRING];
	char *p;
	char *token;

	if (!list || !*list)
	{
		return;
	}

	Q_strncpyz(buf, list, sizeof(buf));
	p = buf;

	while (1)
	{
		token = COM_ParseExt(&p, qfalse);
		if (!token[0])
		{
			break;
		}
		Tracker_AddAddress(token);
	}
}

void Tracker_Send(char *format, ...)
{
	va_list argptr;
	char    msg[MAX_MSGLEN];
	int     i;

	if (numTrackerAddrs == 0)
	{
		return;
	}

	va_start(argptr, format);
	Q_vsnprintf(msg, sizeof(msg), format, argptr);
	va_end(argptr);

	for (i = 0; i < numTrackerAddrs; i++)
	{
		NET_OutOfBandPrint(NS_SERVER, trackerAddrs[i], "%s", msg);
	}
}

void Tracker_Init(void)
{
	sv_tracker_cvar = Cvar_Get("sv_tracker", TRACKER_DEFAULT_ENDPOINT, CVAR_ARCHIVE);
	sv_tracker_cvar->modified = qfalse;

	t               = time(0);
	numTrackerAddrs = 0;

	Tracker_ParseAddressList(sv_tracker_cvar->string);

	if (numTrackerAddrs > 0)
	{
		Com_Printf("Tracker: enabled (%i endpoint(s)).\n", numTrackerAddrs);
	}
}

void Tracker_ServerStart(void)
{
	Tracker_Send("start");
}

void Tracker_ServerStop(void)
{
	Tracker_Send("stop");
}

void Tracker_ClientConnect(client_t *cl)
{
	Tracker_Send("connect %i %s %s", (int)(cl - svs.clients), Tracker_getGUID(cl), cl->name);
}

void Tracker_ClientDisconnect(client_t *cl)
{
	Tracker_Send("disconnect %i", (int)(cl - svs.clients));
}

void Tracker_ClientName(client_t *cl)
{
	if (!*cl->name)
	{
		return;
	}

	Tracker_Send("name %i %s %s", (int)(cl - svs.clients), Tracker_getGUID(cl), Info_ValueForKey(cl->userinfo, "name"));
}

void Tracker_Map(char *mapname)
{
	Tracker_Send("map %s", mapname);
	maprunning = qtrue;
}

void Tracker_MapRestart(void)
{
	Tracker_Send("maprestart");
	maprunning = qtrue;
}

void Tracker_MapEnd(void)
{
	Tracker_Send("mapend");
	maprunning = qfalse;
}

/**
 * @brief Emit mod-independent Score/Ping/Team for every active client.
 *   ws <slot> 1 0 \<ping>\<score>\<team>\0\<name>
 */
void Tracker_WriteScores(void)
{
	int            i;
	client_t      *cl;
	playerState_t *ps;
	int            ping;
	int            score;
	int            team;

	for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++)
	{
		if (cl->state != CS_ACTIVE)
		{
			continue;
		}
		ps    = SV_GameClientNum(i);
		score = ps->persistant[PERS_SCORE];
		team  = ps->persistant[PERS_TEAM];
		ping  = cl->ping < 9999 ? cl->ping : 9999;

		Tracker_Send("ws %i 1 0 \\%i\\%i\\%i\\0\\%s",
		             i, ping, score, team, cl->name);
	}
}

void Tracker_Frame(int msec)
{
	if (sv_tracker_cvar && sv_tracker_cvar->modified)
	{
		Com_Printf("Tracker: sv_tracker changed, reinitializing...\n");
		Tracker_Init();
		return;
	}

	if (catchBot == TR_BOT_CONNECT)
	{
		Tracker_ClientConnect(&svs.clients[catchBotNum]);
		catchBot = TR_BOT_NONE;
	}

	if (!(time(0) - waittime > t))
	{
		return;
	}

	Tracker_Send("p");
	Tracker_WriteScores();

	t = time(0);
}

void Tracker_catchBotConnect(int clientNum)
{
	catchBot    = TR_BOT_CONNECT;
	catchBotNum = clientNum;
}

static char *Tracker_getGUID(client_t *cl)
{
	char *cl_guid = Info_ValueForKey(cl->userinfo, "cl_guid");

	if (cl_guid[0] && Q_stricmp(cl_guid, "unknown") != 0)
	{
		return cl_guid;
	}

	return "unknown";
}

/**
 * @brief Forward obituary kills. Game emits via G_LogPrintf -> G_Printf:
 *   "Kill: <killer> <victim> <mod>: <name> killed <name> by <MOD_x>\n"
 */
void Tracker_GamePrint(const char *text)
{
	int killer, victim, mod;

	if (numTrackerAddrs == 0 || !text)
	{
		return;
	}

	if (Q_strncmp(text, "Kill: ", 6) != 0)
	{
		return;
	}

	if (sscanf(text + 6, "%i %i %i", &killer, &victim, &mod) == 3)
	{
		Tracker_Send("kill %i %i %i", killer, victim, mod);
	}
}
