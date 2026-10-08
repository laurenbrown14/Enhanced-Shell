#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_LINE 1024
#define MAX_ARGS 64
#define MAX_HISTORY 10

char *history[MAX_HISTORY];
int hist_count = 0;

void add_history(const char *cmd) {
    if (hist_count < MAX_HISTORY) {
        history[hist_count++] = strdup(cmd);
    } else {
        free(history[0]);
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            history[i] = history[i + 1];
        }
        history[MAX_HISTORY - 1] = strdup(cmd);
    }
}

int run_single(char *cmd_str, int in_fd, int out_fd) {
    char *args[MAX_ARGS];
    char *input = NULL;
    char *output = NULL;
    int force = 0;
    int i = 0;

    char *token = strtok(cmd_str, " \t");
    while (token != NULL) {
        if (strcmp(token, "<") == 0) {
            input = strtok(NULL, " \t");
        } else if (strcmp(token, ">!") == 0) {
            output = strtok(NULL, " \t");
            force = 1;
        } else if (strcmp(token, ">") == 0) {
            output = strtok(NULL, " \t");
            force = 0;
        } else {
            args[i++] = token;
        }
        token = strtok(NULL, " \t");
    }
    args[i] = NULL;
    if (args[0] == NULL)
        return 0;

    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0) {
        if (in_fd != STDIN_FILENO) {
            dup2(in_fd, STDIN_FILENO);
            close(in_fd);
        } else if (input) {
            int fd = open(input, O_RDONLY);
            if (fd < 0) { fprintf(stderr, "myshell: no such file: %s\n", input); exit(1); }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        if (out_fd != STDOUT_FILENO) {
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        } else if (output) {
            int fd;
            if (!force && access(output, F_OK) == 0) {
                fprintf(stderr, "myshell: file already exists: %s\n", output);
                exit(1);
            }
            fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror("open failed"); exit(1); }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        execvp(args[0], args);
        fprintf(stderr, "myshell: command not found: %s\n", args[0]);
        exit(1);
    }

    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int execute_pipeline(char *line) {
    char *cmds[MAX_ARGS];
    int num_cmds = 0;

    char *token = strtok(line, "|");
    while (token != NULL && num_cmds < MAX_ARGS) {
        cmds[num_cmds++] = token;
        token = strtok(NULL, "|");
    }

    if (num_cmds == 1) {
        return run_single(cmds[0], STDIN_FILENO, STDOUT_FILENO);
    }

    int pipefds[2 * (num_cmds - 1)];
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipefds + i * 2) < 0) return -1;
    }

    int success = 0;
    for (int i = 0; i < num_cmds; i++) {
        int in = (i == 0) ? STDIN_FILENO : pipefds[(i - 1) * 2];
        int out = (i == num_cmds - 1) ? STDOUT_FILENO : pipefds[i * 2 + 1];

        pid_t p = fork();
        if (p < 0) {
            success = -1;
            break;
        }
        if (p == 0) {
            if (in != STDIN_FILENO) { 
                dup2(in, STDIN_FILENO); 
            }
            if (out != STDOUT_FILENO) { 
                dup2(out, STDOUT_FILENO); 
            }
            for (int j = 0; j < 2 * (num_cmds - 1); j++) {
                close(pipefds[j]);
            }
            
            exit(run_single(cmds[i], STDIN_FILENO, STDOUT_FILENO));
        }
    }

    for (int i = 0; i < 2 * (num_cmds - 1); i++) {
        close(pipefds[i]);
    }
    
    for (int i = 0; i < num_cmds; i++) { 
        int status; 
        wait(&status); 
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            success = -1; 
        }
    }
    return success;
}

int main() {
    char input[MAX_LINE];

    while (1) {
        printf("myshell> ");
        fflush(stdout);

        if (!fgets(input, MAX_LINE, stdin)) break;
        input[strcspn(input, "\r\n")] = 0;
        if (strlen(input) == 0) continue;

        if (input[0] == '!') {
            int n = atoi(&input[1]);
            if (n >= 1 && n <= hist_count) {
                strcpy(input, history[hist_count - n]);
                printf("%s\n", input);
            } else {
                fprintf(stderr, "myshell: event not found\n");
                continue;
            }
        }

        if (strcmp(input, "quit") == 0) break;

        if (strcmp(input, "history") == 0) {
            for (int i = 0; i < hist_count; i++) {
                printf("[%2d] %s\n", hist_count - i, history[i]);
            }
            continue;
        }

        char line_copy[MAX_LINE];
        strcpy(line_copy, input);
        if (execute_pipeline(input) == 0) {
            add_history(line_copy);
        }
    }

    for (int i = 0; i < hist_count; i++) free(history[i]);
    return 0;
}
