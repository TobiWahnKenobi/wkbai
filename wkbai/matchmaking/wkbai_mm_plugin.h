/* wkbai matchmaking plugin interface, version 1
 *
 * This header alone is under the MIT licence (the rest of wkbai is GPL-3), so that a plugin
 * may be built with a closed SDK (Epic Online Services, a console's network library, ...):
 *
 *   Copyright (c) 2026 the wkbai contributors.
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this file,
 *   to deal in it without restriction, including without limitation the rights to use, copy,
 *   modify, merge, publish, distribute, sublicense, and/or sell copies of it, and to permit
 *   persons to whom it is furnished to do so. THE FILE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
 *   ANY KIND.
 *
 * A plugin is a library (.dll, .so, .dylib) in the "plugins" folder beside wkbai's exe or in
 * the user folder. It exports wkbai_mm_plugin_entry, which returns its table of functions.
 * wkbai calls them on its main thread only, once a frame (tick) and when the player acts; a
 * plugin that needs threads keeps them to itself.
 *
 * Text is UTF-8. Listings, filters and settings go as JSON text:
 *   listing  { "name", "host", "map", "players", "places", "spectators", "maxSpectators",
 *              "allowSpectators", "password", "running", "version", "modPolicy",
 *              "mods" (the host's mod manifest), "addresses" (["a.b.c.d:port", ...]) }
 *            and in a plugin's list besides: "id" (the plugin's id of it), "ping" (ms)
 *   filter   { "hideFull", "hidePassword", "sameVersionOnly", "text", "version" }
 * The game's packets are at most a few hundred bytes; they must arrive whole, in order and
 * reliably.
 */

#ifndef WKBAI_MM_PLUGIN_H
#define WKBAI_MM_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define WKBAI_MM_EXPORT __declspec(dllexport)
#else
#define WKBAI_MM_EXPORT __attribute__((visibility("default")))
#endif

#define WKBAI_MM_API_VERSION 1

/* What wkbai gives a plugin */
typedef struct wkbai_mm_host {
	uint32_t api_version;
	void (*log)(const char* line);
	const char* settings_json;  /* wkconfig.json "matchmaking": { "<plugin id>": {...} } */
	const char* user_path;      /* the player's wkbai folder; a plugin keeps its files in a folder there */
	const char* plugin_path;    /* the folder the library is in */
	const char* game_version;   /* only listings of this version can be joined */
	const char* player_name;
	const char* bind_address;   /* wkconfig.json "hostBind" ("": any): tests keep to this machine */
} wkbai_mm_host;

typedef struct wkbai_mm_service wkbai_mm_service; /* the plugin's own */

enum {
	WKBAI_MM_OFF = 0,        /* not set up (the text says what is missing) */
	WKBAI_MM_SIGNING_IN = 1,
	WKBAI_MM_READY = 2,
	WKBAI_MM_FAILED = 3,
};

enum {
	WKBAI_MM_EVENT_NONE = 0,
	WKBAI_MM_EVENT_CONNECTED = 1,    /* host: a guest came; guest: the connection is there */
	WKBAI_MM_EVENT_DISCONNECTED = 2, /* gone, or the connection couldn't be made */
};

typedef struct wkbai_mm_plugin {
	uint32_t api_version;     /* WKBAI_MM_API_VERSION */
	const char* id;           /* short, unique: "eos" */
	const char* title;        /* above its games in the list: "Epic Online Services" */
	const char* icon;         /* an image file in plugin_path, or "" */

	wkbai_mm_service* (*create)(const wkbai_mm_host* host);
	void (*destroy)(wkbai_mm_service* s);
	void (*tick)(wkbai_mm_service* s);
	/* WKBAI_MM_*, and a text for the player (valid until the next call) */
	int (*state)(wkbai_mm_service* s, const char** text);
	void (*sign_in)(wkbai_mm_service* s);

	/* Host: the game offered (again: its listing changed), or taken off */
	void (*publish)(wkbai_mm_service* s, const char* listing_json);
	void (*withdraw)(wkbai_mm_service* s);

	/* Guest: look for games; the games found as a JSON array (valid until the next call) */
	void (*refresh)(wkbai_mm_service* s, const char* filter_json);
	const char* (*listings)(wkbai_mm_service* s);
	/* Connect to a listing (its "id"). Either sets *enet_address ("host:port", valid until the
	 * next call) and returns 0: wkbai connects there with ENet; or returns a connection id > 0
	 * carried by the plugin (its CONNECTED event follows); or returns -1 (failed). */
	int64_t (*connect)(wkbai_mm_service* s, const char* listing_id, const char** enet_address);

	/* The plugin's own transport, both sides, by connection id. Events: guests that came and
	 * went (host), its connection (guest); NONE when nothing happened. */
	int (*poll_event)(wkbai_mm_service* s, int64_t* connection);
	int (*send)(wkbai_mm_service* s, int64_t connection, const void* data, uint32_t size);
	/* the next packet: its size (when larger than capacity it is left there), 0 when none */
	uint32_t (*receive)(wkbai_mm_service* s, int64_t connection, void* buffer, uint32_t capacity);
	void (*close)(wkbai_mm_service* s, int64_t connection, int after_sending);
} wkbai_mm_plugin;

/* The one export: the plugin's table for this interface version, or null if it can't serve it */
typedef const wkbai_mm_plugin* (*wkbai_mm_plugin_entry_fn)(uint32_t host_api_version);
#define WKBAI_MM_PLUGIN_ENTRY "wkbai_mm_plugin_entry"

#ifdef __cplusplus
}
#endif

#endif
