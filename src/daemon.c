#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "daemon.h"
#include "util.h"

void kill_instance(char *pidfile, bool fatal) {
	int pid = 0;
	struct timespec sl = {0, 50000000};
	FILE *f;
	if (!pidfile) {
		log("Pidfile not specified!\n");
		goto fail;
	}
	if (!(f = fopen(pidfile, "r"))) {
		perror("Could not open pidfile for reading");
		goto fail;
	}
	if (fscanf(f, "%d", &pid) >= 0)
		fclose(f);
	if (pid <= 0) {
		log("Invalid pid specified.\n");
		if (fatal) goto fail;
		else goto pidf_cleanup;
	}
	if (kill(pid, SIGTERM)) {
		printf("%d\n", pid);
		perror("Could not kill specified process");
		if (fatal) goto fail;
		else goto pidf_cleanup;
	}
	if (!fatal) {
		while (kill(pid, 0) != -1 && errno != ESRCH)
			nanosleep(&sl, NULL);
	}
	log("Killed instance running at pid %d successfully.\n", pid);
	if (fatal)
		exit(EXIT_SUCCESS);
	return;
fail:
	if (fatal)
		exit(EXIT_FAILURE);
	else
		return;
pidf_cleanup:
	fprintf(stderr, "Trying to remove pidfile anyway\n");
	if (!access(pidfile, W_OK))
		unlink(pidfile);
}

void daemonize(char **pidfile, char *logfile) {
	pid_t pid;
	FILE *f;
	int fd;
	char *c;
	if (!access(*pidfile, F_OK)) {
		log("Another instance may be running. Terminating.\n");
		log("Pidfile located at %s\n", *pidfile);
		exit(EXIT_FAILURE);
	}
	switch ((pid = fork())) {
	case -1:
		log("Failed to fork");
		exit(EXIT_FAILURE);
	case 0:
		break;
	default:
		exit(EXIT_SUCCESS);
	}

	if (setsid() < 0) {
		perror("Failed to set sid");
		exit(EXIT_FAILURE);
	}

	signal(SIGHUP, SIG_IGN);
	switch ((pid = fork())) {
	case -1:
		log("Failed to perform second fork");
		exit(EXIT_FAILURE);
	case 0:
		break;
	default:
		log("Daemonized with pid %d\n", pid);
		exit(EXIT_SUCCESS);
	}
	signal(SIGHUP, SIG_DFL);

	umask(0);

	close(STDIN_FILENO);
	if (open("/dev/null", O_RDONLY) < 0) {
		perror("Failed to reopen stdin");
		goto cleanup;
	}
	if (logfile) {
		fd = open(logfile,
			O_RDWR | O_CREAT | O_APPEND,
			S_IRUSR | S_IWUSR | S_IRGRP);
		if (fd < 0) {
			perror("Failed to open logfile.");
			goto cleanup;
		}
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		close(fd);
	} else {
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		if (open("/dev/null", O_WRONLY) < 0 ||
			open("/dev/null", O_WRONLY) < 0) {
			perror("Failed to reopen stdout or stderr");
			goto cleanup;
		}
	}

	if (*pidfile) {
		c = calloc(1, MAXPATHLEN + 1);
		do {
			if (!(f = fopen(*pidfile, "w"))) {
				log("Failed to open *pidfile. Continuing.");
				break;
			}
			fprintf(f, "%d", getpid());
			fclose(f);
		} while(0);
		if (realpath(*pidfile, c)) {
			free(*pidfile);
			*pidfile = c;
		} else {
			free(c);
		}
	}

	if (chdir("/") < 0) {
		perror("Failed to changed cwd");
		goto cleanup;
	}
	return;
cleanup:
	if (*pidfile)
		unlink(*pidfile);
	exit(EXIT_FAILURE);
}
