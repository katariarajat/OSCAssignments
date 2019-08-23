#include "history.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define max(x, y) (((x) >= (y)) ? (x) : (y))
#define maxStored 20

char* commandHistory[maxStored];
int storedCount = 0;
char fileName[] = "history.txt";

void retrieveStored() {
    int fd = open(fileName, O_CREAT);
    char buf[1000];
    read(fd, buf, 1000);
    storedCount = 0;

    char* ptr = strtok(buf, "\n");
    while (ptr) {
        commandHistory[storedCount++] = ptr;
        ptr = strtok(NULL, "\n");
    }

    close(fd);
}
void writeStored() {
    int fd = open(fileName, O_CREAT | O_TRUNC);

    for (int i = 0; i < storedCount; i++) {
        write(fd, commandHistory[i], strlen(commandHistory[i]));
        write(fd, "\n", 1);
    }

    close(fd);
}
void addNewCommand(char* cmd) {
    if (storedCount > 0 && strcmp(commandHistory[storedCount - 1], cmd) == 0) {
        return;
    }

    if (storedCount < maxStored) {
        commandHistory[storedCount++] = cmd;
    } else {
        for (int i = 0; i < maxStored - 1; i++) {
            commandHistory[i] = commandHistory[i + 1];
        }
        commandHistory[maxStored - 1] = cmd;
    }
    writeStored();
}

void printHistory(int n) {
    for (int i = storedCount - 1; i >= max(storedCount - n, 0); i--) {
        printf("%s\n", commandHistory[i]);
    }
}