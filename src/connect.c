#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mpd/client.h>

#include "util.h"

bool supported_protocol(struct mpd_connection *);
bool authorized(struct mpd_connection *);
int connect_mpd(struct mpd_connection **, char *, int, char *);

bool authorized(struct mpd_connection *conn) {
	int perms = 0;
	struct mpd_pair *pair;
	mpd_send_allowed_commands(conn);
	while ((pair = mpd_recv_pair_named(conn, "command"))) {
		if (!strcmp(pair->value, "status"))
			perms |= 1 << 0;
		if (!strcmp(pair->value, "currentsong"))
			perms |= 1 << 1;
		if (!strcmp(pair->value, "idle"))
			perms |= 1 << 2;
		mpd_return_pair(conn, pair);
	}
	return perms == 7;
}

bool supported_protocol(struct mpd_connection *conn) {
	int cmds = 0, dis = 0;
	struct mpd_pair *pair;
	mpd_send_allowed_commands(conn);
recv:
	while ((pair = mpd_recv_pair_named(conn, "command"))) {
		if (!strcmp(pair->value, "status"))
			cmds |= 1 << 0;
		if (!strcmp(pair->value, "currentsong"))
			cmds |= 1 << 1;
		if (!strcmp(pair->value, "idle"))
			cmds |= 1 << 2;
		mpd_return_pair(conn, pair);
	}
	if (!dis && cmds != 7) {
		dis = 1;
		mpd_send_disallowed_commands(conn);
		goto recv;
	}
	return cmds == 7;
}

int connect_mpd(struct mpd_connection **c, char *host, int port, char *password) {
	struct mpd_connection *conn;
	conn = mpd_connection_new(host, port, 0);
	if (!conn)
		return -1;
	if (mpd_connection_get_error(conn) == MPD_ERROR_SUCCESS) {
		*c = conn;
		log("Connected to mpd instance at %s\n", host);
		if (!supported_protocol(conn)) {
			log("mpd instance does not support all required features.\n");
			return -1;
		}
		if (!authorized(conn)) {
			if (!password) {
				log("Password required.\n");
				return -1;
			}
			if (!mpd_run_password(conn, password)) {
				log("Invalid password provided.\n");
				return -1;
			}
			if (!authorized(conn)) {
				log("Insufficient permissions (read required).\n");
				return -1;
			}
		}
	} else {
		log("Could not connect to mpd instance: %s\n",
			mpd_connection_get_error_message(conn));
		return 0;
	}
	return 1;
}
