#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>  //fork
#include <sys/wait.h> //waitpid
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/sem.h>

#define BUFF_SIZE 128 // buffer size
#define PROC_COUNT 2 // number of processes
#define PARTY_COUNT 6 // number of parties

typedef struct data {
    int id;
    int voted_for;
    int is_valid;
} t_data;

typedef struct stats {
    int valid_votes;
    int invalid_votes;
} t_stats;

typedef struct vote {
    int id;
    int party;
} t_vote;

/* used to collect data to be sent over pipe */
typedef struct batch {
    size_t size; /* actual size */
    size_t cap;  /* capacity */
    t_data *batch;     /* the items */
} t_batch;

void goOut(int, char *); /* Take a brake */
void getPipeName(char *, int, char *); /* Gets name of pipe according to process id */
void batchInit(t_batch *); /* Initializes the batch */
void batchAdd(t_batch *, t_data); /* Adds an item to the batch */
void batchDestroy(t_batch *); /* Initializes the batch */
int sem_create(const char*, int);
void sem_op(int, int);
void sem_destroy(int);
void noMemoryError(void); /* Handles memory shortage -> prints message to stderr */
void fileError(const char *); /* Handles file errors -> prints message to stderr */
void ipcError(const char *); /* Handles IPC errors -> prints message to stderr */
void assertionError(const char*); /* Handles assertion errors -> prints message to stderr */
static int randomBetween(int, int); /* gets a random number between [lower, upper] */
static void emptyBuffer(void); /* empties buffer if it's overloaded (if fgets wasn't able to put \n in the array) */

static volatile int sigCount = 0;
void handler(int sig_number){ sigCount++; }
void empty_handler(int sig_number){ }

int main(int argc, char *argv[])
{
    if (argc < 2){
        fprintf(stderr, "Specify voter count in the arguments!\n");
        exit(1);
    }

    key_t sem_key = ftok(argv[0], 1);
    int sem_id = sem_create(argv[0], 1); // semaphore is UP

    pid_t proc_ids[PROC_COUNT];

    for (int ind = 0; ind < PROC_COUNT; ++ind) {
        if ((proc_ids[ind] = fork()) < 0) { ipcError("Forking was unsuccessful."); return false; }

        if (proc_ids[ind] == 0) {
            /* Child processes */
            /* common tasks go here */
            srand(time(NULL) ^ (getpid()<<16)); // seed random
            // pause(); // wait until start signal

            /* child specific tasks go here */
            if (ind == 0){
                /* Child1: 'ellenőriz' */
                printf("child1 is preparing\n"); fflush(stdout);
                sleep(randomBetween(1, 3)); // preparation time

                // open pipe to communicate with child1
                char to_child2_pipe[BUFF_SIZE];
                getPipeName(to_child2_pipe, getppid(), "_btw_children");
                int to_child2_fd = open(to_child2_pipe, O_WRONLY);

                printf("child1 is ready\n"); fflush(stdout);
                kill(getppid(), SIGUSR1);  // signal parent: child1 is ready

                // read child2 pid and voter data from pipe
                pid_t child2_pid;
                t_batch voters;
                char to_parent_pipe[BUFF_SIZE];
                getPipeName(to_parent_pipe, getpid(), "");
                int to_parent_fd = open(to_parent_pipe, O_RDONLY);
                read(to_parent_fd, &child2_pid, sizeof(pid_t));
                read(to_parent_fd, &voters.size, sizeof(voters.size));
                voters.batch = (t_data *) malloc(voters.size * sizeof(t_data));
                if (voters.batch == NULL) { noMemoryError(); }
                read(to_parent_fd, voters.batch, voters.size * sizeof(t_data));
                close(to_parent_fd);

                goOut(sem_id, "child1");

                // print the received data
                printf("child1 has received the data from the parent:\n"); fflush(stdout);
                for (int i = 0; i < voters.size; ++i) {
                    printf("\tid. %d\n", voters.batch[i].id); fflush(stdout);
                }

                // check if identifier is valid
                printf("child1 is checking whether the ids are valid.\n"); fflush(stdout);
                sleep(randomBetween(1, 3)); // time to validate
                for (int i = 0; i < voters.size; ++i) {
                    voters.batch[i].is_valid = (randomBetween(1, 100) > 20);
                }
                printf("child1 is done with validation.\n"); fflush(stdout);

                goOut(sem_id, "child1");

                // send voters to child2 after checking identifiers
                printf("child1 is sending the validated data to child2.\n"); fflush(stdout);
                write(to_child2_fd, &voters.size, sizeof(voters.size)); // send count first
                write(to_child2_fd, voters.batch, voters.size * sizeof(t_data)); // send voters after
                close(to_child2_fd);

                kill(child2_pid, SIGUSR2); // notify child2 that data is ready

                //free(voters.batch);
                batchDestroy(&voters);
            }

            else if (ind == 1){
                /* Child2: 'pecsétel' */
                printf("child2 is preparing\n"); fflush(stdout);
                signal(SIGUSR2, empty_handler);  // set signal handler so it won't kill the process
                sleep(1); // to avoid race

                // open pipe to communicate with child1
                char to_child1_pipe[BUFF_SIZE];
                getPipeName(to_child1_pipe, getppid(), "_btw_children");
                int to_child1_fd = open(to_child1_pipe, O_RDONLY);

                // prepare, then tell parent that child2 is ready
                sleep(randomBetween(1, 3)); // preparation time
                printf("child2 is ready\n"); fflush(stdout);
                kill(getppid(), SIGUSR1);  // signal parent: child2 is ready

                goOut(sem_id, "child2");

                // wait until signal that data is ready
                pause();
                // read voter data from pipe
                t_batch voters;
                read(to_child1_fd, &voters.size, sizeof(voters.size));
                voters.batch = (t_data *) malloc(voters.size * sizeof(t_data));
                if (voters.batch == NULL) { noMemoryError(); }
                read(to_child1_fd, voters.batch, voters.size * sizeof(t_data));
                close(to_child1_fd);

                // print received data
                printf("child2 has received the data from child1:\n"); fflush(stdout);
                for (int i = 0; i < voters.size; ++i) {
                    printf("\tid. %d - %d\n", voters.batch[i].id, voters.batch[i].is_valid); fflush(stdout);
                }

                goOut(sem_id, "child2");

                // count valid data
                printf("child2 is calculating the voting stats.\n"); fflush(stdout);
                sleep(randomBetween(1,3));  // count duration
                t_stats stats = {0, 0};
                for (int i = 0; i < voters.size; ++i) {
                    stats.valid_votes += voters.batch[i].is_valid;
                }
                stats.invalid_votes = (int)voters.size - stats.valid_votes;

                // send stats to parent
                printf("child2 is sending the stats to the parent.\n"); fflush(stdout);
                char to_parent_pipe[BUFF_SIZE];
                getPipeName(to_parent_pipe, getpid(), "");
                int to_parent_fd = open(to_parent_pipe, O_WRONLY);
                write(to_parent_fd, &stats, sizeof(stats));

                // the voting
                goOut(sem_id, "child2");

                printf("child2: the voting has started.\n"); fflush(stdout);
                for (int i = 0; i < voters.size; ++i) {
                    if (voters.batch[i].is_valid){
                        usleep(randomBetween(150, 350)); // voting time
                        t_vote vote = {voters.batch[i].id, randomBetween(1, PARTY_COUNT)};
                        write(to_parent_fd, &vote, sizeof(vote));
                        printf("child2: %d has submitted her/his vote.\n", voters.batch[i].id); fflush(stdout);
                    }
                }

                close(to_parent_fd);
                //free(voters.batch);
                batchDestroy(&voters);
            }

            _exit(0);  // for every child
        }
    }

    /* Parent: 'elnök' */
    // set handler so the default handler won't terminate the process
    signal(SIGUSR1, handler);

    // create pipes for communication with children, and pipe for children-to-children
    for (int i = 0; i < PROC_COUNT; ++i) {
        char pipe_name[BUFF_SIZE];
        getPipeName(pipe_name, proc_ids[i], "");
        if (mkfifo(pipe_name, S_IRUSR|S_IWUSR ) == -1) { ipcError("Pipe creation was unsuccessful."); }
    }

    char btw_child_pipe[BUFF_SIZE];
    getPipeName(btw_child_pipe, getpid(), "_btw_children");
    if (mkfifo(btw_child_pipe, S_IRUSR|S_IWUSR ) == -1) { ipcError("Pipe creation was unsuccessful."); }

    // wait until children are ready
    pause();
    // in case signals fire the same time
    while (sigCount < PROC_COUNT) {
        sleep(100);
    }
    sigCount = 0;

    goOut(sem_id, "parent");

    // generate voters
    int count = atoi(argv[1]);
    t_batch voter_data;
    batchInit(&voter_data);
    for (int i = 0; i < count; ++i) {
        t_data data = {randomBetween(10000, 99999), -1, 0};
        batchAdd(&voter_data, data);
    }

    // send pid of child2 and voters to child1
    char child1_pipe[BUFF_SIZE];
    getPipeName(child1_pipe, proc_ids[0], "");

    int child_1_fd = open(child1_pipe, O_WRONLY);
    write(child_1_fd, &proc_ids[1], sizeof(pid_t)); // send child2 pid first
    write(child_1_fd, &voter_data.size, sizeof(voter_data.size)); // send count first
    write(child_1_fd, voter_data.batch, voter_data.size * sizeof(t_data)); // send voters after
    close(child_1_fd);

    goOut(sem_id, "parent");

    // read the valid / invalid vote rate from child2 pipe
    char child2_pipe[BUFF_SIZE];
    getPipeName(child2_pipe, proc_ids[1], "");

    t_stats stats;
    int child_2_fd = open(child2_pipe, O_RDONLY);
    read(child_2_fd, &stats, sizeof(stats));
    printf("parent received the valid/invalid vote rates:\n\tvalid: %d\n\tinvalid: %d\n", stats.valid_votes, stats.invalid_votes); fflush(stdout);
    // print stats into file
    char stat_file[BUFF_SIZE];
    getPipeName(stat_file, getpid(), ".txt");
    FILE *fp = fopen(stat_file, "wb+");
    if (fp == NULL) { fileError("Can't open file."); }
    fprintf(fp, "valid: %d\ninvalid: %d\n", stats.valid_votes, stats.invalid_votes);
    printf("parent has written the rates into file: '%s'\n", stat_file); fflush(stdout);
    fclose(fp);

    goOut(sem_id, "parent");

    // wait until all valid voter summit their votes
    int results[PARTY_COUNT] = {0};
    int voted = 0;
    while (voted < stats.valid_votes){
        t_vote vote;
        read(child_2_fd, &vote, sizeof(vote));
        results[vote.party-1]++;
        printf("parent has received the vote of %d\n", vote.id); fflush(stdout);
        voted++;
    }
    close(child_2_fd);

    goOut(sem_id, "parent");

    printf("\nparent is counting the votes...\n"); fflush(stdout);
    sleep(2);
    printf("~~~~~~~~~ The results are ready! ~~~~~~~~~\n"); fflush(stdout);
    int winnerVotes = -1, winnerParty = -1;
    for (int i = 0; i < PARTY_COUNT; ++i) {
        printf("\t%d - %d votes\n", i+1, results[i]); fflush(stdout);
        if (results[i] > winnerVotes){
            winnerVotes = results[i];
            winnerParty = i+1;
        }
    }
    printf("\nSo the winner is: %d (%d votes)\n", winnerParty, winnerVotes); fflush(stdout);

    // wait until children are done
    pid_t finished_pid;
    while ((finished_pid = waitpid(-1, NULL, 0)) != -1) {
        for (int i = 0; i < PROC_COUNT; ++i) {
            if (proc_ids[i] == finished_pid){
                // a child has finished
                // printf("child%d has finished.\n", i+1); fflush(stdout);
            }
        }
    }

    // unlink pipe files created by parent
    for (int i = 0; i < PROC_COUNT; ++i) {
        char pipe_name[BUFF_SIZE];
        getPipeName(pipe_name, proc_ids[i], "");
        unlink(pipe_name);
    }
    unlink(btw_child_pipe);
    sem_destroy(sem_id); // delete semaphore
    batchDestroy(&voter_data);
}

void goOut(int sem_id, char *proc_name) {
    // probability of going out: 30%
    if (randomBetween(1, 100) > 70) {
        printf("%s wants to go out...\n", proc_name); fflush(stdout);
        sem_op(sem_id, -1);
        printf("%s went out to take a brake.\n", proc_name); fflush(stdout);
        sleep(5);
        printf("%s came back.\n", proc_name); fflush(stdout);
        sem_op(sem_id, 1);
    }
}

void getPipeName(char *name_buff, int pid, char *postfix){
    sprintf(name_buff,"/tmp/nvp190_%d%s", pid, postfix);
}

void batchInit(t_batch *batch) {
    batch->size = 0;
    batch->cap = 10;

    // initial allocation
    batch->batch = (t_data *)malloc(batch->cap * sizeof(t_data));
    if (batch->batch == NULL){ noMemoryError(); }
}

void batchAdd(t_batch *batch, t_data data){
    // increase size when full
    if (batch->size == batch->cap)
    {
        t_data *new_batch = (t_data *)realloc(batch->batch, batch->cap * 2 * sizeof(t_data));
        if (new_batch == NULL){ noMemoryError(); }

        batch->batch  = new_batch;
        batch->cap *= 2;
    }

    batch->batch[batch->size++] = data;
}

void batchDestroy(t_batch *batch) {
    batch->size = 0;
    batch->cap = 0;

    free(batch->batch);
    batch->batch = NULL;
}

int sem_create(const char* pathname, int sem_val){
    int sem_id;
    key_t key;

    key=ftok(pathname, 1);
    if((key=semget(key, 1, IPC_CREAT | S_IRUSR | S_IWUSR )) < 0)
        perror("semget");
    // semget 2. parameter is the number of semaphores
    if(semctl(key, 0, SETVAL, sem_val) < 0)    //0= first semaphores
        perror("semctl");

    return key;
}


void sem_op(int sem_id, int op){
    struct sembuf operation;

    operation.sem_num = 0;
    operation.sem_op  = op; // op=1 up, op=-1 down
    operation.sem_flg = 0;

    if(semop(sem_id,&operation,1)<0) // 1 number of sem. operations
        perror("semop");
}

void sem_destroy(int sem_id){
    semctl(sem_id,0,IPC_RMID);
}

void noMemoryError(void){
    fprintf(stderr, "\nOut of memory!");
    exit(1);
}

void fileError(const char *msg){
    fprintf(stderr, "%s %s\n", "File error: ", msg);
    exit(1);
}

void ipcError(const char *msg){
    fprintf(stderr, "%s %s\n", "IPC error: ", msg);
    exit(1);
}

void assertionError(const char *msg){
    fprintf(stderr, "%s %s\n", "Assertion error: ", msg);
    exit(1);
}

static int randomBetween(int lower, int upper){
    return (rand() % (upper - lower + 1)) + lower;
}

static void emptyBuffer(void){
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }
}
