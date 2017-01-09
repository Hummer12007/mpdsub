#include <assert.h>
#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <getopt.h>
#include <setjmp.h>
#include <unistd.h>
#include <wordexp.h>

#include <mpd/client.h>

#include "connect.h"
#include "daemon.h"
#include "formats.h"
#include "ini.h"
#include "util.h"

#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT 6600

#define DEFAULT_FORMAT "%artist%%title||| - %%album| (|)%"
static char *fallback_formats[] = {"%artist%%title||| - %", "%name%", "%file%", NULL};

#define IDLE_MASK MPD_IDLE_PLAYER
#define CONN_RETRY_INTERVAL 3

void read_formats(void);
void print_song(struct mpd_song *, struct mpd_status *status);
void read_config();
void read_params(int, char **);
void handle_error(struct mpd_connection *);
char *expand_path(const char *path);

int usage(void);
int help(void);

void sighandler(int sig);
void sighandler_setup(void);
void setsigmask(bool);

static struct format_list {
	struct format_token *tok;
	struct format_list *next;
} formats;

static struct {
	char *host, *format, *outf, *password, *pidfile, *logfile;
	int port, retry:1, overwrite:1, daemon:1, kill:1;
	FILE *outfile;
} params;

static jmp_buf cb, lb;

int main(int argc, char **argv) {
	int res = 0;
	enum mpd_state state;
	struct mpd_connection *conn;
	struct mpd_status *status;
	struct mpd_song *song;
	read_config();
	read_params(argc, argv);
	read_formats();
	sighandler_setup();
	setjmp(cb);
	do {
		setsigmask(true);
		res = connect_mpd(&conn, params.host, params.port, params.password);
		if (!res && !params.retry) res = -1;
		setsigmask(false);
		if (!res)
			sleep(CONN_RETRY_INTERVAL);
	} while (!res);
	if (res < 0) {
		exit(EXIT_FAILURE);
	}
	switch ((res = setjmp(lb))) {
	case -1:
	case 1:
		log("Terminating.\n");
		mpd_connection_free(conn);
		if (params.pidfile && params.daemon)
			if (unlink(params.pidfile)) {
				log("%s\n", params.pidfile);
				perror("Failed to unlink pidfile");
			}
		return res == 1 ? EXIT_SUCCESS : EXIT_FAILURE;
	case 2:
		log("Idle await interrupted.\n");
		if (!mpd_run_noidle(conn))
			handle_error(conn);
		break;
	}
	while (1) {
		setsigmask(true);
		status = mpd_run_status(conn);
		if (!status)
			handle_error(conn);
		state = mpd_status_get_state(status);
		if (state == MPD_STATE_PLAY || state == MPD_STATE_PAUSE) {
			song = mpd_run_current_song(conn);
			if (!song)
				handle_error(conn);
			print_song(song, status);
		} else {
			print_song(NULL, status);
		}
		setsigmask(false);
		res = mpd_run_idle_mask(conn, IDLE_MASK);
		if (!res)
			handle_error(conn);
	}
}

void print_song(struct mpd_song *song, struct mpd_status *status) {
	int cnt;
	char *c = NULL;
	struct format_list *l = formats.next;
	setsigmask(true);
	if (params.overwrite && params.outf) {
		rewind(params.outfile);
		if (truncate(params.outf, 0))
			log("Could not truncate outfile. Expect unexpected results.");
	}
	cnt = format_song(&c, song, status, formats.tok);
	if (!cnt)
		do
			free(c);
		while (!(cnt = format_song(&c, song, status, l->tok)) && (l = l->next));
	fprintf(params.outfile, "%s\n", c);
	free(c);
	fflush(params.outfile);
	setsigmask(false);
}

void read_formats() {
	char **p = fallback_formats;
	struct format_list *l = &formats;
	l->tok = parse_format(params.format);
	while (*p) {
		l->next = calloc(1, sizeof(struct format_list));
		l = l->next;
		l->tok = parse_format(*p);
		p++;
	}
}

void handle_error(struct mpd_connection *conn) {
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
		if (!mpd_connection_clear_error(conn)) {
			log("Connection error: %s\n",
				mpd_connection_get_error_message(conn));
			if (params.retry) {
				log("Reconnecting!\n");
				longjmp(cb, 1);
			} else {
				longjmp(lb, -1);
			}
		} else {
			log("Protocol level error: %s\n"
				"Incompatible server version. Terminating.\n",
				mpd_connection_get_error_message(conn));
			longjmp(lb, -1);
		}
	}
}

void sighandler(int sig) {
	switch (sig) {
	case SIGTERM:
	case SIGINT:
	case SIGHUP:
		longjmp(lb, 1);
	case SIGUSR1:
		longjmp(cb, 1);
	case SIGUSR2:
		longjmp(lb, 2);
	}
}

void sighandler_setup() {
	static struct sigaction sa;
	sa.sa_handler = sighandler;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);
}

void setsigmask(bool set) {
	static sigset_t mask, orig;
	static int i = 0;
	if (!i) {
		sigemptyset(&mask);
		sigaddset(&mask, SIGTERM);
		sigaddset(&mask, SIGINT);
		sigaddset(&mask, SIGHUP);
		sigaddset(&mask, SIGUSR1);
		sigaddset(&mask, SIGUSR2);
		sigprocmask(SIG_BLOCK, set ? &mask : NULL, &orig);
		i++;
	} else {
		sigprocmask(SIG_SETMASK, set ? &mask : &orig, NULL);
	}
}

static struct option opts[] = {
	{"help",	no_argument,		NULL,	'?'},
	{"host",	required_argument,	NULL,	'h'},
	{"port",	required_argument,	NULL,	'p'},
	{"password",	required_argument,	NULL,	'P'},
	{"format",	required_argument,	NULL,	'f'},
	{"overwrite",	no_argument,		NULL,	'O'},
	{"retry",	no_argument,		NULL,	'r'},
	{"daemonize",	no_argument,		NULL,	'd'},
	{"kill",	no_argument,		NULL,	'k'},
	{"outfile",	optional_argument,	NULL,	'o'},
	{"pidfile",	required_argument,	NULL,	1},
	{"logfile",	required_argument,	NULL,	'l'},
	{NULL,		0,			NULL,	0}
};

static char *descriptions[] = {
	"display this help",
	"mpd instance hostname (or socket file, defaults to localhost)",
	"mpd instance port (defaults to 6600)",
	"password for mpd instance",
	"song format: text with tokens in format\n"
		"\t\t'%tag|prefix|suffix|condprefix%' (each optional)\n"
		"\t\t(condprefix is output iff the previous tag is present)",
	"if specified, overwrite output file with latest song only",
	"keep trying to reconnect to mpd",
	"run in background",
	"kill an already running instance",
	"output file (defaults to stdout)",
	"pidfile location",
	"logfile location",
	NULL
};

static_assert(sizeof(opts)/sizeof(opts[0]) == sizeof(descriptions)/sizeof(descriptions[0]),
		"Option descriptions are not consistent.");

int parse_cb(void *data, const char *section, const char *name, const char *value) {
	(void) data;(void) section;
	if (!strcasecmp(name, "outfile"))
		params.outf = expand_path(value);
	else if (!strcasecmp(name, "pidfile"))
		params.pidfile = expand_path(value);
	else if (!strcasecmp(name, "logfile"))
		params.logfile = expand_path(value);
	else if (!strcasecmp(name, "overwrite")
			&& !strcasecmp(value, "true"))
		params.overwrite = true;
	else if (!strcasecmp(name, "retry")
			&& !strcasecmp(value, "true"))
		params.retry = true;
	else if (!strcasecmp(name, "format"))
		params.format = strdup(value);
	else if (!strcasecmp(section, "strings")) {
		if (!strcasecmp(name, "play"))
			strings.play = strdup(value);
		else if (!strcasecmp(name, "stop"))
			strings.stop = strdup(value);
		else if (!strcasecmp(name, "pause"))
			strings.pause = strdup(value);
		else if (!strcasecmp(name, "unknown"))
			strings.unknown = strdup(value);
	}
	return true;
}

void read_config() {
	static char *configs[] = {"~/.mpdsub.conf", "~/.config/mpdsub.conf"};
	size_t i;
	char *c;
	for (i = 0; i < sizeof(configs) / sizeof(configs[0]); ++i) {
		ini_parse(c = expand_path(configs[i]), parse_cb, NULL);
		free(c);
	}
}

void read_params(int argc, char **argv) {
	int c;
	char *p;
	while (1) {
		if ((c = getopt_long(argc, argv, "h:p:P:f:o:Ordk?", opts, NULL)) < 0)
			break;
		switch (c) {
		case 'h':
			params.host = strdup(optarg);
			break;
		case 'p':
			params.port = atoi(optarg);
			break;
		case 'P':
			params.password = strdup(optarg);
			break;
		case 'f':
			params.format = strdup(optarg);
			break;
		case 'O':
			params.overwrite = true;
			break;
		case 'r':
			params.retry = true;
			break;
		case 'd':
			params.daemon = true;
			break;
		case 'k':
			params.kill = true;
			break;
		case 'o':
			if (!strcmp(optarg, "-"))
				params.outf = NULL;
			else
				params.outf = expand_path(optarg);
			break;
		case 'l':
			params.logfile = expand_path(optarg);
			break;
		case 1:
			params.pidfile = expand_path(optarg);
			break;
		default:
		case '?':
			if (!optopt)
				help();
			else
				usage();
		}
	}
	if (!params.host) {
		params.host = getenv("MPD_HOST");
		if (!params.host)
			params.host = DEFAULT_HOST;
	}
	if (!params.port || params.port < 0 || params.port > 65535) {
		p = getenv("MPD_HOST");
		if (!p || !(params.port = atoi(p)) ||
				params.port < 0 || params.port > 65535)
			params.port = DEFAULT_PORT;
	}
	if (!params.format)
		params.format = DEFAULT_FORMAT;
	if (params.kill)
		kill_instance(params.pidfile, !params.daemon);
	if (params.daemon)
		daemonize(&params.pidfile, params.logfile);
	if (params.outf) {
		params.outfile = fopen(params.outf, "w+");
		if (!params.outfile) {
			perror("Could not open the output file for writing");
			exit(EXIT_FAILURE);
		}
	} else {
		params.outfile = stdout;
	}
}

char *expand_path(const char *path) {
	wordexp_t we;
	char *res;
	if (wordexp(path, &we, 0) || we.we_wordc == 0)
		return NULL;
	res = strdup(we.we_wordv[0]);
	wordfree(&we);
	return res;

}

int usage() {
	fprintf(stderr, "Usage: mpdsub [OPTION...]\n");
	exit(EXIT_FAILURE);
}

static char *strtoupper(const char *str) {
	int i, s = strlen(str);
	char *res = calloc(1, s + 1);
	for (i = 0; i < s; ++i)
		res[i] = toupper(str[i]);
	return res;
}

int help() {
	int i;
	char *f = NULL;
	fprintf(stderr, "Usage: mpdsub [OPTION...]\n");
	fprintf(stderr, "\nThis program connects to a running mpd instance and outputs, in a configurable manner, the currently playing song.");
	fprintf(stderr, "\nExample usage (outfile will contain the latest song played):\n");
	fprintf(stderr, "\tmpdsub -kdOr --pidfile ~/.mpd/mpdsub.pid --outfile ~/.mpd/mpdsub.playing\n");
	fprintf(stderr, "\nOptions:\n");
	for (i = 0; descriptions[i]; i++, f = NULL) {
		fprintf(stderr, "\t");
		if (isprint(opts[i].val))
			fprintf(stderr, "-%c, ", opts[i].val);
		fprintf(stderr, "--%s", opts[i].name);
		if (opts[i].has_arg == required_argument)
			fprintf(stderr, " %s", f = strtoupper(opts[i].name));
		if (opts[i].has_arg == optional_argument)
			fprintf(stderr, " [%s]", f = strtoupper(opts[i].name));
		fprintf(stderr, "\n");
		free(f);
		fprintf(stderr, "\t\t%s\n", descriptions[i]);
	}
	fprintf(stderr, "\n");
	exit(EXIT_SUCCESS);
}
