#ifndef DAEMON_H
#define DAEMON_H
#include <stdbool.h>
void kill_instance(char *pidfile, bool fatal);
void daemonize(char **pidfile, char *logfile);
#endif
