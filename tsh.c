/*
 * tsh - A tiny shell program with job control
 *
 * Chinmay Satpanthi csatpanthi
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"



/* Global variables */

char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */



/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);



/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
*/
void eval(char *cmdline)
{
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS];

  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  int bg = parseline(cmdline, argv);
  if (argv[0] == NULL)
    return;   /* ignore empty lines */

	pid_t pid;		//process id var
	sigset_t sig;	//signal var

	char *newenviron[] = { NULL };

	if (!builtin_cmd(argv)){			//checks if its a non built in command
		sigemptyset(& sig);					//initializes sig to exclude all defined signals
		sigaddset(&sig, SIGCHLD);		//modifies the set 
		sigprocmask(SIG_BLOCK, &sig, NULL);	//blocks the child signal SIGCHLD		

		if ((pid = fork()) == 0){								
			sigprocmask(SIG_UNBLOCK, &sig, NULL); //unblock child process
			setpgid(0,0);			//child is placed in new process group

			if (execve(argv[0], argv, newenviron) < 0 ){			//checks for return as well as create new environment as a string of arrays in the 'newenviron' char [] 	
				printf("%s: Command not found \n", argv[0]);
				exit(0);
			}	
		}
	
		addjob(jobs, pid, bg?BG:FG, cmdline);		//adds job function
		sigprocmask(SIG_UNBLOCK, &sig, NULL);		//unblocks the parent SIGCHLD signal
		
		if (bg){
			printf("[%d](%d) %s", pid2jid(pid), pid, cmdline);
		}else {
			waitfg(pid); 
		}
	}
  return;
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv)
{
	if (!strcmp(argv[0], "quit")){			//switch for 'quit'
		exit(0);
	}	

	if (!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")){	//switch for background or foregroung input 
		do_bgfg(argv);
		return 1;
	}

	if (!strcmp(argv[0], "jobs")){		//switch for 'jobs' as input
		listjobs(jobs);
		return 1;
	}	

  return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv)
{
	struct job_t *jobp=NULL;

  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }

  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //

	kill(-(jobp->pid), SIGCONT);			//sends out the SIGCONT signal

	if (!strcmp(argv[0], "bg" )){			//checks if background
		jobp->state = BG;								//sets the jobs state	
		printf("[%d] (%d) %s", jobp->jid, jobp->pid, jobp->cmdline);	
	} else {
		
		jobp->state = FG;								//if not background then set to foreground

		waitfg(jobp->pid);							//block until pid is no longer foreground process
	}
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
	while (pid == fgpid(jobs)){		//checks if foregrounds pid matches the pid provided and stall till no longer match
		sleep(0);
	}

	return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.
//
void sigchld_handler(int sig)
{
	pid_t pid;

	int check;

	while ((pid = waitpid(- 1, &check, WNOHANG|WUNTRACED)) > 0){
		if (WIFEXITED(check)){		//this checks if child process returned by waitpid exited normally
			deletejob(jobs, pid);
		}
		if (WIFSIGNALED(check)){	//checks if fucntion exited because it raised a signal that caused it to exit (zombie process)
			printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(check));
			deletejob(jobs, pid);
		}

		if (WIFSTOPPED(check)){	  //checks if process has been stopped
			printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(check));

			struct job_t *job = getjobpid(jobs, pid);
	
			if (job != NULL){
				job->state = ST;			//sets the job status to ST
			}
		}
	}
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.
//
void sigint_handler(int sig)
{
	pid_t pid = fgpid(jobs);	//sets pid to the foreground pid from jobs

	if (pid != 0) {					
		kill(-pid, SIGINT);			//delete the FG process group
	}
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.
//
void sigtstp_handler(int sig)
{
	pid_t pid = fgpid(jobs);  
	
	if (pid != 0){   
		struct job_t *job = getjobpid(jobs, pid);	
		if (job->state == ST){	//checks if job has a ST state	
			return;
		} else {
			kill(-pid, SIGTSTP);	//sends out the SIGSTP signal
		}
	}
	return;
}

/*********************
 * End signal handlers
 *********************/
