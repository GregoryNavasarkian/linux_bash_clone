
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/wait.h>

#define MAX_ARGS 512
#define MAX_INPUT 2048

// function declarations
void getUserInput(char *userInput, char **arguments, char **inputFile, char **outputFile, bool *runBackground, int *numArgs);
void changeDirectory(char *const *arguments, int numArgs);
void getStatus(int childExitMethod);
void fileRedirect(const char *inputFile, const char *outputFile, bool runBackground, int fileNo);
void handleSigStop();

// bool for background mode toggle
bool isForeground = false;

/*
 * function main
 * returns void
 * sets signals, checks for builtin functions, runs fork/exec
 */
int main(void) {
    char userInput[MAX_INPUT];
    char *arguments[MAX_ARGS];
    char *inputFile;
    char *outputFile;
    int childExitMethod = 0;
    pid_t pid;
    pid_t processes[MAX_ARGS];
    int numProcesses = 0;
    bool runBackground = false;

    // custom signal handler for sig stop
    struct sigaction sig1 = {0};
    sig1.sa_handler = handleSigStop;
    sig1.sa_flags = SA_RESTART;
    sigfillset(&sig1.sa_mask);
    sigaction(SIGTSTP, &sig1, NULL);

    // custom signal handler for sig interrupt
    struct sigaction sig2 = {0};
    sig2.sa_handler = SIG_IGN;
    sig2.sa_flags = 0;
    sigfillset(&sig2.sa_mask);
    sigaction(SIGINT, &sig2, NULL);

    while (true) {
        // reset variables upon each iteration of loop
        inputFile =  NULL;
        outputFile = NULL;
        runBackground = false;

        // print colon on each line
        printf(": ");
        fflush(stdout);

        // clear input and arguments buffers
        memset(userInput, 0, sizeof userInput);
        memset(arguments, 0, sizeof arguments);

        // get user input
        int numArgs;
        getUserInput(userInput, arguments, &inputFile, &outputFile, &runBackground, &numArgs);

        // check for empty input or comment, if true do nothing and continue
        if (userInput[0] == '#' || strlen(userInput) == 1) {}

        // if cd argument run changeDirectory function, set runBackground to false
        else if (strcmp(arguments[0], "cd") == 0) {
            runBackground = false;
            changeDirectory(arguments, numArgs);
            fflush(stdout);
        }

        // if status argument run getStatus
        else if (strcmp(arguments[0], "status") == 0) {
            getStatus(childExitMethod);
            fflush(stdout);
        }

        // if exit command kill running background processes and end loop
        else if (strcmp(arguments[0], "exit") == 0) {
            // loop through background pids
            for (int i = 0; i < numProcesses; i++) {
                // attempt to kill process
                int killed = kill(processes[i], SIGTERM);
                // if process is successfully killed print message
                if (killed == 0) {
                    childExitMethod = 2;
                    printf("background pid %d is done: terminated by signal %d\n", processes[i], WTERMSIG(childExitMethod));
                }
            }
            _exit(0);
        }

        else {
            // fork process
            pid = fork();
            // if fork fails
            if (pid == -1) {
                perror("fork()\n");
                _exit(1);
            }
            // if pid is 0 run child process
            else if (pid == 0) {
                sig2.sa_handler = SIG_DFL;
                sigaction(SIGINT, &sig2, NULL);
                int fileNo = -1;
                // get file redirects if they exist
                fileRedirect(inputFile, outputFile, runBackground, fileNo);
                // run exec, if error print message
                if (execvp(arguments[0], arguments)) {
                    // this should not happen
                    printf("smallsh: %s: No such file or directory\n", arguments[0]);
                    _exit(1);
                }
            }
            // run parent process
            else {
                // if background is allowed print background pid and add process to count
                if (runBackground == true) {
                    printf("background pid is %d\n", pid);
                    processes[numProcesses++] = pid;
                    fflush(stdout);
                }
                // if background process not allowed wait for process
                else {
                    waitpid(pid, &childExitMethod, 0);
                    if (WIFSIGNALED(childExitMethod) && isForeground == false) {
                        printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
                    }
                    fflush(stdout);
                }
            }
        }
        while((pid = waitpid(-1, &childExitMethod, WNOHANG)) > 0) {
            // upon completion of process, print pid and status
            if (WIFEXITED(childExitMethod)){
                if (isForeground == false)
                    printf("background pid %d is done: exit value %d\n", pid, WEXITSTATUS(childExitMethod));
                fflush(stdout);
            }
            // if process is interrupted print pid and termination signal
            else {
                printf("background pid %d is done: terminated by signal %d\n", pid, WTERMSIG(childExitMethod));
                fflush(stdout);
            }
        }
    }
}

/*
 * function to get and parse user input
 * checks for background processes, redirection, and comments
 * returns void
 */
void getUserInput(char *userInput, char **arguments, char **inputFile, char **outputFile, bool *runBackground, int *numArgs) {
    // reset argument counter
    (*numArgs) = 0;
    fgets(userInput, MAX_INPUT, stdin);  // get user input
    strtok(userInput, "\n");  // remove newline char

    // loop to replace $$ with pid
    for (size_t i = 0; i <= strlen(userInput); i++) {
        if (userInput[i] == '$' && userInput[i + 1] == '$') {
            char *temp = strdup(userInput);
            temp[i] = '%';
            temp[i + 1] = 'd';
            sprintf(userInput, temp, getpid());
        }
    }

    // parse input
    char *token = strtok(userInput, " ");
    while (token != NULL) {
        // store output file
        if(strcmp(token, ">") == 0) {
            token = (strtok(NULL, " "));
            (*outputFile) = strdup(token);
            token = strtok(NULL, " ");
        }
        // store input file
        else if (strcmp(token, "<") == 0) {
            token = (strtok(NULL, " "));
            (*inputFile) = strdup(token);
            token = strtok(NULL, " ");
        }
        // add arguments from userInput to arguments array
        else {
            arguments[(*numArgs)] = strdup(token);
            (*numArgs)++;
            token = strtok(NULL, " ");
        }
    }

    // if last arg in arguments array is '&' isForeground set to true
    if (strcmp(arguments[(*numArgs) - 1], "&") == 0) {
        // remove '&' char from array
        (*numArgs)--;
        // set last arg to null
        arguments[*numArgs] = NULL;
        if (isForeground == false) {
            (*runBackground) = true;
        }
    }
}

/*
 * function to change directory
 * checks for invalid args and changed dir to specified or 'home'
 * returns void
 */
void changeDirectory(char *const *arguments, int numArgs) {
    char *dir;
    // if no arguments after cd, set dir to home directory
    if (arguments[1] == NULL) {
        dir = getenv("HOME");
        chdir(dir);
    }
    // set dir to specified directory in arguments[1]
    else {
        dir = arguments[1];
    }
    // change directory, print error if cant change dir or too many arguments
    if (chdir(dir) != 0) {
        if (numArgs > 2) {
            printf("smallsh: cd: too many arguments\n");
        }
        else {
            printf("smallsh: cd: No such file or directory\n");
        }
    }
}

/*
 * function to get status
 * prints the status of processes
 * returns void
 */
void getStatus(int childExitMethod) {
    // print exit status if process exited
    if (WIFEXITED(childExitMethod)) {
        printf("exit value %d\n", WEXITSTATUS(childExitMethod));
    }
    // print exit status if process terminated by SIGINT
    else {
        printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
    }
}

/*
 * function to redirect file input and descriptor
 * attempts to open input and descriptor files
 * error checking to determine if file is able to be opened/accessed
 * returns void
 */
void fileRedirect(const char *inputFile, const char *outputFile, bool runBackground, int fileNo) {
    if (!runBackground) {
        // if input file array is not null
        if (inputFile != NULL) {
            // attempt to open file
            fileNo = open(inputFile, O_RDONLY);
            // display error message if file cannot be opened
            if (fileNo == -1) {
                printf("smallsh: %s: No such file or directory\n", inputFile);
                fflush(stdout);
                _exit(1);
            }
            // change file descriptor display error if fails
            dup2(fileNo, 0);
            // close file descriptor when exec succeeds
            fcntl(fileNo, F_SETFD, FD_CLOEXEC);
        }
        // if output file string is not null
        if (outputFile != NULL) {
            // attempt to open output file
            fileNo = open(outputFile, O_WRONLY | O_TRUNC | O_CREAT, 0744);
            // display error message if file cannot be opened
            if (fileNo == -1) {
                printf("smallsh: %s: No such file or directory\n", outputFile);
                fflush(stdout);
                _exit(1);
            }
            // change file descriptor display error if fails
            dup2(fileNo, 1);
            // close file descriptor when exec succeeds
            fcntl(fileNo, F_SETFD, FD_CLOEXEC);
        }
    }
}

/*
 * function to handle sig stop invocation
 * prints message indicating mode
 * changes bool isForeground
 * returns void
 */
void handleSigStop() {
    if (isForeground == 0) {
        char* message = "Entering foreground-only mode (& is now ignored)\n: ";
        write(STDOUT_FILENO, message, 51);
        isForeground = true;
    } else {
        char* message = "Exiting foreground-only mode\n: ";
        write(STDOUT_FILENO, message, 31);
        isForeground = false;
    }
    fflush(stdout);
}