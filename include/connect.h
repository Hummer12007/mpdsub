#ifndef CONNECT_H
#define CONNECT_H
#include <mpd/client.h>

int connect_mpd(struct mpd_connection **, char *, int, char *);
#endif //CONNECT_H
