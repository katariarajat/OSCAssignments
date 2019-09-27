#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include "cronjob.h"
#include "directory.h"
#include "history.h"
#include "nightswatch.h"
#include "pinfo.h"
#include "print.h"
#include "process.h"
#include "prompt.h"
#include "stringers.h"

#define stater struct termios

char* pendingNames[100];
int pendingIDs[100];
int pendingCount = 0;
int processpid = -1;

void execCommand(char* command);

void checkPending() {
    for (int i = 0; i < pendingCount; i++) {
        if (pendingIDs[i] == 0)
            continue;
        int st;
        int ret = waitpid(pendingIDs[i], &st, WNOHANG);

        if (ret == -1) {
            char buf[100];
            sprintf(buf, "Error checking status %d", pendingIDs[i]);
            perror(buf);
            continue;
        } else if (ret == 0) {
            continue;
        } else if (WIFEXITED(st)) {
            int ec = WEXITSTATUS(st);
            printf("Process %s with id %d exited ", pendingNames[i],
                   pendingIDs[i]);
            if (ec == 0) {
                printf("normally");
            } else {
                printf("with status %d", ec);
            }

            printf("\n");
        }
    }
    fflush(stdout);
}

int countPipes(const char* commands) {
    int i = 0, c = 0;
    while (commands[i]) {
        c += (commands[i] == '|');
        i++;
    }

    return c;
}

void handlePipelining(char* commands, int noOfCommands) {
    char *PIPE = "|", *cmd = strtok(commands, PIPE);
    int cmdIndex = 0, *oddPipe = (int*)malloc(sizeof(int) * 2),
        *evenPipe = (int*)malloc(sizeof(int) * 2),
        **pipes = (int**)malloc(sizeof(int*) * 2);
    pipes[0] = evenPipe;
    pipes[1] = oddPipe;
    int parity;
    char** allCmds = (char**)malloc(sizeof(char*) * 400);

    allCmds[0] = cmd;
    while ((cmd = strtok(NULL, PIPE))) {
        allCmds[++cmdIndex] = cmd;
    }
    cmdIndex = 0;

    while (cmdIndex < noOfCommands) {
        char* cmd = allCmds[cmdIndex];
        parity = cmdIndex & 1;
        if (pipe(pipes[parity]) < 0) {
            perror("Pipe couldn't init");
            return;
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("Couldn't fork in handlePipelining");
            return;
        } else if (pid == 0) {
            // configure IO
            if (cmdIndex == 0) {
                dup2(evenPipe[1], STDOUT_FILENO);
                // dup2(pipes[parity][1], STDOUT_FILENO);
            } else if (cmdIndex == noOfCommands - 1) {
                dup2(pipes[!parity][0], STDIN_FILENO);
            } else {
                dup2(pipes[parity][1], STDOUT_FILENO);
                dup2(pipes[!parity][0], STDIN_FILENO);
            }

            char* cmd2 = (char*)malloc(1000);
            memcpy(cmd2, cmd, 1000);

            execCommand(cmd2);
            exit(0);
        } else {
            wait(NULL);
            if (cmdIndex == 0) {
                close(evenPipe[1]);
            } else if (cmdIndex == noOfCommands - 1) {
                close(pipes[!parity][0]);
            } else {
                close(pipes[parity][1]);
                close(pipes[!parity][0]);
            }
            cmdIndex++;
        }
    }
}

char** tokenizeCommands(char* allCommandsString, int* commandsCountRef) {
    char** commands = (char**)malloc(100);
    for (int i = 0; i < 100; i++) {
        commands[i] = (char*)malloc(100);
    }
    char* delim = ";";

    int commandsCount = 0;
    char* ptr = strtok(allCommandsString, delim);
    while (ptr) {
        char* actual = trim(ptr);
        if (strlen(actual) != 0) {
            commands[commandsCount++] = actual;
        }
        ptr = strtok(NULL, delim);
    }

    *commandsCountRef = commandsCount;

    return commands;
}

void rawModeOn() {
    stater termas;
    tcgetattr(0, &termas);
    termas.c_lflag &= ~(ICANON | ECHO);  // Disable echo as well
    tcsetattr(0, TCSANOW, &termas);
}

void rawModeGone() {
    stater termas;
    tcgetattr(0, &termas);
    termas.c_lflag |= ICANON | ECHO;
    tcsetattr(0, TCSANOW, &termas);
}

int keyboardWasPressed() {
    int bytesarewaitingforme;
    rawModeOn();
    ioctl(0, FIONREAD, &bytesarewaitingforme);
    int ans = bytesarewaitingforme > 0;
    rawModeGone();
    return ans;
}

void handleArrows(char* command) {
    char* cmd2 = (char*)malloc(strlen(command));
    memcpy(cmd2, command, strlen(command) + 1);
    int offset = 0;
    for (int i = 2; i < (int)strlen(command); i += 3) {
        offset += (command[i] == 'A' ? 1 : -1);
    }

    // offset > 0 denotes how many history items we have to go up into
    if (offset < 0 || storedCount < offset) {
        printf("Command doesn't exist");
    } else {
        command = commandHistory[storedCount - offset];
        memcpy(cmd2, command, strlen(command) + 1);
        printPrompt();
        printf("%s\n", cmd2);
        execCommand(cmd2);
    }
}

void execCommand(char* command) {
    // if command is just a combo of up arrows and down arrows
    if (command[0] == 27) {
        handleArrows(command);
        return;
    }

    int cmdLength = strlen(command);
    char* cmd2 = (char*)malloc(cmdLength);
    char* cmd3 = (char*)malloc(cmdLength);
    memcpy(cmd2, command, strlen(command) + 1);
    memcpy(cmd3, command, strlen(command) + 1);
    addNewCommand(cmd2);

    int c = countPipes(cmd2);
    if (c) {
        handlePipelining(cmd2, c + 1);
        return;
    }

    int hasInput = 0, hasOutput = 0, hasAppend = 0;
    char *inp = (char*)calloc(0, 0), *outp = (char*)calloc(0, 0),
         *append = (char*)calloc(0, 0);

    // first parse main command and all its args
    char* delim = "\t ";
    char** args = (char**)malloc(100);
    int argCount = 0;
    int firstCall = 1;

    while (1) {
        char* arg;
        if (firstCall) {
            arg = strtok(command, delim);
            firstCall = 0;
        } else {
            arg = strtok(NULL, delim);
        }
        if (!arg)
            break;
        if (!strcmp(arg, "<")) {
            hasInput = 1;
            continue;
        }
        if (hasInput) {
            inp = arg;
            hasInput = 0;
            continue;
        }
        if (!strcmp(arg, ">")) {
            hasOutput = 1;
            continue;
        }
        if (hasOutput) {
            outp = arg;
            hasOutput = 0;
            continue;
        }
        if (!strcmp(arg, ">>")) {
            hasAppend = 1;
            continue;
        }
        if (hasAppend) {
            append = arg;
            hasAppend = 0;
            continue;
        }
        args[argCount++] = arg;
    }
    for (int i = 0; i < argCount; i++) {
        args[i] = trim(args[i]);
    }

    // actual args begin from 1
    char* cmd = args[0];

    int isBackgroundJob = 0;
    for (int i = 0; i < argCount; i++) {
        if (strcmp(args[i], "&") == 0) {
            // remove & from arguments
            for (int j = i + 1; j < argCount; j++) {
                args[j - 1] = args[j];
            }
            args[argCount - 1] = NULL;
            argCount--;
            isBackgroundJob = 1;
            break;
        }
    }

    int fd1 = -1, fd2 = -1;
    // save original file descriptors for later restoration
    int fdin = dup(0);
    int fdout = dup(1);
    if (inp && strlen(inp)) {
        fd1 = open(inp, O_RDONLY, 0644);
        if (fd1 < 0) {
            perror("Opening < file");
            return;
        }
        dup2(fd1, STDIN_FILENO);
    }
    if (outp && strlen(outp)) {
        fd2 = open(outp, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd2 < 0) {
            perror("Opening > file");
            return;
        }
        dup2(fd2, STDOUT_FILENO);
    }
    if (append && strlen(append)) {
        fd2 = open(append, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (fd2 < 0) {
            perror("Opening >> file");
            return;
        }
        dup2(fd2, STDOUT_FILENO);
    }

    if (!strcmp(cmd, "ls")) {
        char* dir = ".";
        int hiddenShow = 0, longlist = 0;

        for (int i = 1; i < argCount; i++) {
            char* arg = args[i];
            if (strlen(arg) == 0)
                continue;
            if (arg[0] == '-') {
                for (int j = 1; j < (int)strlen(arg); j++) {
                    switch (arg[j]) {
                        case 'a':
                            hiddenShow = 1;
                            break;
                        case 'l':
                            longlist = 1;
                            break;
                    }
                }
            } else if (strcmp("&", arg)) {
                dir = arg;
            }
        }
        ls(dir, hiddenShow, longlist);
    } else if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit") ||
               !strcmp(cmd, "q")) {
        exit(0);
    } else if (!strcmp(cmd, "cd")) {
        char* target = "~";
        if (argCount > 1)
            target = args[1];
        changeDirectory(target);
    } else if (!strcmp(cmd, "pwd")) {
        printPWD(1, 1);
    } else if (!strcmp(cmd, "echo")) {
        char* finalArg = (char*)malloc(1000);
        int len = 0;
        for (int i = 1; i < argCount; i++) {
            char* arg = args[i];
            for (int j = 0; j < (int)strlen(arg); j++) {
                finalArg[len++] = arg[j];
            }
            finalArg[len++] = ' ';
        }
        finalArg[len] = 0;
        // join all args by spaces and then print :(
        echo(finalArg, len);
    } else if (!strcmp(cmd, "pinfo")) {
        printPinfo(argCount, args);
    } else if (!strcmp(cmd, "history")) {
        int displayCount = 10;

        if (argCount == 2) {
            displayCount = atoi(args[1]);
        }

        printHistory(displayCount);
    } else if (!strcmp(cmd, "nightswatch")) {
        int takeInterval = 0, interval = -1;

        for (int i = 1; i < argCount; i++) {
            if (!strcmp(args[i], "-n")) {
                takeInterval = 1;
                continue;
            }
            if (takeInterval) {
                interval = atoi(args[i]);
                takeInterval = 0;
            } else if (interval == -1) {
                printf("Did not specify interval");
            } else {
                int printValue = 0;
                if (!strcmp(args[i], "dirty")) {
                    printValue = 1;
                }
                nightsprint(printValue, interval);
            }
        }

    } else if (!strcmp(cmd, "jobs")) {
        for (int i = 0; i < pendingCount; i++) {
            char* pid = (char*)malloc(100);
            snprintf(pid, 100, "%d", pendingIDs[i]);

            // check status of process here
            char* status = getProcStatusString(pid);

            printf("[%d] %s %s [%s]\n", i + 1, status, pendingNames[i], pid);
        }
    } else if (!strcmp(cmd, "kjob")) {
        if (argCount < 3) {
            printf("Error: usage kjob <jobNumber> <signalNumber>\n");
            return;
        }
        union sigval;

        int pid = pendingIDs[atoi(args[1]) - 1], signalNumber = atoi(args[2]);

        kill(pid, signalNumber);
    } else if (!strcmp(cmd, "setenv")) {
        if (argCount < 2 || argCount > 3) {
            printf("Invalid syntax! Should be: setenv ​ var [value]\n");
            return;
        }

        char* var = args[1];
        char* value = "";

        if (argCount > 2) {
            value = (char*)malloc(1000);
            strcat(value, args[2]);
        }

        strcat(var, "=");
        strcat(var, value);
        putenv(var);
    } else if (!strcmp(cmd, "unsetenv")) {
        if (argCount != 2) {
            printf("Invalid syntax! Should be: unsetenv​ var\n");
            return;
        }

        char* var = args[1];
        unsetenv(var);
    } else if (!strcmp(cmd, "getenv")) {
        if (argCount != 2) {
            printf("Invalid syntax! Should be: getenv ​var\n");
            return;
        }

        char *var = args[1], *ans = getenv(var);
        if (ans)
            printf("%s\n", ans);
        else
            printf("var %s not found\n", var);
    } else if (!strcmp(cmd, "fg")) {
        if (argCount != 2) {
            printf("Usage: fg <jobNumber>\n");
            return;
        }

        int jobn = atoi(args[1]);

        if (jobn > pendingCount || jobn <= 0) {
            printf("Invalid job number\n");
            return;
        }

        int pid = pendingIDs[jobn - 1];
        // group1
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        tcsetpgrp(0, __getpgid(pid));
        kill(pid, SIGCONT);

        waitpid(pid, NULL, WUNTRACED);

        tcsetpgrp(0, getpid());
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        // group2
        // setpgid(pid, getpgrp());
        // kill(pid, SIGCONT);
        pendingIDs[jobn - 1] = 0;

        // similar to that of process.c
        // if (WIFSTOPPED(st)) {
        //     // store in jobs array
        // }
    } else if (!strcmp(cmd, "bg")) {
        if (argCount != 2) {
            printf("Usage: bg <jobNumber>\n");
            return;
        }

        int jobn = atoi(args[1]);

        if (jobn > pendingCount || jobn <= 0) {
            printf("Invalid job number\n");
            return;
        }

        int pid = pendingIDs[jobn - 1];
        kill(pid, SIGTTIN);
        kill(pid, SIGCONT);
    } else if (!strcmp(cmd, "overkill")) {
        for (int i = 0; i < pendingCount; i++) {
            if (!pendingIDs[i])
                kill(pendingIDs[i], 9);
        }

        checkPending();
    } else if (!strcmp(cmd, "cronjob")) {
        cronjobParse(args);
    } else {
        int idOfChild = execProcess(cmd, args, isBackgroundJob);

        if (idOfChild != -1) {
            pendingNames[pendingCount] = cmd2;
            pendingIDs[pendingCount] = idOfChild;
            pendingCount++;
        }
    }
    if (fd1 >= 0) {
        close(fd1);
    }
    if (fd2 >= 0) {
        close(fd2);
    }
    if (inp && strlen(inp)) {
        dup2(fdin, STDIN_FILENO);
    }
    if ((outp && strlen(outp)) || (append && strlen(append))) {
        dup2(fdout, STDOUT_FILENO);
    }
}