#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>

#include "util.h"
#include "formats.h"

static int get_token(char *format, struct format_token *);
static bool tag_counts(char *tag);
static char *get_tag(struct mpd_song *, struct mpd_status *status, char *);

struct format_strings strings = {"playing", "stopped", "paused", "unknown"};

int format_song(char **c, struct mpd_song *song, struct mpd_status *status, struct format_token *format) {
	size_t s = 128, pos = 0, len;
	int cnt = 0;
	char *buf, *p;
	bool pt = false;
	if (!format)
		return 0;
	*c = calloc(s, 1);
	while (format) {
		if (format->tag) {
			buf = get_tag(song, status, format->contents);
			if (!buf) {
				pt = false;
				goto cont;
			}
			if (!*buf) {
				pt = false;
				free(buf);
				goto cont;
			}
			if (tag_counts(format->contents))
				cnt++;
			len = strlen(buf) +
				(format->prefix ? strlen(format->prefix) : 0) +
				(format->suffix ? strlen(format->suffix) : 0) +
				(pt ? (format->condprefix ?
				       strlen(format->condprefix)  : 0) : 0) +
				1;
			p = calloc(1, len);
			if (pt && format->condprefix) strcat(p, format->condprefix);
			if (format->prefix) strcat(p, format->prefix);
			strcat(p, buf);
			if (format->suffix) strcat(p, format->suffix);
			free(buf);
			buf = p;
			pt = true;
		} else {
			buf = strdup(format->contents);
		}
		len = pos + strlen(buf);
		while (len + 1 > s)
			*c = realloc(*c, s += 128);
		pos = len;
		strcat(*c, buf);
		free(buf);
cont:		format = format->next;
	}
	return cnt;
}

static inline char *print_toggle(bool b) {
	return b ? "on" : "off";
}

static inline char *print_unsigned(unsigned u) {
	char *c = calloc(1, 11);
	sprintf(c, "%u", u);
	return c;
}

// whether to count tag, when formatting (only song-related tags are counted)
// TODO: an option to count every tag
static inline bool tag_counts(char *tag) {
	return mpd_tag_name_iparse(tag) > -1;
}

char *get_tag(struct mpd_song *song, struct mpd_status *status, char *tag) {
	enum mpd_tag_type t = mpd_tag_name_iparse(tag);
	int i;
	if (!status)
		return NULL;
	if (!strcasecmp(tag, "state")) {
		switch (mpd_status_get_state(status)) {
		case MPD_STATE_PLAY:
			return strdup(strings.play);
		case MPD_STATE_PAUSE:
			return strdup(strings.pause);
		case MPD_STATE_STOP:
			return strdup(strings.stop);
		case MPD_STATE_UNKNOWN:
			return strdup(strings.unknown);
		}
	} else if (!strcasecmp(tag, "volume")) {
		i = mpd_status_get_volume(status);
		return i > 0 ? print_unsigned((unsigned) i) : "NONE";
	} else if (!strcasecmp(tag, "queue")) {
		return print_unsigned(mpd_status_get_queue_length(status));
	} else if (!strcasecmp(tag, "repeat")) {
		return print_toggle(mpd_status_get_repeat(status));
	} else if (!strcasecmp(tag, "random")) {
		return print_toggle(mpd_status_get_random(status));
	} else if (!strcasecmp(tag, "single")) {
		return print_toggle(mpd_status_get_single(status));
	} else if (!strcasecmp(tag, "consume")) {
		return print_toggle(mpd_status_get_consume(status));
	}
	if (!song)
		return NULL;
	if (t > -1 && song) {
		return (char *) mpd_song_get_tag(song, t, 0);
	} else if (!strcasecmp(tag, "time") || !strcasecmp(tag, "length")) {
		return print_unsigned(mpd_song_get_duration(song));
	} else if (!strcasecmp(tag, "file") || !strcasecmp(tag, "uri")) {
		return (char *) mpd_song_get_uri(song);
	} else if (!strcasecmp(tag, "position")) {
		return print_unsigned(mpd_song_get_pos(song) + 1);
	}
	return NULL;
}

struct format_token *parse_format(char *format) {
	struct format_token *ret = NULL, **tok;
	int i = 0;
	if (!format)
		return NULL;
	// the following prevents an empty token from appearing at the end
	for (tok = &ret;
		(i = get_token(format += i,
			*tok = malloc(sizeof(struct format_token))));
		tok = &(*tok)->next)
		if (!ret)
			ret = *tok;
	free(*tok);
	*tok = NULL;
	return ret;
}

int get_token(char *format, struct format_token *tok) {
	int cnt = 0, i = 0;
	char *c = format, **p;
	if (!format)
		return cnt;
	tok->tag = *format == '%';
	tok->contents = tok->prefix = tok->suffix = tok->condprefix = NULL;
	tok->next = NULL;
	if (tok->tag) {
		c++, cnt++, format++;
		if (*format == '%' || !*format) {
			tok->tag = false, tok->contents = "%";
			return cnt;
		}
	}
	while (*format) {
		cnt++;
		switch (*format) {
		case '%':
			if (!tok->tag)
				cnt--;
			goto br;
		case '|':
			if (!tok->tag || i == 3)
				break;
			p = i == 0 ? &tok->contents :
				i == 1 ? &tok->prefix :
				i == 2 ? &tok->suffix :
				&tok->condprefix;
			*p = calloc(1, format - c + 1);
			strncpy(*p, c, format - c);
			c = format + 1;
			i++;
			break;
		}
		format++;
	}
br:	p = !tok->tag ? &tok->contents :
		i == 0 ? &tok->contents :
		i == 1 ? &tok->prefix :
		i == 2 ? &tok->suffix :
		&tok->condprefix;
	*p = calloc(format - c + 1, 1);
	strncpy(*p, c, format - c);
	return cnt;
}
