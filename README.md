## mpdsub

This program connects to a running mpd instance and outputs, in a configurable manner, the currently playing song. The program can be configured via a text config.

Example usage (outfile will contain the latest song played):

	`mpdsub -kdOr --pidfile ~/.mpd/mpdsub.pid --outfile ~/.mpd/mpdsub.playing`

Options:
```
	-?, --help
		display the help message
	-h, --host HOST
		mpd instance hostname (or socket file, defaults to localhost)
	-p, --port PORT
		mpd instance port (defaults to 6600)
	-P, --password PASSWORD
		password for mpd instance
	-f, --format FORMAT
		song format: text with tokens in format
		'%tag|prefix|suffix|condprefix%' (each optional)
		(condprefix is output iff the previous tag is present)
	-O, --overwrite
		if specified, overwrite output file with latest song only
	-r, --retry
		keep trying to reconnect to mpd
	-d, --daemonize
		run in background
	-k, --kill
		kill an already running instance
	-o, --outfile OUTFILE
		output file (defaults to stdout)
	--pidfile PIDFILE
		pidfile location
	-l, --logfile LOGFILE
		logfile location
```
