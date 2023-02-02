#include <assert.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "jobs.h"

job_list_t *job_list;
char *fg_command[512];
int job_number;
pid_t parent_pgid;

/*
 * Removes whitespace from buffer and creates an array with each input on the
 * line (tokens) as well as an array with each input, including only the file
 * name instead of file path
 *
 * Parameters:
 *  - buffer: an array containing the input the user types in
 *  - tokens: an array containing the parsed inputs, including the filepath
 *  - argv: an array containing the parsed inputs without the full filepath
 * (only file name)
 *
 * Returns:
 *  - nothing
 */
void parse(char buffer[1024], char *tokens[512], char *argv[512]) {
    char *str = &buffer[0];  // sets a pointer to the buffer array
    int i = 0;
    char *tokenArray = strtok(str, " \t\n");
    while (tokenArray != NULL) {
        tokens[i] = tokenArray;
        char *modified = tokenArray;
        char *occurrence = strrchr(modified, '/');
        if (occurrence == NULL && i == 0) {
            argv[0] = tokenArray;  // if Null and first word
        } else if (occurrence != NULL &&
                   i == 0) {  // if slash is in file and it is first token word
            argv[0] = occurrence + 1;  // next word
        } else {
            argv[i] = tokenArray;  // else just make arg the token word
        }
        tokenArray = strtok(NULL, " \t\n");  // move on to next word
        i++;                                 // add one to index
    }
}

/*
* Determines whether the user has typed any redirect symbols (<, >, >>). If
there are too many
* symbols or no specified input/output file, an error is printed.
*
* Parameters:
*  - argv: an array containing the parsed inputs without the full filepath
(only file name)
*  - no_redirect: an array containing all the elements of argv except the
redirect symbols and their accompanying files
*  - input_file: if an input redirect < is present, the indicated input file is
stored in this array
*  - output_file: if an output redirect >, >> is present, the indicated output
file is stored in this array
*  - is_append: an int that is 0 if the redirect symbol indicates appending
(>>) and 1 if not
*
* Returns:
*  - 1 if argv was empty (no input) and 0 if not
*/
int parse_redirects(char *argv[], char *no_redirect[], char *input_file[],
                    char *output_file[], int *is_append, int *is_background) {
    int no_redirects_counter = 0;
    int amt_input_redirects = 0;
    int amt_output_redirects = 0;
    int amt_append_redirects = 0;
    int amt_redirects = 0;

    for (int i = 0; i < 512; i++) {
        if (amt_redirects > 1) {
            /* if there are more than 1 redirect symbols */
            if (amt_input_redirects > 1) {
                /* if there are two < */
                fprintf(stderr, "syntax error: multiple input files \n");
                break;
            }
            if (amt_output_redirects > 1 || amt_append_redirects > 1 ||
                (amt_append_redirects + amt_output_redirects) > 1) {
                /* if there are two > or >> or both at the same time*/
                fprintf(stderr, "syntax error: multiple output files \n");
                break;
            }

        } else if (!argv[i]) {
            if (i == 0) {
                /* if argv is empty*/
                return 1;
            }

        } else if (strcmp(argv[i], "<") == 0 && !argv[i + 1]) {
            /* no input file specified */
            fprintf(stderr, "must specify input file \n");
            break;
        } else if (strcmp(argv[i], ">") == 0 && argv[i + 1] == NULL) {
            /* no output file specified */

            fprintf(stderr, "must specify output file \n");
            break;
        } else if (strcmp(argv[i], ">>") == 0 && !argv[i + 1]) {
            /* no output file specified */
            fprintf(stderr, "must specify output file \n");
            break;
        } else if (strcmp(argv[i], ">") == 0 && argv[i]) {
            if (strcmp(argv[i + 1], "<") == 0 ||
                strcmp(argv[i + 1], ">") == 0 ||
                strcmp(argv[i + 1], ">>") == 0) {
                /* if there is a > followed by >, <, or >>*/
                fprintf(
                    stderr,
                    "cannot have two redirect symbols next to each other \n");
                break;
            }
            output_file[0] = argv[i + 1];  // set the output file
            i++;
            amt_redirects++;  // increase number of total redirects and output
                              // redirects
            amt_output_redirects++;

        } else if (strcmp(argv[i], "<") == 0 && argv[i]) {
            if (strcmp(argv[i + 1], "<") == 0 ||
                strcmp(argv[i + 1], ">") == 0 ||
                strcmp(argv[i + 1], ">>") == 0) {
                /* if there is a < followed by >, <, or >>*/
                fprintf(
                    stderr,
                    "cannot have two redirect symbols next to each other \n");
                break;
            }
            input_file[0] = argv[i + 1];
            i++;
            amt_redirects++;
            amt_input_redirects++;

        } else if (strcmp(argv[i], ">>") == 0 && argv[i]) {
            if (strcmp(argv[i + 1], "<") == 0 ||
                strcmp(argv[i + 1], ">") == 0 ||
                strcmp(argv[i + 1], ">>") == 0) {
                /* if there is a >> followed by >, <, or >>*/
                fprintf(
                    stderr,
                    "cannot have two redirect symbols next to each other \n");
                break;
            }
            output_file[0] = argv[i + 1];
            *is_append = 1;
            i++;
            amt_redirects++;
            amt_append_redirects++;

        } else if ((strcmp(argv[i], "&") == 0) && (!argv[i + 1])) {
            // if argv[i] is & and is the last character, should be a background
            // job
            *is_background = 1;
        } else {
            no_redirect[no_redirects_counter] = argv[i];
            no_redirects_counter++;
        }
    }
    return 0;
}

/*
* Checks if cd, rm, ln, exit, fg, and bg was called and executes appropriately.
*
* Parameters:
*  - no_redirect: an array containing all the elements of argv except the
redirect symbols and their accompanying files
*  - tokens: an array containing the parsed inputs, including the filepath
*  - is_background: a pointer to an int that tells if it is a background process or not
*
* Returns:
*  - 1 if cd, rm, ln, fg, and bg was called and 0 otherwise
*/
int check_sys_cmds(char *no_redirect[], char *tokens[], int *is_background) {
    if (strcmp(no_redirect[0], "cd") == 0 &&
        strcmp(tokens[0], "/bin/cd") != 0) {
        if (!no_redirect[1]) {
            /* if cd is not followed by anything and is builtin */
            fprintf(stderr, "cd: syntax error \n");
        } else {
            int cd_err = chdir(no_redirect[1]);
            if (cd_err == -1) {
                perror("cd");
            }
        }
        return 1;

    } else if (strcmp(no_redirect[0], "ln") == 0 &&
               strcmp(tokens[0], "/bin/ln") != 0) {
        if (!no_redirect[1]) {
            /* if ln is not followed by anything and is builtin */
            fprintf(stderr, "ln: syntax error \n");
        } else {
            int ln_err = link(no_redirect[1], no_redirect[2]);
            if (ln_err == -1) {
                perror("ln");
            }
        }
        return 1;

    } else if (strcmp(no_redirect[0], "rm") == 0 &&
               strcmp(tokens[0], "/bin/rm") != 0) {
        if (!no_redirect[1]) {
            /* if rm is not followed by anything and is builtin */
            fprintf(stderr, "rm: syntax error \n");
        } else {
            int rm_err = unlink(no_redirect[1]);
            if (rm_err == -1) {
                perror("rm");
            }
        }
        return 1;

    } else if (strcmp(no_redirect[0], "jobs") == 0) {
        // treating "jobs" like other system commands
        if (no_redirect[1]) {
            fprintf(stderr, "jobs: syntax error \n");
        }
        jobs(job_list);
        return 1;

    } else if (strcmp(no_redirect[0], "fg") == 0) {
        // treating "fg" like other system commands
        char jobid[512];
        memset(&jobid[0], 0, 512 * sizeof(char));
        int i = 1;
        int j = 0;
        int status;
        if (!no_redirect[1]) {
            fprintf(stderr, "fg: syntax error \n");
        } else {
            while (no_redirect[1][i]) {
                jobid[j] = no_redirect[1][i];
                i++;
                j++;
            }
            int thejobid = atoi(jobid);
            pid_t theprocessid = get_job_pid(job_list, thejobid);
            if (theprocessid == -1) {
                fprintf(stderr, "job not found \n");
                return 1;
            }
            
           // update_job_jid(job_list, thejobid, RUNNING);
            kill(-theprocessid, SIGCONT);
            tcsetpgrp(0, theprocessid);
            

            int wait_err = waitpid(theprocessid, &status, WUNTRACED);
            if (wait_err == -1) {
                perror("waitpid");
            } else if (WIFSIGNALED(status)) {
                // terminated by a signal
                fprintf(stdout, "[%d] (%d) terminated by signal %d\n",
                        thejobid, wait_err, WTERMSIG(status));
                remove_job_jid(job_list, thejobid);
                
            } else if (WIFSTOPPED(status)) {
                // stopped
                fprintf(stdout, "[%d] (%d) suspended by signal %d\n",
                        thejobid, wait_err, WSTOPSIG(status));
                update_job_jid(job_list, thejobid, STOPPED);
            }else if(WIFEXITED(status)){
                remove_job_jid(job_list, thejobid);
            }
            tcsetpgrp(0, getpgrp());
            //remove_job_jid(job_list, thejobid);

            return 1;
        }
    } else if (strcmp(no_redirect[0], "bg") == 0) {
        // treating "jobs" like other system commands
        char jobid[512];
        memset(&jobid[0], 0, 512 * sizeof(char));
        int i = 1;
        int j = 0;
        int status;
        if (!no_redirect[1]) {
            fprintf(stderr, "bg: syntax error \n");
        } else {
            while (no_redirect[1][i]) {
                jobid[j] = no_redirect[1][i];
                i++;
                j++;
            }
            int thejobid = atoi(jobid);
            pid_t theprocessid = get_job_pid(job_list, thejobid);
            if (theprocessid == -1) {
                fprintf(stderr, "job not found \n");
                return 1;
            }
            kill(-theprocessid, SIGCONT);
            update_job_jid(job_list, thejobid, RUNNING);
            *is_background = 2;
        }
        tcsetpgrp(0, getpgrp());

        return 1;
    }

    else if (strcmp(no_redirect[0], "exit") == 0) {
        cleanup_job_list(job_list);
        exit(0);
    }

    return 0;
}

/*
 * Checks if a redirect symbol was used and then closes and opens the
 * appropriate file descriptor with the new specified file
 *
 * Parameters:
 *  - input_file: the array containing a new input file, if specified, or
 * "stdin" otherwise
 *  - output_file: the array containing a new output file, if specified, or
 * "stdout" otherwise
 *  - is_append: an int that specifies whether the output file should be
 * appended
 *
 * Returns:
 *  - nothing
 */
void redirect_file(char *input_file[], char *output_file[], int is_append) {
    if (strcmp(input_file[0], "stdin") != 0) {
        /* changing the input file */
        close(0);
        if (open(input_file[0], O_RDWR, S_IRWXU) == -1) {
            perror("input error");
            cleanup_job_list(job_list);
            exit(1);
        }
    }
    if (strcmp(output_file[0], "stdout") != 0 && is_append == 0) {
        /* changing the output file wihtout the append flag */
        close(1);
        if (open(output_file[0], O_CREAT | O_TRUNC | O_RDWR, S_IRWXU) == -1) {
            perror("output error");
            cleanup_job_list(job_list);
            exit(1);
        }

    } else if (strcmp(output_file[0], "stdout") != 0 && is_append == 1) {
        /* changing the output file with the append flag */
        close(1);
        if (open(output_file[0], O_CREAT | O_APPEND | O_RDWR, S_IRWXU) == -1) {
            perror("append error");
            cleanup_job_list(job_list);
            exit(1);
        }
    }
}

/*
 * Checks if a redirect symbol was the first element in the tokens array,
 * and then resets the tokens array to start after the symbol and its
 * following file. This makes it so that tokens[0] is always the filepath of
 * the program to execute.
 *
 * Parameters:
 *  - tokens: an array containing the parsed inputs, including the filepath
 *
 * Returns:
 *  - nothing
 */
void get_filepath(char *tokens[]) {
    if (strcmp(tokens[0], ">") == 0 || strcmp(tokens[0], "<") == 0 ||
        strcmp(tokens[0], ">>") == 0) {
        tokens[0] = tokens[2];
    }
}

/*
 * Installs the signal handler so that we can handle the signals
 *
 * Parameters:
 *  - sig: an int representing the signal
 * - handler: the handler we want to install
 *
 * Returns:
 *  - int
 */
int install_handler(int sig, void (*handler)(int)) {
    if (signal(sig, handler) != SIG_ERR) {
        return 0;
    }

    return -1;
}

/*
 * Ignores the signals so that the shell does not accidentally exit prematurely
 *
 *
 * Returns:
 *  - nothing
 */
void ignore_signals() {
    sigset_t old;
    sigset_t full;
    sigfillset(&full);

    // Ignore signals while installing handlers
    sigprocmask(SIG_SETMASK, &full, &old);

    if (install_handler(SIGINT, SIG_IGN))
        perror("Warning: could not install handler for SIGINT");

    if (install_handler(SIGTSTP, SIG_IGN))
        perror("Warning: could not install handler for SIGTSTP");

    if (install_handler(SIGTTOU, SIG_IGN))
        perror("Warning: could not install handler for SIGTTOU");

    // Restore signal mask to previous value
    sigprocmask(SIG_SETMASK, &old, NULL);
}

/*
 * Resets the signals to the default so that the signals can then be used by other processes
 *
 * Returns:
 *  - nothing
 */
void reset_signals() {
    sigset_t old;
    sigset_t full;
    sigfillset(&full);

    // Ignore signals while installing handlers
    sigprocmask(SIG_SETMASK, &full, &old);

    if (install_handler(SIGINT, SIG_DFL))
        perror("Warning: could not install handler for SIGINT");

    if (install_handler(SIGTSTP, SIG_DFL))
        perror("Warning: could not install handler for SIGTSTP");

    if (install_handler(SIGTTOU, SIG_DFL))
        perror("Warning: could not install handler for SIGTTOU");

    // Restore signal mask to previous value
    sigprocmask(SIG_SETMASK, &old, NULL);
}

/*
 * Goes through each child process and checks to make sure that if something has
 * changed status, it is handled appropriately and removed form the job list, or
 * has its job status updated depending on what signal affects the process.
 *
 *
 * Returns:
 *  - nothing
 */
void reaper() {
    int wret, wstatus;

    while ((wret = waitpid(-1, &wstatus, WNOHANG | WUNTRACED | WCONTINUED)) >
           0) {
        int jid = get_job_jid(job_list, wret);

        if (WIFEXITED(wstatus)) {
            // terminated normally
            fprintf(stdout, "[%d] (%d) terminated with exit status %d\n", jid,
                    wret, WEXITSTATUS(wstatus));
            remove_job_jid(job_list, jid);

        } else if (WIFSIGNALED(wstatus)) {
            // terminated by a signal
            fprintf(stdout, "[%d] (%d) terminated by signal %d\n", jid, wret,
                    WTERMSIG(wstatus));
            remove_job_jid(job_list, jid);
        }
        if (WIFSTOPPED(wstatus)) {
            // stopped
            update_job_jid(job_list, jid, STOPPED);
            fprintf(stdout, "[%d] (%d) suspended by signal %d\n", jid, wret,
                    WSTOPSIG(wstatus));
        }
        if (WIFCONTINUED(wstatus)) {
            // continued
            update_job_jid(job_list, jid, RUNNING);
            fprintf(stdout, "[%d] (%d) resumed\n", jid, wret);
        }
    }
}

int main() {
    
    char buffer[1024];
    char *tokens[512];
    char *argv[512];
    char *no_redirect[512];
    char *input_file[50];
    char *output_file[50];
    int is_append;
    int is_background_job = 0;
    job_list = init_job_list();
    job_number = 1;
    parent_pgid = getpid();
    ignore_signals();

    while (1) { /*inifinite while loop*/
        if (job_number > 1) {
            reaper();
        }
#ifdef PROMPT
        int err = printf("33sh> ");
        if (err < 0) {
            /* handle a write error */
            fprintf(stderr, "Writing to terminal failed: %i\n", err);
            return 1;
        }
        int err_two = fflush(stdout);
        if (err_two < 0) {
            fprintf(stderr, "Writing to terminal failed: %i\n", err);
            return 1;
        }
#endif
        int status;

        /* we use memset to reset all the arrays to ensure that they are all
        cleared from previous
        uses and the contents do not carry over to the new input*/
        memset(&argv[0], 0, 512 * sizeof(char *));
        memset(&buffer[0], 0, 1024 * sizeof(char));
        memset(&tokens[0], 0, 512 * sizeof(char *));
        memset(&no_redirect[0], 0, 512 * sizeof(char *));
        memset(&input_file[0], 0, 1 * sizeof(char *));
        memset(&output_file[0], 0, 1 * sizeof(char *));
        memset(&fg_command[0], 0, 512 * sizeof(char *));
        is_append = 0;  // 0 for not, 1 for yes
        is_background_job = 0;
        int *append_ptr = &is_append;
        int *is_background_ptr = &is_background_job;
        input_file[0] = "stdin";
        output_file[0] = "stdout";

        ssize_t buffer_size = read(0, buffer, 1024);

        if (buffer_size == -1) {
            fprintf(stderr, "Reading input failed \n");
        } else if (buffer_size == 1) {
            continue;
        } else if (buffer_size > 1024) {
            fprintf(stderr, "input is too long \n");
        } else if (buffer_size == 0) {
            /* in the case of ctrl-d */
            cleanup_job_list(job_list);
            exit(0);
        }

        parse(buffer, tokens, argv);
        fg_command[0] = tokens[0];

        if (argv[0] == NULL) {
            continue;
        }

        parse_redirects(argv, no_redirect, input_file, output_file, append_ptr,
                        is_background_ptr);
        get_filepath(tokens);  // making sure tokens[0] is the full filepath
                               // name
        int sys_cmd = check_sys_cmds(argv, tokens, is_background_ptr);

        if (sys_cmd == 0) {
            /* if cd, rm, or ln was not already called */

            pid_t pid = 0;

            if ((pid = fork()) == 0) {
                // making the the group process id unique
                pid_t *pid_ptr = &pid;
                *pid_ptr = getpid();
                setpgid(pid, pid);

                if (is_background_job == 0) {
                    // if it is not a background job, give it control of the
                    // terminal
                    tcsetpgrp(0, pid);
                    reset_signals();
                    redirect_file(input_file, output_file, is_append);

                } else {
                    // if it is a background job, add it to the jobs list
                    tcsetpgrp(0, parent_pgid);
                    reset_signals();
                    fprintf(stdout, "[%d] (%d) \n", job_number, pid);
                    redirect_file(input_file, output_file, is_append);
                }

                execv(tokens[0], no_redirect);
                perror("execv");
                cleanup_job_list(job_list);
                exit(0);
            }

            if (is_background_job == 0) {
                int wait_err = waitpid(pid, &status, WUNTRACED);
                if (wait_err == -1) {
                    perror("waitpid");
                } else if (WIFSIGNALED(status)) {
                    // terminated by a signal
                    fprintf(stdout, "[%d] (%d) terminated by signal %d\n",
                            job_number, wait_err, WTERMSIG(status));
                    remove_job_jid(job_list, job_number);
                } else if (WIFSTOPPED(status)) {
                    // stopped
                    fprintf(stdout, "[%d] (%d) suspended by signal %d\n",
                            job_number, wait_err, WSTOPSIG(status));
                    add_job(job_list, job_number, pid, STOPPED, tokens[0]);
                    job_number++;
                }
            } else if (is_background_job == 1) {
                // increase number of current background job
                add_job(job_list, job_number, pid, RUNNING, tokens[0]);
                job_number++;
            }

            tcsetpgrp(0, parent_pgid);
            
        }
    }

    cleanup_job_list(job_list);

    return 0;
}
