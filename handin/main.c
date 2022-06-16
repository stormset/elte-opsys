/*
 * A part of the code was reused from my hand-in for 'Imperative Programming'.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>  //fork
#include <sys/wait.h> //waitpid

#define INIT_SIZE 10
#define GROW_BY 5
#define MANAGE_LIMIT 5
#define BUFFER_SIZE 128 // size of buffer for input processing (maximum accepted text length = BUFFER_SIZE - 1)
#define PROC_MAX 10 // max. number of inspectors

typedef struct Person {
    char name[BUFFER_SIZE], area[BUFFER_SIZE];
    unsigned applicationCount;
} t_person;

typedef struct Result {
    char name[BUFFER_SIZE];
    int collected;
} t_result;

typedef struct {
    const size_t count;
    const char **areas;
} t_inspector;

/* Global variables */
struct {
    FILE *fp;
    char name[BUFFER_SIZE];
} linkedFile;

struct {
    t_person **iterator;
    int size, count, freed;
} list;

/* Input handling */
bool checkedReadIntoBuffer(size_t, void *, const char *, bool (*)(const char *, void *), void *);
bool lengthChecker(const char *, void *);
bool lengthAndAlreadyExistsChecker(const char *, void *);
bool validAreaChecker(const char *, void *);
bool validAreaOrEmptyChecker(const char *, void *);
bool lengthAndOnlyDigitsAndIsPositiveChecker(const char *, void *);
/* All functions with bool return type return true on operation success, false otherwise. */
bool run(void); /* The execution loop, handling the user input */
bool startContest(void); /* Appends an item to the global Person *iterator */
bool addItem(void); /* Appends an item to the global Person *iterator */
bool removeItem(void); /* Deletes an item, identified by the persons name */
bool changeItem(void); /* Modifies an item, identified by the persons name */
bool manageAllocatedSpace(void); /* Moves the pointers up in the iterator, while keeping their relative position, when the freed space is >= 5 */
bool listItems(void); /* Lists the items */
bool listItemsWithArea(void); /* Lists the items */
bool linkToFile(const char *); /* Link current 'context' to file */
bool unlinkFile(); /* Unlink from linked file. */
bool growIterator(int); /* Grows the global Person *iterator */
bool askYesNo(const char *); /* Asks a yes or no question returning true on positive answer */
bool exitExecution(void); /* exits the execution loop, frees the allocated storage */
int getEntryCount(void); /* Grows the global Person *iterator */
void loadDataFromFile(void);
void saveDataToFile(void);
void appendRecord(t_person *);
void removeAllRecord(void);
void reopenFile(void);
void noMemoryError(void); /* Handles memory shortage -> prints message to stderr */
void fileError(const char *); /* Handles file errors -> prints message to stderr */
void ipcError(const char *); /* Handles IPC errors -> prints message to stderr */
void assertionError(const char*); /* Handles assertion errors -> prints message to stderr */
static int randomBetween(int, int); /* gets a random number between [lower, upper] */
static void emptyBuffer(void); /* empties buffer if it's overloaded (if fgets wasn't able to put \n in the array) */

static size_t contestCount = 0;
static void startHandler(int sig, siginfo_t *si, void *ucontext)
{
    contestCount = si->si_value.sival_int;
}

int main(void)
{
    // set signal handler of contest start
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = startHandler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &sa, NULL);

    printf("*******************************************************************************************\n"
           "***************************************** MANUAL ******************************************\n"
           "*******************************************************************************************\n"
           "***                                                                                     ***\n"
           "***    It works similarly to a CLI except that the commands don't take arguments,       ***\n"
           "***    but ask for the required arguments after enter is pressed.                       ***\n"
           "***                                                                                     ***\n"
           "**************************************** COMMANDS *****************************************\n"
           "***    link   – Links the application data store to a file. By default the data store   ***\n"
           "***             is not linked.                                                          ***\n"
           "***                                                                                     ***\n"
           "***    unlink – Unlinks the application data store from the file.                       ***\n"
           "***                                                                                     ***\n"
           "***    ls     – Lists the stored records.                                               ***\n"
           "***                                                                                     ***\n"
           "***    filter – Lists the records where 'area' equals to the one given in parameter.    ***\n"
           "***                                                                                     ***\n"
           "***    start  – Starts the contest.                                                     ***\n"
           "***                                                                                     ***\n"
           "***    add    – Adds a new record to the data store.                                    ***\n"
           "***                                                                                     ***\n"
           "***    rem    – Removes a record from the data store identified by 'name'.              ***\n"
           "***                                                                                     ***\n"
           "***    mod    – Changes a record in the data store identified by 'name'.                ***\n"
           "***             Note: If you don't want to change an attribute, simply press ENTER.     ***\n"
           "***                                                                                     ***\n"
           "***    quit   – Exits the application.                                                  ***\n"
           "*******************************************************************************************\n"
           "*******************************************************************************************\n"
    );

    return !run();
}

bool run(void){
    if (!list.iterator){
        if (!growIterator(INIT_SIZE)) return false; /* Initial allocation */
    }

    while (true) {
        char cmd_buffer[32];
        if (checkedReadIntoBuffer(32, cmd_buffer, "> ", NULL, NULL)){
            if(strlen(cmd_buffer) == 0) continue;

            if (strcmp(cmd_buffer, "start") == 0){
                if(!startContest()) return false;
            }
            else if (strcmp(cmd_buffer, "add") == 0){
                if(!addItem()) return false;
            }
            else if (strcmp(cmd_buffer, "rem") == 0){
                if (!removeItem()) return false;
            }
            else if (strcmp(cmd_buffer, "mod") == 0){
                if (!changeItem()) return false;
            }
            else if (strcmp(cmd_buffer, "ls") == 0){
                if (!listItems()) return false;
            }
            else if (strcmp(cmd_buffer, "filter") == 0){
                if (!listItemsWithArea()) return false;
            }
            else if (strcmp(cmd_buffer, "link") == 0){
                if (!linkToFile(NULL)) return false;
            }
            else if (strcmp(cmd_buffer, "unlink") == 0){
                if (!unlinkFile()) return false;
            }
            else if (strcmp(cmd_buffer, "quit") == 0){
                return exitExecution();
            }
            else{
                printf("> Unknown command!\n");
            }
        } else {
            printf("> Unknown command!\n");
        }
    }
}

bool lengthChecker(const char *input, void *args){
    size_t len = strlen(input);
    return ((size_t*)args)[0] <= len && len <= ((size_t*)args)[1];
}

bool lengthAndAlreadyExistsChecker(const char *input, void *args){
    size_t len = strlen(input);
    if (!(((size_t*)args)[0] <= len && len <= ((size_t*)args)[1]))
        return false;

    for (int i = 0; i < list.count; ++i) {
        if (list.iterator[i] && strcmp(input, list.iterator[i]->name) == 0){
            return false;
        }
    }

    return true;
}

bool validAreaChecker(const char *input, void *args){
    static const char* validAreas[] = {"Barátfa", "Lovas", "Szula", "Kígyós-patak", "Malom telek", "Páskom", "Káposztás kert"};
    static size_t len = sizeof(validAreas) / sizeof(validAreas[0]);

    for (size_t i = 0; i < len; ++i) {
        if (strcmp(input, validAreas[i]) == 0){
            return true;
        }
    }

    return false;
}

bool validAreaOrEmptyChecker(const char *input, void *args){
    if (strlen(input) == 0)
        return true;
    return validAreaChecker(input, args);
}

bool lengthAndOnlyDigitsAndIsPositiveChecker(const char *input, void *args){
    size_t len = strlen(input);

    if (!lengthChecker(input, args))
        return false;

    if (len == 0)
        return true;

    for (size_t i = 0; i < len; ++i){
        if ((input[i] < '0' || input[i] > '9' )) return false;
    }

    return atoi(input) > 0;
}

bool checkedReadIntoBuffer(size_t length, void *dest, const char *prompt,
                           bool (*checker)(const char *, void *), void *checkerArgs){
    char buffer[BUFFER_SIZE + 1]; /* +1 so strlen(.) > BUFFER_SIZE - 1 can be checked */
    bool keepRecord = true;

    if (length > BUFFER_SIZE){
        assertionError("Unable to read more characters then the buffer size!");
    }

    do{
        printf("%s", prompt);
        fgets(buffer, length + 1, stdin);

        char *trim;
        if((trim = strchr(buffer, '\n')))
            *trim = '\0';
        else {
            emptyBuffer();  // there wasn't place for the line-end in the buffer, it should be discarded
        }
    }while ( (strlen(buffer) > length) || ((checker && (!checker(buffer, checkerArgs))) &&
                                           (keepRecord = askYesNo("The input is invalid. Want to reenter? (y/n): "))) );

    if (keepRecord){
        strncpy((char *)dest, buffer, length);
    }

    return keepRecord;
}

bool startContest(void) {
    static const char *areas1[] = {"Barátfa", "Lovas", "Kígyós-patak", "Káposztás kert"};
    static const char *areas2[] = {"Szula", "Malom telek", "Páskom"};
    static const t_inspector inspectors[] = {{4, areas1}, {3, areas2}};
    static const size_t inspectorCount = sizeof(inspectors) / sizeof(inspectors[0]);

    if (getEntryCount() <= 0){
        printf("Please add rabbits first!\n");
        return true;
    }

    pid_t proc_ids[PROC_MAX];
    int pipe_fds[PROC_MAX][2];

    for (size_t i = 0; i < inspectorCount; ++i) {
        if (pipe(pipe_fds[i]) == -1) { ipcError("Pipe creation was unsuccessful."); return false; }
        if ((proc_ids[i] = fork()) < 0) { ipcError("Forking was unsuccessful."); return false; }

        if (proc_ids[i] == 0) {
            /* child (inspector) */

            srand(time(NULL) ^ (getpid()<<16)); // seed random
            pause(); // wait until contest starts

            // read contestant's information from unnamed pipe
            t_person *persons = (t_person *) malloc(contestCount * sizeof(t_person));

            for (size_t j = 0; j < contestCount; ++j) {
                read(pipe_fds[i][0], &persons[j], sizeof(t_person));
            }
            close(pipe_fds[i][0]);

            printf("Inspector %lu. received the information of the participants.\n", i+1);
            fflush(stdout);

            sleep(randomBetween(1,3)); // prepare for the contest
            printf("Contest started in the area of inspector %lu.\n", i+1);
            fflush(stdout);

            // send back the contest results in the related areas
            sleep(randomBetween(1,5)); // duration of the contest

            printf("Inspector %lu. sends back the results to the judge...\n", i+1);
            fflush(stdout);
            sleep(randomBetween(1,3)); // summarize

            for (size_t j = 0; j < contestCount; ++j) {
                t_result result;
                strncpy(result.name, persons[j].name, BUFFER_SIZE);
                result.collected = randomBetween(1, 100);

                write(pipe_fds[i][1], &result, sizeof(result));
            }
            close(pipe_fds[i][1]);

            free(persons);
            _exit(0);
        }
    }

    /* Judge ("Főnyuszi") */

    // send controlled areas for inspectors
    printf("Sending records to their inspectors according to area...\n");

    sleep(2);
    size_t contestantCount[PROC_MAX] = {0};

    for (int i = 0; i < list.count; ++i) {
        for (size_t j = 0; j < inspectorCount; ++j) {
            for (size_t k = 0; k < inspectors[j].count; ++k) {
                if (list.iterator[i] && strcmp(inspectors[j].areas[k], list.iterator[i]->area) == 0) {
                    write(pipe_fds[j][1], list.iterator[i], sizeof(t_person));
                    contestantCount[j]++;
                }
            }
        }
    }

    // notify children that the contest started, and send the nr. of contestants
    for (size_t i = 0; i < inspectorCount; ++i) {
        close(pipe_fds[i][1]);

        union sigval sv = {0};
        sv.sival_int = (int)contestantCount[i];
        sigqueue(proc_ids[i], SIGUSR1, sv);
    }

    // wait until inspectors are done
    pid_t finished_pid;
    t_result winner; winner.collected = -1;
    while ((finished_pid = waitpid(-1, NULL, 0)) != -1) {
        for (size_t i = 0; i < inspectorCount; ++i) {
            if (proc_ids[i] == finished_pid){
                // an inspector has finished
                printf("Judge got the results from inspector %lu. They are:", i+1); fflush(stdout);

                int *pipe_fd = pipe_fds[i];

                for (size_t j = 0; j < contestantCount[i]; ++j) {
                    t_result result;
                    read(pipe_fd[0], &result, sizeof(result));

                    printf("\n\t%-14s (%-3d eggs)", result.name, result.collected); fflush(stdout);

                    if (result.collected > winner.collected){
                        winner.collected = result.collected;
                        strncpy(winner.name, result.name, BUFFER_SIZE);
                    }
                }
                printf("\n"); fflush(stdout);
                close(pipe_fd[0]);
            }
        }
    }

    if (getEntryCount() > 0){
        printf("\n~~~~~~~~~ The Winner is: %s (with %d eggs)! ~~~~~~~~~\n", winner.name, winner.collected); fflush(stdout);
    }

    return true;
}

bool addItem(void){
    t_person *newRecord = (t_person *) malloc(sizeof(t_person));
    if (!newRecord){
        noMemoryError();
        return false;
    }

    // read name
    size_t args[2] = {1, BUFFER_SIZE - 1}; // minLength, maxLength
    if (!checkedReadIntoBuffer(BUFFER_SIZE, newRecord->name, ">> Name: ", lengthAndAlreadyExistsChecker, args)){
        printf("Dropping record...\n");
        return true;
    }

    // read area
    if (!checkedReadIntoBuffer(BUFFER_SIZE, newRecord->area, ">> Area: ", validAreaChecker, NULL)){
        printf("Dropping record...\n");
        return true;
    }

    // read application count
    size_t args2[2] = {1, 5}; // minLength, maxLength
    char num_buffer[6];
    if (!checkedReadIntoBuffer(6, num_buffer, ">> Application count: ", lengthAndOnlyDigitsAndIsPositiveChecker, args2)){
        printf("Dropping record...\n");
        return true;
    } else {
        newRecord->applicationCount = atoi(num_buffer);
    }

    appendRecord(newRecord);

    if (linkedFile.fp != NULL) {
        saveDataToFile();
    }

    return true;
}

bool removeItem(void){
    char tmp[BUFFER_SIZE];

    size_t args[2] = {1, BUFFER_SIZE - 1}; // minLength, maxLength
    if (!checkedReadIntoBuffer(BUFFER_SIZE, tmp, "[DELETE]>> Name: ", lengthChecker, args)){
        printf("No record to delete.\n");
        return true;
    }

    bool success = true;
    int dropped = 0;
    for (int i = 0; i < list.count; ++i) {
        if (list.iterator[i] && strcmp(tmp, list.iterator[i]->name) == 0){
            free(list.iterator[i]);
            list.iterator[i] = NULL;
            list.freed++;
            dropped++;
        }
    }
    if (dropped > 0){
        if (linkedFile.fp != NULL) {
            saveDataToFile();
        }

        success = manageAllocatedSpace();
        printf("Dropped %d record.\n", dropped);
    }
    else printf("No record to delete.\n");

    return success;
}

bool changeItem(void){
    char tmp[BUFFER_SIZE];
    char prompt_text[BUFFER_SIZE + 32];

    size_t args[2] = {1, BUFFER_SIZE - 1}; // minLength, maxLength
    if (!checkedReadIntoBuffer(BUFFER_SIZE, tmp, "[CHANGE]>> Name: ", lengthChecker, args)){
        printf("No record to change.\n");
        return true;
    }

    for (int i = 0; i < list.count; ++i) {
        if (list.iterator[i] && strcmp(tmp, list.iterator[i]->name) == 0){
            t_person tmpRec;
            /* Ask for new data */
            size_t args2[2] = {0, BUFFER_SIZE - 1};
            sprintf(prompt_text, "[CHANGE: NAME][PREVIOUS: %s]>> ", list.iterator[i]->name);
            if (!checkedReadIntoBuffer(BUFFER_SIZE, tmpRec.name, prompt_text, lengthAndAlreadyExistsChecker, args2)){
                printf("Record wasn't modified.\n");
                return true;
            }

            // read area
            sprintf(prompt_text, "[CHANGE: AREA][PREVIOUS: %s]>> ", list.iterator[i]->area);
            if (!checkedReadIntoBuffer(BUFFER_SIZE, tmpRec.area, prompt_text, validAreaOrEmptyChecker, NULL)){
                printf("Record wasn't modified.\n");
                return true;
            }

            // read application count
            size_t args3[2] = {0, 5}; // minLength, maxLength
            char num_buffer[6];
            sprintf(prompt_text, "[CHANGE: APPLICATION_COUNT][PREVIOUS: %d]>> ", list.iterator[i]->applicationCount);
            if (!checkedReadIntoBuffer(6, num_buffer, prompt_text, lengthAndOnlyDigitsAndIsPositiveChecker, args3)){
                printf("Record wasn't modified.\n");
                return true;
            } else {
                if (strlen(num_buffer) == 0)
                    tmpRec.applicationCount = list.iterator[i]->applicationCount;
                else
                    tmpRec.applicationCount = atoi(num_buffer);
            }

            /* Copy data into record */
            if(strlen(tmpRec.name) != 0) // if empty leave the original
                strncpy(list.iterator[i]->name, tmpRec.name, BUFFER_SIZE);
            if(strlen(tmpRec.area) != 0) // if empty leave the original
                strncpy(list.iterator[i]->area, tmpRec.area, BUFFER_SIZE);
            list.iterator[i]->applicationCount = tmpRec.applicationCount;

            if (linkedFile.fp != NULL) {
                saveDataToFile();
            }

            return true;
        }
    }


    printf("No record to change.\n");
    return true;
}

bool listItems(void){
    fflush(stdout);
    printf("\n===================================== %d entries =====================================\n", getEntryCount());
    printf("%-40s%-30s  %-30s", "[Name]", "[Area]", "[Application Count]");
    if (getEntryCount() > 0) {
        for (int i = 0; i < list.count; ++i) {
            if (list.iterator[i]) {
                printf("\n%-40s", list.iterator[i]->name);
                printf("%-30s\t", list.iterator[i]->area);
                printf("%-30d", list.iterator[i]->applicationCount);
            }
        }
    }
    printf("\n\n");
    return true;
}

bool listItemsWithArea(void){
    char areaInput[BUFFER_SIZE];
    size_t args[2] = {1, BUFFER_SIZE - 1}; // minLength, maxLength
    if (!checkedReadIntoBuffer(BUFFER_SIZE, areaInput, "[FILTER]>> Area: ", lengthChecker, args)){
        return true;
    }

    fflush(stdout);
    printf("\n===================================== Rabbits in '%s' =====================================\n", areaInput);
    printf("%-40s%-30s  %-30s", "[Name]", "[Area]", "[Application Count]");
    for (int i = 0; i < list.count; ++i) {
        if (list.iterator[i] && strcmp(areaInput, list.iterator[i]->area) == 0) {
            printf("\n%-40s", list.iterator[i]->name);
            printf("%-30s\t", list.iterator[i]->area);
            printf("%d", list.iterator[i]->applicationCount);
        }
    }
    printf("\n\n");
    return true;
}

bool linkToFile(const char *filename){
    char fileNameBuffer[BUFFER_SIZE];

    if (linkedFile.fp != NULL) {
        printf("Already linked to a file called: '%s'. Unlink from the file, and try again.\n", linkedFile.name);
    } else {
        const char *linkToName = filename;
        if (filename == NULL){
            size_t args[2] = {1, BUFFER_SIZE - 1}; // minLength, maxLength
            if (!checkedReadIntoBuffer(BUFFER_SIZE, fileNameBuffer, ">> File name: ", lengthAndAlreadyExistsChecker, args)){
                printf("Linking wasn't completed.\n");
                return true;
            }

            linkToName = fileNameBuffer;
        }

        linkedFile.fp = fopen(linkToName, "ab+");

        if (linkedFile.fp == NULL) {
            fileError("Unable to link to file!");
            return false;
        } else {
            // change currently linked file name
            strncpy(linkedFile.name, linkToName, BUFFER_SIZE);

            if (getEntryCount() == 0){
                loadDataFromFile();
            } else {
                // Ask whether to load data from file or dump current data into file.
                bool shouldLoad = askYesNo("Do you want to load data from file?\n"
                                           "\tIf 'yes' is selected current entries will be removed and the entries will be loaded from the file\n"
                                           "\tIf 'no' is selected the file will be emptied and the current entries will be saved in the file.\n"
                                           "Answer: ");
                if (shouldLoad)
                    loadDataFromFile();
                else{
                    fclose(linkedFile.fp);
                    saveDataToFile();
                }
            }
        }
    }

    return true;
}

bool unlinkFile(void){
    linkedFile.fp = NULL;

    return true;
}

bool askYesNo(const char *msg){
    char tmp[2];
    printf("%s", msg);
    fgets(tmp, 2, stdin);
    if(!strchr(tmp, '\n')) emptyBuffer();
    switch (tmp[0]){
        case 'y': case 'Y':
            return true;
        default:
            return false;
    }
}

bool manageAllocatedSpace(void){
    bool success = true;
    if (list.freed >= MANAGE_LIMIT){
        int emptyPos = 0;
        while (emptyPos < list.count){
            if (!list.iterator[emptyPos]){
                int nonEmptyPos = emptyPos + 1;
                while (!list.iterator[nonEmptyPos] && nonEmptyPos < list.count) nonEmptyPos++;
                if (nonEmptyPos >= list.count) break; /* Done. All position is empty. */
                else {
                    list.iterator[emptyPos] = list.iterator[nonEmptyPos];
                    list.iterator[nonEmptyPos] = NULL;
                }
            } else  emptyPos++;
        }
        success = growIterator(list.size - list.freed + 1);
        list.count -= list.freed;
        list.freed = 0;
    }

    return success;
}

bool growIterator(int newSize){
    t_person **tmp = (t_person **) realloc(list.iterator, newSize * sizeof(t_person *));
    if (tmp) {
        list.iterator = tmp;
        list.size = newSize;
        return true;
    }
    else {
        noMemoryError();
        return false;
    }
}

void freeAllocated(void){
    for (int i = 0; i < list.count; ++i) {
        free(list.iterator[i]);
    }
    free(list.iterator);
}

bool exitExecution(void){
    freeAllocated();
    return true;
}

int getEntryCount(void) {
    return list.count - list.freed;
}

/* TODO: verify data loaded from file */
void loadDataFromFile(void){
    if (linkedFile.fp != NULL){
        removeAllRecord();

        /* Read data */
        char buffer1[BUFFER_SIZE];
        char buffer2[BUFFER_SIZE];
        char buffer3[BUFFER_SIZE];

        char fmt[32];
        fscanf(linkedFile.fp, "%*[^\n]\n"); /* ignore first line */

        sprintf(fmt, "%%%d[^;];%%%d[^;];%%%d[^;\n]\n", BUFFER_SIZE - 1, BUFFER_SIZE - 1, 5);
        while (fscanf(linkedFile.fp, fmt, buffer1, buffer2, buffer3) != EOF) {
            t_person *newRecord = (t_person *) malloc(sizeof(t_person));
            if (!newRecord) noMemoryError();

            strncpy(newRecord->name, buffer1, BUFFER_SIZE);
            strncpy(newRecord->area, buffer2, BUFFER_SIZE);
            newRecord->applicationCount = atoi(buffer3);

            appendRecord(newRecord);
        }
        fclose(linkedFile.fp);
    }
}

void saveDataToFile(void){
    if (linkedFile.fp != NULL){
        reopenFile();
        fprintf(linkedFile.fp, "Name;Area;Application Count\n");
        for (int i = 0; i < list.count; ++i) {
            if (list.iterator[i]) {
                fprintf(linkedFile.fp, "%s;", list.iterator[i]->name);
                fprintf(linkedFile.fp, "%s;", list.iterator[i]->area);
                fprintf(linkedFile.fp, "%d", list.iterator[i]->applicationCount);
                fprintf(linkedFile.fp, "\n");
            }
        }
        fclose(linkedFile.fp);
    }
}

void appendRecord(t_person *newRecord) {
    list.iterator[list.count++] = newRecord;
    if (list.count >= list.size) growIterator(list.size + GROW_BY);
}

void removeAllRecord(void) {
    int dropped = 0;
    for (int i = 0; i < list.count; ++i) {
        if (list.iterator[i] != NULL){
            free(list.iterator[i]);
            list.iterator[i] = NULL;
            list.freed++;
            dropped++;
        }
    }
    if (dropped > 0){
        manageAllocatedSpace();
    }
}

void reopenFile(void){
    if (linkedFile.fp != NULL){
        linkedFile.fp = fopen(linkedFile.name, "wb+");

        if (linkedFile.fp == NULL)
            fileError("Unable to link to file!");
    }
}

void noMemoryError(void){
    freeAllocated();
    fprintf(stderr, "\nOut of memory!");
}

void fileError(const char *msg){
    freeAllocated();
    fprintf(stderr, "%s: %s\n", "File error: ", msg);
}

void ipcError(const char *msg){
    freeAllocated();
    fprintf(stderr, "%s: %s\n", "IPC error: ", msg);
}

void assertionError(const char *msg){
    freeAllocated();
    fprintf(stderr, "%s: %s\n", "Assertion error: ", msg);
    exit(1);
}

static int randomBetween(int lower, int upper){
    return (rand() % (upper - lower + 1)) + lower;
}

static void emptyBuffer(void){
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }
}
