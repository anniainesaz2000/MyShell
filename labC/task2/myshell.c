#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include "LineParser.h"
#include <stdbool.h>

#define TERMINATED  -1
#define RUNNING 1
#define SUSPENDED 0

#define HISTLEN 20
#define MAX_BUF 200


typedef struct process{
    cmdLine* cmd;         /* the parsed command line*/
    pid_t pid; 		      /* the process id that is running the command*/
    int status;           /* status of the process: RUNNING/SUSPENDED/TERMINATED */
    struct process *next;  /* next process in chain */
    struct process *prev;  /* previous process in chain */
} process;

//functions declerations
process* addProcess(process** process_list, cmdLine* cmd, pid_t pid);
void printProcessList(process** process_list);
void updateProcessList(process **process_list);
void updateProcessStatus(process* process_list, int pid, int status);
void deleteTerminated();
void freeCmdLine(cmdLine *pCmdLine);

//Global variables
process *process_list = NULL;
char history[HISTLEN][MAX_BUF];
int newest = 0;
int oldest = 0;
int history_count = 0;

process* addProcess(process **p_list, cmdLine *cmd, pid_t pid) {
    process* newProcess = (process*)malloc(sizeof(process));
    if (newProcess == NULL) {
        perror("Failed to allocate memory for new process");
        exit(EXIT_FAILURE);
    }
    newProcess->cmd = cmd;
    newProcess->pid = pid;
    newProcess->status = RUNNING;
    newProcess->next = process_list;
    newProcess->prev = NULL;

    if(process_list != NULL){
        process_list->prev = newProcess; 
    }

    return newProcess;
   
}


void printProcessList(process **p_list) {
    updateProcessList(&process_list);
    process *current = process_list;
    int index = 0;

    printf("Index  PID       Command       STATUS\n");
    while (current) {
        printf("%-6d %-10d %-12s %-10s\n",
               index++,
               current->pid,
               current->cmd->arguments[0],
               current->status == RUNNING ? "Running" :
               current->status == SUSPENDED ? "Suspended" : "Terminated");

        current = current->next;
    }

    deleteTerminated();
}


void updateProcessList(process **p_list){
    process *current = process_list;
    pid_t pid;
    int status;


    while (current) {
        printf("got to loop \n");

        pid = waitpid(current->pid, &status, WNOHANG);
        
        if (pid == 0) {
            // No child process has changed state yet
        } else if (pid > 0) {
            printf("got to pid>0 \n");
            // A child process has changed state, pid holds the child's PID
            if (WIFEXITED(status)) {
                // Child exited normally
                printf("got to exited \n");
                current->status = TERMINATED;
            } else if (WIFSIGNALED(status)) {
                // Child terminated due to a signal
                printf("got to exited2 \n");
                current->status = TERMINATED;
            } else if (WIFSTOPPED(status)) {
                // Child stopped by a signal
                printf("got to stop \n");
                current->status = SUSPENDED;
            } else if (WIFCONTINUED(status)) {
                // Child process continued
                printf("got to continue \n");
                current->status = RUNNING;
            }
        } else {
            // Error in waitpid
            current->status = TERMINATED;
            //perror("waitpid");
        }
        current = current->next;
    }

}

void updateProcessStatus(process* p_list, int pid, int status){

    process *current = process_list;
    bool done = false;

    while (current && !done) {
        if(current->pid == pid){
            current->status = status;
            done = true;
        }
        current = current->next;
    }
    
}

void deleteTerminated(){

    process *current = process_list;

    while (current) {
        if(current->status == TERMINATED){
            process *next_item = current->next;
            if(current->prev != NULL && next_item != NULL){
                current->prev->next = next_item;
                next_item->prev = current->prev;
            }else if(current->prev != NULL){
                current->prev->next = NULL;
            }else if(next_item != NULL){
                 next_item->prev = NULL;
                 process_list = next_item;
            }else{
                process_list = NULL;
            }

            current->prev = NULL;
            current->next = NULL;
            //freeCmdLine(current->cmd);
            current = next_item;

        }else{
            current = current->next;
        }
        
    }
    
}

void add_to_history(const char *cmd) {
    strncpy(history[newest], cmd, MAX_BUF);
    history[newest][MAX_BUF - 1] = '\0';  // Ensure null-terminated string
    newest = (newest + 1) % HISTLEN;
    if (history_count < HISTLEN) {
        history_count++;
    } else {
        oldest = (oldest + 1) % HISTLEN;
    }
}

void print_history() {
    printf("History:\n");
    for (int i = 0; i < history_count; i++) {
        int index = (oldest + i) % HISTLEN;
        printf("%d: %s\n", i + 1, history[index]);
    }
}

char* get_history_command(int n) {
    if (n <= 0 || n > history_count) {
        printf("Invalid history index\n");
        return NULL;
    }
    int index = (oldest + n - 1) % HISTLEN;
    return history[index];
}



void freeCmdLine(cmdLine *pCmdLine) {
    while (pCmdLine != NULL) {
        for (int i = 0; i < pCmdLine->argCount; i++) {
            if (pCmdLine->arguments[i] != NULL) {
                free(pCmdLine->arguments[i]);
            }
        }

        if (pCmdLine->inputRedirect != NULL) {
            free((char *)pCmdLine->inputRedirect);
        }

        if (pCmdLine->outputRedirect != NULL) {
            free((char *)pCmdLine->outputRedirect);
        }

        cmdLine *nextCmdLine = pCmdLine->next;
        free(pCmdLine); // Free the command line structure itself

        pCmdLine = nextCmdLine;
    }
}


void freeProcessList(process *p_list) {
    while (p_list != NULL) {
        process *next = p_list->next;
        freeCmdLine(p_list->cmd);
        free(p_list);
        p_list = next;
    }
    process_list = NULL; // Reset the process list pointer
}


void execute2childs(cmdLine *pCmdLine1, cmdLine *pCmdLine2) {
    int fd[2];
    pid_t pid;

    // Create the pipe
    if (pipe(fd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    // Fork the first child process
    pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // Child 1 process: Set up input redirection and pipe to Child 2
        if (pCmdLine1->inputRedirect != NULL) {
            freopen(pCmdLine1->inputRedirect, "r", stdin);
        }

        close(fd[0]); // Close unused read end
        dup2(fd[1], STDOUT_FILENO); // Redirect stdout to pipe
        close(fd[1]); // Close original write end

        if (execvp(pCmdLine1->arguments[0], pCmdLine1->arguments) == -1) {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    } else {
        // Fork the second child process
        pid_t pid2 = fork();
        if (pid2 == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid2 == 0) {
            // Child 2 process: Set up pipe from Child 1 and output redirection
            if (pCmdLine2->outputRedirect != NULL) {
                freopen(pCmdLine2->outputRedirect, "w", stdout);
            }

            close(fd[1]); // Close unused write end
            dup2(fd[0], STDIN_FILENO); // Redirect stdin from pipe
            close(fd[0]); // Close original read end

            if (execvp(pCmdLine2->arguments[0], pCmdLine2->arguments) == -1) {
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        } else {
            // Parent process: Close pipe and wait for both children to finish
            process_list = addProcess(&process_list, pCmdLine1, pid);
            close(fd[0]);
            close(fd[1]);
            waitpid(pid, NULL, 0);
            waitpid(pid2, NULL, 0);
        }
    }
}


void execute(cmdLine *pCmdLine, char* buffer) {
    char *pipe_pos = strchr(buffer, '|');

    if(pipe_pos != NULL) {
        // Calculate the length of the command before and after the pipe
        size_t before_pipe_len = pipe_pos - buffer;

        // Allocate memory for the two commands (+1 for the null terminator)
        char *before_pipe = (char *)malloc(before_pipe_len + 1);

        if (before_pipe == NULL) {
            fprintf(stderr, "Error: Memory allocation failed.\n");
            return;
        }


        // Copy the parts into the new strings
        strncpy(before_pipe, buffer, before_pipe_len);
        before_pipe[before_pipe_len] = '\0';  // Null-terminate the string

        char * index = pipe_pos + 1;
        while(strncmp(index, " ", 1) == 0){
            index++;
        }

        size_t after_pipe_len = strlen(index + 1);
        char *after_pipe = (char *)malloc(after_pipe_len + 1);

         if (after_pipe == NULL) {
            fprintf(stderr, "Error: Memory allocation failed.\n");
            return;
        }

        strcpy(after_pipe, index);  // Skip the '|' character and copy the rest
        while (*after_pipe == ' ') after_pipe++;  // Skip leading spaces in after_pipe
        

        cmdLine *pCmdLine1 = parseCmdLines(before_pipe);
        cmdLine *pCmdLine2 = parseCmdLines(after_pipe);

        execute2childs(pCmdLine1, pCmdLine2);

        // Free allocated memory
        // freeCmdLine(pCmdLine1);
        // freeCmdLine(pCmdLine2);
        free(before_pipe);
        free(after_pipe);

    } else {
        // Normal execution without pipe
        pid_t pid = fork();  // Create a new process

        if (pid == -1) {
            perror("Error forking");
            exit(1);
        } else if (pid == 0) {
            // In the child process
            fprintf(stderr, "PID: %d Executing command: %s \n", pid, buffer); //task1a

            //task3
            if(pCmdLine->inputRedirect != NULL){
                freopen(pCmdLine->inputRedirect, "r", stdin);
            }

            if(pCmdLine->outputRedirect != NULL){
                freopen(pCmdLine->outputRedirect, "w", stdout);
            }

            if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1) {// I checked in the Internet what are execv's arguments
                perror("Error executing command");
                exit(1);
            }

        } else {
            // In the parent process
            process_list = addProcess(&process_list, pCmdLine, pid);
            if(pCmdLine->blocking == 1){
                waitpid(pid, NULL, 0);  // Wait for the child process to complete, task1b
            }
        }
    }

    //freeCmdLine(pCmdLine);
}



int debug(int argc, char *argv[]){

    int debug_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            debug_mode = 1; // Turn on debug mode
            break;
        } 
    }

    return debug_mode;

}

int main(int argc, char *argv[]) {

    int max_input_size = 2048;
    char cwd[PATH_MAX];
    char buffer[max_input_size];
    int debug_mode = debug(argc, argv);

    while (strcmp(buffer, "quit\n") != 0) {

        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("Current working directory: %s\n", cwd);
        } else {
            perror("getcwd() error");
        }

        if (fgets(buffer, max_input_size, stdin) != NULL) {
            add_to_history(buffer);

            if (strncmp(buffer, "history", 7) == 0) {
                print_history();
                continue;
            } else if (strcmp(buffer, "!!\n") == 0) {
                char* last_cmd = get_history_command(history_count - 1);
                if (last_cmd) {
                    strncpy(buffer, last_cmd, max_input_size);
                    buffer[max_input_size - 1] = '\0';
                    printf("Executing: %s", buffer);
                } else {
                    continue;
                }
            } else if (buffer[0] == '!' && buffer[1] != '!') {
                int n = atoi(&buffer[1]);
                char* nth_cmd = get_history_command(n);
                if (nth_cmd) {
                    strncpy(buffer, nth_cmd, max_input_size);
                    buffer[max_input_size - 1] = '\0';
                    printf("Executing: %s", buffer);
                } else {
                    printf("command failed!");
                    continue;
                }
            }

            cmdLine *parsedLine = parseCmdLines(buffer);
            if (strcmp(parsedLine->arguments[0], "cd") == 0) {  // task1c
                if (chdir(parsedLine->arguments[1]) == -1) {
                    fprintf(stderr, "cd operation failed");
                }
                process_list = addProcess(&process_list, parsedLine, getpid());
            } else if (strcmp(parsedLine->arguments[0], "alarm") == 0) {  // task2
                if (kill(atoi(parsedLine->arguments[1]), SIGCONT) == 0) {
                    printf("alarm succeeded!\n");
                } else {
                    printf("alarm failed!\n");
                }
                process_list = addProcess(&process_list, parsedLine, getpid());
                updateProcessStatus(process_list, atoi(parsedLine->arguments[1]), RUNNING);
            } else if (strcmp(parsedLine->arguments[0], "blast") == 0) {  // task2
                if (kill(atoi(parsedLine->arguments[1]), SIGINT) == 0) {
                    printf("blast succeeded!\n");
                } else {
                    printf("blast failed!\n");
                }
                process_list = addProcess(&process_list, parsedLine, getpid());
                updateProcessStatus(process_list, atoi(parsedLine->arguments[1]), TERMINATED);
            } else if (strcmp(parsedLine->arguments[0], "sleep") == 0) {
                if (kill(atoi(parsedLine->arguments[1]), SIGTSTP) == 0) {
                    printf("sleep succeeded!\n");
                } else {
                    printf("sleep failed!\n");
                }
                process_list = addProcess(&process_list, parsedLine, getpid());
                updateProcessStatus(process_list, atoi(parsedLine->arguments[1]), SUSPENDED);
            } else if (strcmp(parsedLine->arguments[0], "procs") == 0) {
                printProcessList(&process_list);
            } else {
                execute(parsedLine, buffer);
            }
        } else {
            perror("fgets error");
        }

        if (debug_mode == 1) {
            fprintf(stderr, "Debug: %s", buffer);
        }

    }

    freeProcessList(process_list);
    exit(0);
    return 0;
}



