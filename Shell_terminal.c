#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_COMMAND_LENGTH 100 // Maximum lenght for a command input
#define MAX_ARGS 10   // maximum number of agrs for a command
#define HISTORY_SIZE 10 // number of commands for history

char *history[HISTORY_SIZE]; // array to store history of commands
int current_history_size = 0; // current size to change for history


//function declarations
void add_to_history(const char *command);
void print_history();
char** parse_command(char *command, int *background);
void execute_system_command(char *args[], int background);
void handle_pipe(char *input);
void handle_logical_and(char *input);
void handle_redirection_and_commands(char *input);
void parse_and_execute(char *input);


//Adds previous commands to history array
void add_to_history(const char *command) {
    int index = current_history_size % HISTORY_SIZE;
    if (history[index] != NULL) {
        free(history[index]);
    }
    history[index] = strdup(command);
    current_history_size++;
}


//prints history array
void print_history() {
    int start = current_history_size > HISTORY_SIZE ? current_history_size - HISTORY_SIZE : 0;
    for (int i = start; i < current_history_size; i++) {
        printf("[%d] %s\n", i - start + 1, history[i % HISTORY_SIZE]);
    }
}


//parses the input command and separates args, checks for bg execution
char** parse_command(char *command, int *background) {
    static char *args[MAX_ARGS + 1]; //arraay to hold args
    int i = 0;
    char *next_token = command; 
    char *current_token;
    while ((current_token = strtok_r(next_token, " ", &next_token)) && i < MAX_ARGS) {
        if (strcmp(current_token, "&") == 0) {
            *background = 1;
            continue;  // Skip adding '&' to args
        }
        // Handle strings enclosed in quotes
        if (current_token[0] == '\"') {
            current_token++;  // Skip the opening quote
            char *end_quote = strchr(current_token, '\"');
            if (end_quote) {
                *end_quote = '\0';  // Remove the closing quote
            } else {
                // Handle case where closing quote is in a subsequent token
                char *temp_token = strtok_r(NULL, "\"", &next_token);
                strcat(current_token, " ");
                strcat(current_token, temp_token);
            }
        }
        args[i++] = current_token;
    }
    args[i] = NULL;
    return args;
}


//executes non built in commands
void execute_system_command(char *args[], int background) {
    pid_t pid = fork();//creating child process
    if (pid == 0) { //child process
        if (execvp(args[0], args) == -1) {
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
    } else {
        if (!background) {
            waitpid(pid, NULL, 0); // wait for the child process
        } else {
            printf("Process %d running in background\n", pid);
        }
    }
}


//handles pipeing operations
void handle_pipe(char *input) {
    int fds[2];
    pipe(fds); //create pipe

    char *part1 = strtok(input, "|");
    char *part2 = strtok(NULL, "");

    pid_t pid1 = fork();
    if (pid1 == 0) { //left of pipe
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);

        int background = 0;
        char **args = parse_command(part1, &background);
        execvp(args[0], args);
        perror("execvp left failed");
        exit(EXIT_FAILURE);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) { //right of pipe
        dup2(fds[0], STDIN_FILENO);
        close(fds[1]);
        close(fds[0]);

        int background = 0;
        char **args = parse_command(part2, &background);
        execvp(args[0], args);
        perror("execvp right failed");
        exit(EXIT_FAILURE);
    }

    close(fds[0]); //close unused file descriptions
    close(fds[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

//executes commands with &&
void handle_logical_and(char *input) {
    char *first_command = strtok(input, "&&");
    char *second_command = strtok(NULL, "");

    int status;
    pid_t pid = fork();
    if (pid == 0) { //left part of && operator
        int background = 0;
        char **args = parse_command(first_command, &background);
        if (execvp(args[0], args) == -1) {
            perror("Failed to execute first part of &&");
            exit(EXIT_FAILURE);
        }
    } else {
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            parse_and_execute(second_command); //left part
        }
    }
}

// ">" and ">> handling"
void handle_redirection_and_commands(char *input) {
    char *command = strtok(input, ">");
    char *rest = strtok(NULL, "");      

    if (rest) {
        int append_mode = (rest[0] == '>'); 
        char *filename = strtok(append_mode ? rest + 1 : rest, " "); 

        int fd;
        if (append_mode) {
            // Append mode
            fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        } else {
            // Overwrite mode
            fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }

        if (fd == -1) {
            perror("Failed to open file");
            return;
        }

        int stdout_copy = dup(STDOUT_FILENO);
        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("Failed to redirect stdout");
            return;
        }
        close(fd);

        parse_and_execute(command);

        dup2(stdout_copy, STDOUT_FILENO); // Restore STDOUT
        close(stdout_copy);
    } else {
        // No redirection, execute command normally
        parse_and_execute(command);
    }
}



//main interpretion and  execution of commands
void parse_and_execute(char *input) {
    if (strstr(input, "&&")) {
        handle_logical_and(input);
    } else if (strstr(input, "|")) {
        handle_pipe(input);
    } else if (strstr(input, ">") || strstr(input, ">>")) {
        handle_redirection_and_commands(input);
    } else {
        int background = 0;
        char **args = parse_command(input, &background);
        if (strcmp(args[0], "cd") == 0) {
            if (args[1]) {
                if (chdir(args[1]) == -1) {
                    perror("chdir failed");
                }
            } else {
                fprintf(stderr, "cd: expected argument\n");
            }
        } else if (strcmp(args[0], "pwd") == 0) {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                printf("%s\n", cwd);
            } else {
                perror("getcwd failed");
            }
        } else if (strcmp(args[0], "exit") == 0) {
            exit(0);
        } else if (strcmp(args[0], "history") == 0) {
            print_history();
        } else {
            execute_system_command(args, background);
        }
    }
}


// main function, the entry point
int main() {
    char input[MAX_COMMAND_LENGTH];
    memset(history, 0, sizeof(history)); // initialize history array

    while (1) {
        printf("myshell> ");
        if (!fgets(input, sizeof(input), stdin)) break;
        if (input[strlen(input) - 1] == '\n') {
            input[strlen(input) - 1] = '\0'; // remove newline at end
        }
        add_to_history(input);
        parse_and_execute(input);
    }

    // Cleaning the allocated memory
    for (int i = 0; i < HISTORY_SIZE; i++) {
        if (history[i]) free(history[i]);
    }
    return 0;
}
