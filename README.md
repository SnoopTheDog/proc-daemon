# proc daemon

Desc. . .

Things to note:

`/var/log/proc-daemon` holds the `log` (application output) and `proc-daemon.log` (debug) files.

`/run/proc-daemon` holds the `proc-daemon.pid` lockfile.

The `proc-daemon.service` file will be copied into `/usr/lib/systemd/system`.
## Installation
```bash
sudo make
```

## Usage

```bash
# Start logging
sudo systemctl start proc-daemeon

# Stop logging
sudo systemctl stop proc-daemeon

# Check service status
sudo systemctl status proc-daemeon

# Tail using journalctl
journalctl --unit proc-daemon --follow

# Command line options
 Usage: /usr/bin/proc-daemon [OPTIONS]


 Note: --log_file references the system log file for the
         daemon, while --read references the log file output by
         the application, that logs all ran services

 Options:
   -h --help                 Print this help page
   -r --read                 Open output log file for reading
   -w --wipe                 Wipe output log file

 Options below are meant for systemd, check service file

   -l --log_file  filename   Define the proc-daemon.log file
   -d --daemon               Daemonize this application
   -p --pid_file  filename   PID file used by daemonized app
```