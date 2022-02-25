/*
 * TO-DO:
 *  - System logging
 *  - Logging to /var/log/proclog-daemon/proclog-daemon.log using log_stream
 */

/*
 * Daemon low-level housework list
 * 	- Fork off the parent process
 *  - Change gile mode mask (umask)
 *  - Open any logs for writing
 *  - Create a unique Session ID (SID)
 *  - Change the current working dir to a safe place
 *  - Close standard file descriptors
 *  - Enter daemon code
 * 
 * Note: forking twice has the effect of orphaning the "grandchild" process,
 * 		 as a result, it becomes the responsebility of the OS to clean up
 */

/* 
 * Fixes error: use of undeclared identifier 'F_ULOCK',
 * as it tells the headers we want the POSIX extensions. Another solution should
 * be to compile with option -D_XOPEN_SOURCE=600
 */
#if __STDC_VERSION__ >= 199901L
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>
#include <proc/readproc.h>

#define CMD_S 64
#define USER_S P_G_SZ
#define LOG_S 24000
/* https://www.pathname.com/fhs/pub/fhs-2.3.html#THEVARHIERARCHY */
#define LOG_DIR "/var/log/proclog-daemon"
#define LOG_FILE "/var/log/proclog-daemon/processes-log"
#define LOG_FORMAT "%u:%s\t\t%s\n"

static int running = 0;
static int delay = 1;
static char *pid_file_name = NULL;
static int pid_fd = -1;
static char *app_name = NULL;
static FILE *log_stream;

/* Holds single process data */
typedef struct Entry{
	int tid;
	char cmd[CMD_S], user[USER_S];
} Entry;

/* Structure holding all entries of the processes */
typedef struct List{
	int empty_pos;
	Entry log[LOG_S];
} List;

FILE *fptr;

/* Handles signals we care about */
void handle_signal(int sig)
{
	if (sig == SIGINT) {
		fprintf(log_stream, "Debug: stopping daemon ...\n");
		/* Unlock and close lockfile */
		if (pid_fd != -1) {
			lockf(pid_fd, F_ULOCK, 0);
			close(pid_fd);
		}
		/* Try to delete lockfile */
		if (pid_file_name != NULL) {
			unlink(pid_file_name);
		}
		running = 0;
		/* Reset signal handling to default behavor */
		signal(SIGINT, SIG_DFL);
	} else if (sig == SIGHUP) {
		fprintf(log_stream, "Debug: received SIGHUP signal ...\n");
	} else if (sig == SIGCHLD) {
		fprintf(log_stream, "Debug: received SIGCHLD signal ...\n");
	}
}

/*
 * Daemonizes the application
 * https://www.freedesktop.org/software/systemd/man/daemon.html
 */
static void daemonize(void)
{
	int fd;
	pid_t pid, sid;
	
	/* 
	 * Fork off the parent process.
	 * fork() function returns either the process id (PID) of the child
	 * process (not equal to zero), or -1 on failure
	 */
	pid = fork();
	if (pid < 0) {
		/* syslog error? */
		perror("Failed");
		exit(EXIT_FAILURE);
	}

	if (pid > 0) {
		/* Success, terminate parent */
		exit(EXIT_SUCCESS);
	}

	/*
	 * On success: the child process becomes the session leader.
	 * Note: the child process MUST get a unique SID from the kernel to 
	 * operate otherwise it becomes an orphan in the system
	 */
	sid = setsid();
	if (sid < 0) {
		/* syslog error? */
		perror("Failed");
		exit(EXIT_FAILURE);
	}

	/* Ignore signal sent from child to parent process */
	signal(SIGCHLD, SIG_IGN);

	/* Fork off for the second time */
	pid = fork();
	if (pid < 0) {
		/* syslog error? */
		perror("Failed");
		exit(EXIT_FAILURE);
	}

	if (pid > 0) {
		/* Success, terminate parent */
		exit(EXIT_SUCCESS);
	}

	/*
	 * By setting the umask to 0, we will have full access to the files
	 * generated by the daemon.
	 */
	umask(0);

	/* Change the working directory */ 
	if (chdir("/") < 0) {
		/* syslog error? */
		perror("Failed");
		exit(EXIT_FAILURE);
	}

	/**
	 * Closing all file descriptors (STDIN, STDOUT, STDERR). Since a daemon
	 * cannot use the terminal, these are redundant
	 */
	for (fd = sysconf(_SC_OPEN_MAX); fd > 0; fd--) {
		close(fd);
	}

	/* Reopen stdin (fd = 0), stdout (fd = 1), stderr (fd = 2) */
	stdin = fopen("/dev/null", "r");
	stdout = fopen("/dev/null", "w+");
	stderr = fopen("/dev/null", "w+");

	/* Try to write PID of daemon to lockfile */
	if (pid_file_name != NULL) {
		char str[256];
		pid_fd = open(pid_file_name, O_RDWR|O_CREAT, 0640);
		if (pid_fd < 0) {
			/* Cannot open lockfile */
			perror("Failed");
			exit(EXIT_FAILURE);
		}
		if (lockf(pid_fd, F_TLOCK, 0) < 0) {
			/* Cannot lock file */
			perror("Failed");
			exit(EXIT_FAILURE);
		}
		/* Get current PID and write it to lockfile */
		sprintf(str, "%d\n", getpid());
		write(pid_fd, str, strlen(str));
	}
}

int in_log(List* ls, int* tid)
{
	for (int i = 0; i < ls->empty_pos; i++)
		if (ls->log[i].tid == *tid) 
			return 1;
	return 0;
}

/* Fills the List structure with data from LOG_FILE */
void sync_log(List* ls)
{
	/* If exists, open at end, otherwise creates file */
	fptr = fopen(LOG_FILE, "a");
	if (fptr == NULL) {
		perror("Failed");
		exit(EXIT_FAILURE);
	}
	fclose(fptr);

	/* Check for errors on file */
	if (access(LOG_FILE, F_OK) != 0) {
		printf("Error opening file\n");
		exit(1);
	}

	int tid;
	char cmd[CMD_S], user[USER_S];

	/* Open for reading and scan first line */
	fptr = fopen(LOG_FILE, "r");
	fscanf(fptr, LOG_FORMAT, &tid, cmd, user);

	/* Loops until end of file, loading data into List */
	while (!feof (fptr)) {
		if (!in_log(ls, &tid)) {
			ls->log[ls->empty_pos].tid = tid;
			strncpy(ls->log[ls->empty_pos].cmd, cmd, CMD_S);
			strncpy(ls->log[ls->empty_pos].user, user, CMD_S);
			ls->empty_pos++;
		}
		fscanf(fptr, LOG_FORMAT, &tid, cmd, user);
	}

	fclose(fptr);
}

/* Iterates through all processes and logs new ones */
void iterate(proc_t* proc_info, List* ls)
{
	PROCTAB* proc = openproc(PROC_FILLMEM | PROC_FILLUSR | PROC_FILLSTATUS);

	while (readproc(proc, proc_info) != NULL) {
		/*
		 * Blacklisting redundant services e.g.
		 * if(in_log(ls, &proc_info->tid) || 
		 * 	!strcmp(proc_info->cmd, "sleep") ||
		 * 	!strcmp(proc_info->cmd, "cpuUsage.sh"))
		 */
		if (in_log(ls, &proc_info->tid))
			continue;
		
		/* Writing to the List structure */
		ls->log[ls->empty_pos].tid = proc_info->tid;
		strncpy(ls->log[ls->empty_pos].cmd, proc_info->cmd, CMD_S);
		strncpy(ls->log[ls->empty_pos].user, proc_info->euser, USER_S);
		
		/* Writing to the log file */
		fprintf(fptr, LOG_FORMAT, proc_info->tid, proc_info->cmd, 
				proc_info->euser);
		ls->empty_pos++;			
	}
	closeproc(proc);
}

/* Prints usage guide */
void print_help(void)
{
	printf("\n Usage: %s [OPTIONS]\n\n", app_name);
	printf("\n Note: --log_file references the system log file for the\n");
	printf("         daemon, while --read references the log file output by\n");
	printf("         the application, that logs all ran services\n");
	printf("\n Options:\n");
	printf("   -h --help                 Print this help page\n");
	printf("   -r --read                 Open output log file for reading\n");
	printf("   -w --wipe                 Wipe output log file\n");
	printf("\n Options below are meant for systemd, check service file\n\n");
	printf("   -l --log_file  filename   Define the proclog-daemon.log file\n");
	printf("   -d --daemon               Daemonize this application\n");
	printf("   -p --pid_file  filename   PID file used by daemonized app\n");
	printf("\n");
}

int main(int argc, char *argv[])
{
	/* Prevents running wild */
	if (argv[1] == NULL) {
		printf("\nExecution without parameters is not intended!\n");
		printf("Read the README for instructions on using the daemon\n");
		return EXIT_SUCCESS;
	}

	/* Extension of the option struct from include/bits */
	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"read", no_argument, 0, 'r'},
		{"wipe", no_argument, 0, 'w'},
		{"log_file", required_argument, 0, 'l'},
		{"pid_file", required_argument, 0, 'p'},
		{"daemon", no_argument, 0, 'd'},
		{NULL, 0, 0, 0}
	};

	int value, option_index = 0, ret;
	char *log_file_name = NULL;
	int start_daemonized = 0;

	/* app_name = /usr/bin/proclog-daemon */
	app_name = argv[0];

	/* Process command line arguments */
	while ((value = getopt_long(argc, argv, "l:p:dwrh", long_options, 
			&option_index)) != -1) {
		switch (value)
		{
		case 'h':
			print_help();
			return EXIT_SUCCESS;
		case '?':
			print_help();
			return EXIT_SUCCESS;
		case 'r':
			system("vim -R /var/log/proclog-daemon/log");
			//execve("/usr/bin/vim", execv_arg, NULL);
			return EXIT_SUCCESS;
		case 'w':
			/* Requires sudo */
			fptr = fopen(LOG_FILE, "w");
			if (fptr == NULL) {
				perror("Failed");
				exit(EXIT_FAILURE);
			}
			return EXIT_SUCCESS;
		case 'l':
			log_file_name = strdup(optarg);
			break;
		case 'p':
			pid_file_name = strdup(optarg);
			break;
		case 'd':
			start_daemonized = 1;
			break;
		default:
			break;
		}
	}

	/* When daemonizing is requested */
	if (start_daemonized == 1) {
		daemonize();
	}

	/* Open system log and write startup message */
	openlog(argv[0], LOG_PID|LOG_CONS, LOG_DAEMON);
	syslog(LOG_INFO, "Started %s", app_name);
	
	/* 
	 * Signal handling!
	 *  - from: https://en.wikipedia.org/wiki/C_signal_handling
	 * SIGABRT - "abort", abnormal termination.
	 * SIGFPE - floating point exception.
	 * SIGILL - "illegal", invalid instruction.
	 * SIGINT - "interrupt", interactive attention request sent to the program.
	 * SIGSEGV - "segmentation violation", invalid memory access.
	 * SIGTERM - "terminate", termination request sent to the program.
	 * 
	 * Notes: 
	 * SIG_DFL - let the kernel handle it 
	 * SIG_IGN - ignore signal
	 */
	signal(SIGABRT, SIG_DFL);
	signal(SIGFPE, SIG_IGN);
	signal(SIGILL, SIG_IGN);
	signal(SIGINT, handle_signal);
	signal(SIGSEGV, SIG_IGN);
	signal(SIGTERM, SIG_DFL);
	signal(SIGHUP, handle_signal);

	/* Try to open log file */
	if (log_file_name != NULL) {
		log_stream = fopen(log_file_name, "a+");
		if (log_stream == NULL) {
			syslog(LOG_ERR, "Cannot open log file: %s, error: %s", 
				   log_file_name, strerror(errno));
			log_stream = stdout;
		}
	} else {
		log_stream = stdout;
	}

	/* Can be changed in function handle_signal */
	running = 1;

	/* Alloc in memory log */
	List* ls = malloc(sizeof(List));

	/* Proc reading alloc */
	proc_t proc_info;
	memset(&proc_info, 0, sizeof(proc_info));

	sync_log(ls);

	/* Main loop */
	while (running == 1) {
		fptr = fopen(LOG_FILE, "a");
		iterate(&proc_info, ls);
		fclose(fptr);
		sleep(delay);
	}

	/* Close log file, when it is used */
	if (log_stream != stdout) {
		fclose(log_stream);
	}

	/* Write syslog and close it */
	syslog(LOG_INFO, "Stopped %s", app_name);
	closelog();

	/* Free alloc memory */
	if (log_file_name != NULL) free(log_file_name);
	if (pid_file_name != NULL) free(pid_file_name);
	if (ls != NULL) free(ls);
	
	return EXIT_SUCCESS;
}