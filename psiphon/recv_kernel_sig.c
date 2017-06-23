#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <unistd.h>


#define SIG_TEST 44 /* we define our own signal, hard coded since SIGRTMIN is different in user and in kernel space */ 

int configfd;
volatile int producerdone = 0;
char *pidfile = "/sys/kernel/debug/signalconfpid";
void get_drop_counts(void);

/* This should be sent by kernel module instead of us having to manually
 * send Ctrl^C to psiphon. This would be sent by kernel module - pdatadump
 * to indicate the relay channel has been flushed and is ready to be
 * closed, so psiphon can read up remaining and exit gracefully.
 */
void receiveSig(__attribute__((__unused__))int n, 
				siginfo_t *info, 
				__attribute__((__unused__))void *unused) 
{
    /*
     * stop trace so we can reap currently produced data
     */
    producerdone = 1;

	printf("Consumer received signal with value %i\n", info->si_int);

	get_drop_counts();

}

int write_0_and_exit(void)
{
	char buf[10];
	struct stat st;

	if (stat(pidfile, &st) < 0)
	{
		printf("File %s doesnt exist so ignore\n", pidfile);
		return 0;
	}

	if (fcntl(configfd, F_GETFL) == -1)
	{
		printf("File is already closed\n");
		return 0;
	}

    sprintf(buf, "0");
    if (write(configfd, buf, strlen(buf) + 1) < 0) {
        perror("fwrite");
        return -1;
    }
    close(configfd);
	return 0;
}

int write_pid_to_debugfs(void)
{
	char buf[10];
	/* setup the signal handler for SIG_TEST 
 	 * SA_SIGINFO -> we want the signal handler function with 3 arguments
 	 */
	struct sigaction sig;
	sig.sa_sigaction = receiveSig;
	sig.sa_flags = SA_SIGINFO;
	sigaction(SIG_TEST, &sig, NULL);

	/* kernel needs to know our pid to be able to send us a signal ->
 	 * we use debugfs for this -> do not forget to mount the debugfs!
 	 */
	configfd = open(pidfile, O_WRONLY);
	if(configfd < 0) {
		perror("open");
		return -1;
	}
	sprintf(buf, "%i", getpid());
	printf("Writing pid %i to %s\n", getpid(), pidfile);
	if (write(configfd, buf, strlen(buf) + 1) < 0) {
		printf("write errored\n"); 
		return -1;
	}

	printf("wrote pid successfully\n");	
	return 0;
}



