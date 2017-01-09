#ifndef FORMATS_H
#define FORMATS_H
#include <mpd/client.h>

struct format_token {
	bool tag;
	char *contents, *prefix, *suffix, *condprefix;
	struct format_token *next;
};

int format_song(char **, struct mpd_song *, struct mpd_status *status, struct format_token *);

struct format_token *parse_format(char *format);

extern struct format_strings {
	char *play;
	char *stop;
	char *pause;
	char *unknown;
} strings;
#endif //FORMATS_H
