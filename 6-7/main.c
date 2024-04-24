#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>

#define TEXT_SIZE 100
#define NUM_CHILDREN 5

typedef struct {
    char text[NUM_CHILDREN][TEXT_SIZE];
    char encoded_texts[NUM_CHILDREN][TEXT_SIZE];
    int encoded_count;
    sem_t semaphores[NUM_CHILDREN];
} SharedData;

SharedData *shared_data;

void encode_text(int child_id) {
    int i;
    sem_wait(&shared_data->semaphores[child_id]);
    for (i = 0; shared_data->text[child_id][i] != '\0'; ++i) {
        shared_data->encoded_texts[child_id][i] = (char)((int)shared_data->text[child_id][i] - 64);
    }
    shared_data->encoded_texts[child_id][i] = '\0';

    // Signal that this child has finished encoding its fragment
    sem_post(&shared_data->semaphores[child_id]);
}

void signal_handler(int signum) {
    // Cleanup shared memory and semaphores
    munmap(shared_data, sizeof(SharedData));
    shm_unlink("/shared_memory");
    exit(signum);
}

int main() {
    // Initialize shared memory
    int shm_fd = shm_open("/shared_memory", O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(SharedData));
    shared_data = (SharedData *)mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    // Initialize semaphores
    for (int i = 0; i < NUM_CHILDREN; ++i) {
        sem_init(&shared_data->semaphores[i], 1, 0);
    }

    // Set up signal handler for proper cleanup
    signal(SIGINT, signal_handler);

    // Read input text
    for (int i = 0; i < NUM_CHILDREN; i++) {
        printf("Enter %d part of text to encode: ", i+1);
        fgets(shared_data->text[i], TEXT_SIZE, stdin);
        shared_data->text[i][strcspn(shared_data->text[i], "\n")] = '\0'; // Remove newline character
    }

    // Fork child processes
    int i;
    pid_t pid;
    for (i = 0; i < NUM_CHILDREN; ++i) {
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
    for (i = 0; i < NUM_CHILDREN; ++i) {
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
    munmap(shared_data, sizeof(SharedData));
    shm_unlink("/shared_memory");
    for (int i = 0; i < NUM_CHILDREN; ++i) {
        sem_destroy(&shared_data->semaphores[i]);
    }

    return 0;
}
