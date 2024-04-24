#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>

#define TEXT_SIZE 100
#define NUM_CHILDREN 5

typedef struct {
    char text[NUM_CHILDREN][TEXT_SIZE];
    char encoded_texts[NUM_CHILDREN][TEXT_SIZE];
    int encoded_count;
} SharedData;

SharedData *shared_data;
int semaphore_id;

void encode_text(int child_id) {
    struct sembuf sb = {child_id, -1, 0}; // Set up semaphore operation

    if (semop(semaphore_id, &sb, 1) == -1) {
        perror("semop");
        exit(1);
    }

    int i;
    for (i = 0; shared_data->text[child_id][i] != '\0'; ++i) {
        shared_data->encoded_texts[child_id][i] = (char)((int)shared_data->text[child_id][i] - 64);
    }
    shared_data->encoded_texts[child_id][i] = '\0';

    sb.sem_op = 1; // Release the semaphore
    if (semop(semaphore_id, &sb, 1) == -1) {
        perror("semop");
        exit(1);
    }
}

void signal_handler(int signum) {
    // Cleanup shared memory and semaphores
    shmdt(shared_data);
    shmctl(shmget(IPC_PRIVATE, sizeof(SharedData), IPC_CREAT | 0666), IPC_RMID, NULL);
    semctl(semaphore_id, 0, IPC_RMID);
    exit(signum);
}

int main() {
    // Create shared memory
    int shm_id = shmget(IPC_PRIVATE, sizeof(SharedData), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("shmget");
        exit(1);
    }

    // Attach shared memory
    shared_data = (SharedData *)shmat(shm_id, NULL, 0);
    if (shared_data == (void *)-1) {
        perror("shmat");
        exit(1);
    }

    // Create semaphore set
    semaphore_id = semget(IPC_PRIVATE, NUM_CHILDREN, IPC_CREAT | 0666);
    if (semaphore_id == -1) {
        perror("semget");
        exit(1);
    }

    // Set up signal handler for proper cleanup
    signal(SIGINT, signal_handler);

    // Read input text
    for (int i = 0; i < NUM_CHILDREN; i++) {
        printf("Enter %d part of text to encode: ", i+1);
        fgets(shared_data->text[i], TEXT_SIZE, stdin);
        shared_data->text[i][strcspn(shared_data->text[i], "\n")] = '\0'; // Remove newline character
    }

    // Set semaphore values
    for (int i = 0; i < NUM_CHILDREN; i++) {
        if (semctl(semaphore_id, i, SETVAL, 1) == -1) {
            perror("semctl");
            exit(1);
        }
    }

    // Fork child processes
    pid_t pid;
    for (int i = 0; i < NUM_CHILDREN; ++i) {
        pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid == 0) { // Child process
            encode_text(i);
            exit(0);
        }
    }

    // Parent process
    // Wait for all children to finish encoding their fragments
    for (int i = 0; i < NUM_CHILDREN; ++i) {
        wait(NULL);
    }

    // Print original text and encoded fragments
    printf("Original text: ");
    for (int i = 0; i < NUM_CHILDREN; ++i) {
        printf("%s ", shared_data->text[i]);
    }
    printf("\n");
    printf("Encoded fragments:\n");
    for (int i = 0; i < NUM_CHILDREN; ++i) {
        printf("Fragment %d: %s\n", i+1, shared_data->encoded_texts[i]);
    }

    // Cleanup shared memory and semaphores
    shmdt(shared_data);
    shmctl(shm_id, IPC_RMID, NULL);
    semctl(semaphore_id, 0, IPC_RMID);

    return 0;
}
