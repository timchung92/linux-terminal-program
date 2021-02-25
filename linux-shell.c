#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "tokenizer.h"
#include <sys/wait.h>
#include <signal.h>
#include<fcntl.h>

pid_t childPid = 0;
pid_t childPid2 = 0;
int COMMAND_MAX = 10;

void executeShell();
void writeToStdout(char* text);
void sigintHandler(int sig);
char** getCommandFromInput();
void registerSignalHandlers();
void killChildProcess();
int checkPipes (char** args);
int validRedirects(char** args);
void printPennShell();
int handleRedirects(char** args);
void freeToks(char** args);
void parentWait();
void pipeSource(int fd[]);
void pipeDest(int fd[]);
int countArgs(char** args);
int pipeRidirectConflict(char** args, int argCnt);

/**
 * Main program execution
 */
int main( int argc, char *argv[] ) {
  registerSignalHandlers();
  while (1) {
    executeShell();
  }
  return 0;
}

/* Sends SIGKILL signal to a child process.
 * Error checks for kill system call failure and exits program if
 * there is an error */
void killChildProcess() {
    if (kill(childPid, SIGKILL) == -1) {
        perror("Error in kill");
        exit(EXIT_FAILURE);
    }
}
/* Signal handler for SIGINT. Catches SIGINT signal (e.g. Ctrl + C) and
 * kills the child process if it exists and is executing. Does not
 * do anything to the parent process and its execution */
void sigintHandler(int sig) {
    if (childPid != 0) {
        killChildProcess();
    }
}

void registerSignalHandlers() {
    if (signal(SIGINT, sigintHandler) == SIG_ERR) {
        perror("Error in signal");
        exit(EXIT_FAILURE);
    }
}

void printPennShell() {
    char minishell[] = "penn-sh> ";
    writeToStdout(minishell);
}

void executeShell() {
    childPid = 0;
    childPid2 = 0;
    char** args = getCommandFromInput();
    int secondCommand;
    
    if (args == NULL) return;
    int argCnt = countArgs(args);
    if (pipeRidirectConflict(args, argCnt) < 0) {
        return;
    }
    
    if ((secondCommand = checkPipes(args)) < 0) {  //Check for pipes 
        return;
    }

    int fd[2];
    
    if (secondCommand > 0) {  //Run if pipes
        pipe(fd);             
        pipeSource(fd);
        if (childPid) {
            pipeDest(fd); 
        }
    } else {   //Run if no pipes
        childPid = fork();
        childPid2 = -1;
        if (childPid < 0) {
            perror("Error in creating child process");
            exit(EXIT_FAILURE);
        }
    }
    
    //Code run by child processes
    if (childPid == 0 || childPid2 == 0) { 
        int commandIdx = 0;
        if (childPid && childPid2 == 0) {   //if second child process
            commandIdx = secondCommand;     //command is args[secondCommand]
        } 
        if (handleRedirects(&args[commandIdx]) < 0) {
            exit(EXIT_FAILURE);
        }
        if (execvp(args[commandIdx], &args[commandIdx]) == -1) {
            perror("Error in execve");
            exit(EXIT_FAILURE);
        }
    } else { //Code run by parent process
        parentWait();
        if (secondCommand > 0) {
            close(fd[0]); //close both pipe-ends for parent
            close(fd[1]);
            parentWait();
        }
    }
    freeToks(args);  
}

void parentWait() {
    int status;
    do {
        if (wait(&status) == -1) {
            perror("Error in child process termination");
            exit(EXIT_FAILURE);
        }
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
}

/* Method to fork first child process in a pipe */
void pipeSource(int fd[]) {
    childPid = fork();
    if (childPid < 0) {
        perror("Error in creating child process");
        exit(EXIT_FAILURE);
    }
    if (childPid == 0) {   //child process
        close(fd[0]);      //close read-end
        dup2(fd[1], STDOUT_FILENO); //set std-out to pipe write-end
    }
}

/* Method to fork second child process in a pipe */
void pipeDest(int fd[]) {
    childPid2 = fork();
    if (childPid2 < 0) {
        perror("Error in creating child process");
        exit(EXIT_FAILURE);
    }
    if (childPid2 == 0) {   //child process
        dup2(fd[0], STDIN_FILENO);  //set std-in to pipe read-end
        close(fd[1]);   //close pipe write-end
    }
}

/* Checks for valid redirects. Opens file descriptors.
   Updates args for execvp command */
int handleRedirects(char** args) {
    int newStdOut, newStdIn;
    int argsCount; //token count in args
    if ((argsCount = validRedirects(args)) < 0 ) {  //check valid # of redirects
        writeToStdout("Invalid redirection\n");     //if argsCount = -1, print err
        return -1;
    }

    for (int i = 0; i < argsCount; i ++) {  //for each token in args
        if (*args[i] == '>' ) {
            //Open fd for filename following '>' redirect
            if ((newStdOut = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
                perror("Invailid output redirect");
                return -1;
            }
            dup2(newStdOut, STDOUT_FILENO);
            close(newStdOut); 
            args[i] = NULL;   //Replace '>' with NULL for execvp
        } else if (*args[i] == '<' ) {
            //Open fd for filename following '<' redirect
            if ((newStdIn = open(args[i+1], O_RDONLY)) < 0 ) {
                perror("Invalid input redirect");
                return -1;
            }
            dup2(newStdIn, STDIN_FILENO);
            close(newStdIn);
            args[i] = NULL; //Replace '<' with NULL for execvp
            }
        }
    return 1;
}

/* Check for proper number of redirects
 * Returns token count if valid. Otherwise, -1 */
int validRedirects(char** args) {
    int count = 0, outputRedirects = 0, inputRedirects = 0;
    char* file1;
    char* file2;
    //Tracks input and output redirects, args, and filenames
    while (args[count] != NULL) {
        if (*args[count] == '<' ) {  
            if (inputRedirects == 0) {
                file1 = args[count + 1];
            } else {
                file2 = args[count + 1];
            }
            inputRedirects++;
        }
        if (*args[count] == '>') {
            if (outputRedirects == 0) {
                file1 = args[count + 1];
            } else {
                file2 = args[count + 1];
            }
            outputRedirects++;
        }
        count++;
    }

    //Check for 2 of the same redirects
    if (inputRedirects > 1 || outputRedirects > 1) {
        if (strcmp(file1, file2) != 0) {  //allow if same file names
            return -1;  //otherwise, return err
        }
    }
    return count;
}

/* Checks for pipes in args. If found, 
   returns index of the second command.
   Otherwise, returns 0 */
int checkPipes(char** args) {
    int secondCommand = 0;
    int i = 0;
    while (args[i] != NULL) {
        if (*args[i] == '|') {
            if (args[i+1] == NULL) {
                writeToStdout("Invalid pipe\n");
                return -1;
            }
            secondCommand = i + 1;
            free(args[i]);
            args[i] = NULL;
            return secondCommand;
        }
        i++;
    }
    return 0;
}

int pipeRidirectConflict(char** args, int argCnt) {
    for (int i = 0; i < argCnt; i++) {
        if (*args[i] == '|') {
            if (i + 2 >= argCnt) { continue; }
            if (*args[i + 2] == '<') {
                writeToStdout("Invalid double input\n");
                return -1;
             }
        }
        if (*args[i] == '>') {
            if (i + 2 >= argCnt) { continue; }
            if (*args[i + 2] == '|') {
                writeToStdout("Invalid double output\n");
                return -1;
             }
        }
    }
    return 0;
}

int countArgs(char** args) {
    int count = 0;
    while (args[count] != NULL) {
        count++;
    }
    return count++;
}

char** getCommandFromInput() {
    TOKENIZER *tokenizer;
    char string[256] = "";
    int br;
    int argsCnt = 0;

    printPennShell();
    
    string[255] = '\0';   /* ensure that string is always null-terminated */
    while ((br = read( STDIN_FILENO, string, 255 )) > 0) {
        if (br <= 1) {
        return NULL;
        }
        string[br-1] = '\0';   /* remove trailing \n */
        tokenizer = init_tokenizer( string );
        char** args = calloc(COMMAND_MAX, sizeof(char*));
        char* tok;
        while((tok = get_next_token( tokenizer )) != NULL ) {
            args[argsCnt] = tok;
            argsCnt++;
        }
        args[argsCnt] = NULL; 
        free_tokenizer( tokenizer );
        return args;
  }

  //Exit if CTRL+D
  writeToStdout("\n");
  exit(EXIT_SUCCESS);
  
  return NULL;
}

void writeToStdout(char* text) {
    if (write(STDOUT_FILENO, text, strlen(text)) == -1) {
        perror("Error in write");
        exit(EXIT_FAILURE);
    }
}

void freeToks(char** args) {
    for (int i = 0; i < COMMAND_MAX; i++) {
        free(args[i]);
    }
    free(args);
}
