/*
 * Return to Castle Wolfenstein Multiplayer GPL Source Code
 * Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.
 * Tracker integration: Wolffiles contributors, 2026. GPL v3 or later.
 */
/**
 * @file sv_tracker.h
 * @brief Public interface for the Wolffiles tracker reporting (RTCW-MP).
 */

#ifndef SV_TRACKER_H
#define SV_TRACKER_H

void Tracker_Init(void);
void Tracker_Send(char *format, ...);

void Tracker_ServerStart(void);
void Tracker_ServerStop(void);

void Tracker_ClientConnect(client_t *cl);
void Tracker_ClientDisconnect(client_t *cl);
void Tracker_ClientName(client_t *cl);
void Tracker_catchBotConnect(int clientNum);

void Tracker_Map(char *mapname);
void Tracker_MapRestart(void);
void Tracker_MapEnd(void);

void Tracker_WriteScores(void);
void Tracker_Frame(int msec);
void Tracker_GamePrint(const char *text);

#endif // SV_TRACKER_H
