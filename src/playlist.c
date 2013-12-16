/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * tn.razy@gmail.com
 */

#include "api.h"
#include "playlist.h"
#include "http.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <json/json.h>

extern struct hash **rc;

static void pl_load(struct playlist *pl);

static char *pl_hist2str(const struct hash **history, char *buf);

const struct hash **pl_current(struct playlist *pl)
{
	const struct hash **track = NULL;

	if(pl->position < 0 || pl->position > pl->length)
	{
		pl_load(pl);
		return pl_current(pl);
	}

	if(pl->position >= 0)
	{
		register int idx = 0;

		for(struct tracknode *node = pl->list; node;)
		{
			if(idx++ == pl->position)
			{
				track = (const struct hash **)node->track;
				break;
			}

			node = node->next;
		}

		if(NULL == track)
		{
			pl_load(pl);

			return pl_current(pl);
		}
	}

	return track;
}

void pl_destroy(struct playlist *pl)
{
	if(NULL == pl)
	{
		return;
	}

	/* keep history */

	if(pl->list)
	{
		for(struct tracknode *node = pl->list, *ptr; node;)
		{
			ptr = node;

			if(node->track)
			{
				cleanup(node->track);
			}

			node = ptr->next;
		}

		pl->list = NULL;
	}
}

void pl_history(struct playlist *pl, enum cmd_type type, int sid)
{
	char key[16], value[32];
	unsigned max, idx;
	struct hash ***history;

	snprintf(key, 16, "%d", sid);
	snprintf(value, 32, "%c", (char)type);

	history = &pl->history;

	max = 10;
	idx = length((const struct hash **)*history);

	if(history && idx >= max)
	{
		delete(history, (**history)->key);
	}

	set(history, key, value);
}

static char *pl_hist2str(const struct hash **history, char *buf)
{
	struct hash *item;

	int nitem = length(history);

	while(history && --nitem >= 0)
	{
		item = (struct hash *)history[nitem];

		strcat(buf, "|");
		strcat(buf, item->key);
		strcat(buf, ":");
		strcat(buf, item->value);
	}

	return buf;
}

static void pl_load(struct playlist *pl)
{
	char **resp = NULL, history[128] = { 0 }, *hist = history;
	struct json_object *obj;

	enum cmd_type type = CMD_INITPL;
	int sid = 0;

	if(type < CMD_INITPL || type > CMD_UNKNOW)
	{
		type = CMD_INITPL;
	}
	
	hist = pl_hist2str((const struct hash **)pl->history, history);

	if(*hist)
	{
		type = *(strchr(hist, ':') + 1);
		sid = atoi(hist + 1);
	}

	type = CMD_NEXT == type ? CMD_REFRESH : type;
	
	resp = api_send_request(type, sid, hist);

	if(NULL == resp)
	{
		die("failed to get playlist");
	}

	if(CMD_INITPL != type)
	{
		pl_destroy(pl);
	}

	obj = json_tokener_parse(*resp);

	if(json_object_get_int(json_object_object_get(obj, "r")))
	{
		die("API error");
	}

	array_list *tracks = json_object_get_array(json_object_object_get(obj, "song"));

	pl->position = 0;
	pl->length = tracks->length;

	struct tracknode *ptr = NULL, *last = NULL;

	for(int i = tracks->length - 1; i >= 0; i--)
	{
		struct json_object *o = (struct json_object *)array_list_get_idx(tracks, i);

		ptr = calloc(sizeof *ptr, 1);

		assert(ptr);

		set(&ptr->track, "title", json_object_get_string(json_object_object_get(o, "title")));
		set(&ptr->track, "artist", json_object_get_string(json_object_object_get(o, "artist")));
		set(&ptr->track, "picture", json_object_get_string(json_object_object_get(o, "picture")));
		set(&ptr->track, "albumtitle", json_object_get_string(json_object_object_get(o, "albumtitle")));
		set(&ptr->track, "company", json_object_get_string(json_object_object_get(o, "company")));
		set(&ptr->track, "public_time", json_object_get_string(json_object_object_get(o, "public_time")));
		set(&ptr->track, "album", json_object_get_string(json_object_object_get(o, "album")));
		set(&ptr->track, "like", json_object_get_string(json_object_object_get(o, "like")));
		set(&ptr->track, "url", json_object_get_string(json_object_object_get(o, "url")));
		set(&ptr->track, "sid", json_object_get_string(json_object_object_get(o, "sid")));
		set(&ptr->track, "length", json_object_get_string(json_object_object_get(o, "length")));
		set(&ptr->track, "subtype", json_object_get_string(json_object_object_get(o, "subtype")));

		ptr->next = NULL;

		if(i == tracks->length - 1)
		{
			last = ptr;

			pl->list = last;
		}
		else
		{
			last->next = ptr;
			last = ptr;
		}
	}

	/* make sure playlist is not be null */
	if(NULL == pl->list)
	{
		die("empty playlist");
	}

	free_response(resp);
}

