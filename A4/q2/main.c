#include "main.h"
#include <sys/shm.h>
#include "table.h"

int genRandomInRange(int l, int r) {
    return rand() % (r - l + 1) + l;
}

char *getTimestamp() {
    time_t t;
    time(&t);
    char *t2 = ctime(&t);

    char *buf = (char *)calloc(sizeof(char), 9);
    memcpy(buf, t2 + 11, 8);
    return buf;
}

char *getHeader(int type, int id) {
    char *buf = (char *)calloc(sizeof(char), 1000);
    strcat(buf, getTimestamp());
    strcat(buf, "\t");

    switch (type) {
        case ROBOT_TYPE:
            strcat(buf, KGREEN "Robot");
            break;
        case TABLE_TYPE:
            strcat(buf, KMAGENTA "Table");
            break;
        case STUDENT_TYPE:
            strcat(buf, KBLUE "Student");
            break;
    }

    char b2[10] = {0};
    // alignment woes
    if (type == STUDENT_TYPE)
        sprintf(b2, " %d\t" KNRM, id + 1);
    else
        sprintf(b2, " %d\t\t" KNRM, id + 1);
    strcat(buf, b2);
    return buf;
}

void printMsg(int type, int id, char *fmt, va_list arg) {
    char *buf = (char *)calloc(sizeof(char), 1000);
    char buf2[1000] = {0};
    vsprintf(buf, fmt, arg);

    strcat(buf2, getHeader(type, id));
    strcat(buf2, buf);

    printf("%s", buf2);
}

void *shareMem(size_t size) {
    key_t mem_key = IPC_PRIVATE;
    // get shared memory of this much size and with this private key
    // what is 0666?
    int shm_id = shmget(mem_key, size, IPC_CREAT | 0666);
    // attach the address space of shared memory to myself (callee)
    return (void *)shmat(shm_id, NULL, 0);
}

int main() {
    srand(time(0));

    printf("Enter robot count, student count, and table count:\n");
    // scanf("%d%d%d", &robotCount, &studentCount, &tableCount);
    robotCount = 5;
    studentCount = 5;
    tableCount = 5;
    assert(robotCount <= MAX_ROBOTS);
    assert(studentCount <= MAX_STUDENTS);
    assert(tableCount <= MAX_TABLES);

    tables = (table **)shareMem(MAX_TABLES * sizeof(table *));
    robots = (robot **)shareMem(MAX_ROBOTS * sizeof(robot *));
    students = (student **)shareMem(MAX_STUDENTS * sizeof(student *));
    pthread_mutex_t xx = PTHREAD_MUTEX_INITIALIZER;
    updateMutex = xx;

    checkTable = (pthread_mutex_t *)shareMem(sizeof(pthread_mutex_t));
    checkRobot = (pthread_mutex_t *)shareMem(sizeof(pthread_mutex_t));
    tableConditions =
        (pthread_cond_t *)shareMem(sizeof(pthread_cond_t) * tableCount);
    tableMutexes =
        (pthread_mutex_t *)shareMem(sizeof(pthread_mutex_t) * tableCount);
    robotConditions =
        (pthread_cond_t *)shareMem(sizeof(pthread_cond_t) * robotCount);
    robotMutexes =
        (pthread_mutex_t *)shareMem(sizeof(pthread_mutex_t) * robotCount);

    pthread_t *tableThreads =
        (pthread_t *)malloc(sizeof(pthread_t) * tableCount);
    pthread_t *studentThreads =
        (pthread_t *)malloc(sizeof(pthread_t) * studentCount);
    pthread_t *robotThreads =
        (pthread_t *)malloc(sizeof(pthread_t) * robotCount);

    for (int i = 0; i < tableCount; i++) {
        tables[i] = (table *)shareMem(sizeof(table));
        tables[i]->id = i;
        pthread_cond_t x = PTHREAD_COND_INITIALIZER;
        tableConditions[i] = x;
        pthread_mutex_t x2 = PTHREAD_MUTEX_INITIALIZER;
        tableMutexes[i] = x2;
    }

    for (int i = 0; i < robotCount; i++) {
        robots[i] = (robot *)shareMem(sizeof(robot));
        robots[i]->id = i;
        robots[i]->biryaniVesselsRemaining = 0;
        pthread_cond_t x = PTHREAD_COND_INITIALIZER;
        robotConditions[i] = x;
        pthread_mutex_t x2 = PTHREAD_MUTEX_INITIALIZER;
        robotMutexes[i] = x2;
    }

    for (int i = 0; i < studentCount; i++) {
        students[i] = (student *)shareMem(sizeof(student));
        students[i]->id = i;
        pthread_create(&studentThreads[i], NULL, initStudent, students[i]);
    }

    for (int i = 0; i < tableCount; i++) {
        pthread_create(&tableThreads[i], NULL, initTable, tables[i]);
    }

    for (int i = 0; i < robotCount; i++) {
        pthread_create(&robotThreads[i], NULL, initRobot, robots[i]);
    }

    for (int i = 0; i < studentCount; i++) {
        pthread_join(studentThreads[i], NULL);
    }

    for (int i = 0; i < tableCount; i++) {
        if (tables[i]->studentsEatingHere[0] != -1) {
            table *table = tables[i];

            tablePrintMsg(table->id, "entering serving phase\n");
            table->readyToServe = 0;
            for (int i = 0; i < 10; i++) {
                if (table->studentsEatingHere[i] == -1)
                    break;

                studentPrintMsg(table->studentsEatingHere[i],
                                "on serving table %d has been served.\n",
                                table->id + 1);
            }
        }
    }

    for (int i = 0; i < tableCount; i++) {
        pthread_cancel(tableThreads[i]);
    }

    for (int i = 0; i < studentCount; i++) {
        pthread_cancel(studentThreads[i]);
    }

    printf("Simulation over\n");

    return 0;
}