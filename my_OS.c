#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <string.h>
#include <wait.h>

#define MAX_CHILDREN 10
#define SHM_SIZE 256

// Shared memory and semaphore IDs
int shm_id;
char *shared_memory;
int sem_id;

// Structure to store child process data
typedef struct {
    pid_t pid;
    int active;
    int msgs_received;
    int start_time;
    int end_time;
} ChildProcess;

// 
ChildProcess children[MAX_CHILDREN];

// Semaphore operations
void sem_op(int sem_num, int op) {
    struct sembuf sop;
    sop.sem_num = sem_num;
    sop.sem_op = op;
    sop.sem_flg = 0;
    if (semop(sem_id, &sop, 1) == -1) {
        perror("semop");
        exit(1);
    }
}

// Spawn a new child process
void spawn_child(int index, int timestamp) {
    if ((children[index].pid = fork()) == 0) {
        // Child process
        children[index].start_time = timestamp; // Initialize start time for child
        while (1) {
            sem_op(index, -1); // Wait for parent
            if (strcmp(shared_memory, "TERMINATE") == 0) {
                printf("Child %d terminated:\n\tMessages received: %d\n", index + 1, children[index].msgs_received);
                exit(0);
            }
            printf("Child %d received: [%s]\n", index + 1, shared_memory);
            children[index].msgs_received++;
        }
        exit(0);
    } else {
        // Parent process
        children[index].active = 1;
        children[index].msgs_received = 0;
        children[index].start_time = timestamp;
        printf("Spawned child %d with PID %d\n", index + 1, children[index].pid);
    }
}

// Terminate a child process
void terminate_child(int index, int timestamp) {
    if (children[index].active) {
        strcpy(shared_memory, "TERMINATE");
        sem_op(index, 1); // Notify child
        waitpid(children[index].pid, NULL, 0);
        children[index].active = 0;
        children[index].end_time = timestamp;
		printf("\tNumber of steps: %d\n", children[index].end_time - children[index].start_time);
    }
}

int main(int argc, char *argv[]) {
	
	int stop_timestamp; //Holding timestamp for exit
	
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <commands_file> <text_file> <max_children>\n", argv[0]);
        exit(1);
    }

    int max_children = atoi(argv[3]);
    if (max_children > MAX_CHILDREN || max_children <= 0) {
        fprintf(stderr, "max_children must be between 1 and %d\n", MAX_CHILDREN);
        exit(1);
    }

    // Create shared memory
    shm_id = shmget(IPC_PRIVATE, SHM_SIZE, IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("shmget");
        exit(1);
    }
    shared_memory = (char *)shmat(shm_id, NULL, 0);
    if (shared_memory == (char *)-1) {
        perror("shmat");
        exit(1);
    }

    // Create semaphores
    sem_id = semget(IPC_PRIVATE, max_children, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("semget");
        exit(1);
    }
    for (int i = 0; i < max_children; i++) {
        if (semctl(sem_id, i, SETVAL, 0) == -1) {
            perror("semctl");
            exit(1);
        }
    }

    // Initialize child data
    for (int i = 0; i < max_children; i++) {
        children[i].active = 0;
    }

    // Open files
    FILE *commands_file = fopen(argv[1], "r");
    if (!commands_file) {
        perror("fopen commands_file");
        exit(1);
    }
    FILE *text_file = fopen(argv[2], "r");
    if (!text_file) {
        perror("fopen text_file");
        fclose(commands_file);
        exit(1);
    }

    char line[256];
    int current_timestamp = 0;

    while (fgets(line, sizeof(line), commands_file)) {
        int timestamp;
        char process[5];
        char command;

        if (sscanf(line, "%d %s %c", &timestamp, process, &command) == 3) {
            int child_index;
            if (process[0] == 'C' && sscanf(&process[1], "%d", &child_index) == 1) {
                child_index--; // Convert to 0-based index
            } else {
                fprintf(stderr, "Invalid process identifier: %s\n", process);
                continue;
            }

            while (current_timestamp < timestamp) {
               
                int active_children = 0;
				// Counts active children
                for (int i = 0; i < max_children; i++) {
                    if (children[i].active) {
                        active_children++;
                    }
                }
				// If at least 1 ective, Send random text to a random child
                if (active_children > 0) {
                    int random_child = rand() % max_children;
                    while (!children[random_child].active) {
                        random_child = (random_child + 1) % max_children;
                    }

                    if (fgets(line, sizeof(line), text_file)) {
                        line[strcspn(line, "\n")] = '\0'; // Remove newline
                        strcpy(shared_memory, line);
                        sem_op(random_child, 1);
                    } else {
                        rewind(text_file); // Reset to beginning of file if EOF
                    }
                }

                sleep(1);
                current_timestamp++;
            }

            if (child_index < 0 || child_index >= max_children) {
                fprintf(stderr, "Invalid process index: %s\n", process);
                continue;
            }

            if (command == 'S') {
                if (!children[child_index].active) {
                    spawn_child(child_index, timestamp);
                } else {
                    fprintf(stderr, "Child %d is already active\n", child_index + 1);
                }
            } else if (command == 'T') {
                terminate_child(child_index, timestamp);
            } else {
                fprintf(stderr, "Unknown command: %c\n", command);
            }
        } else if (sscanf(line, "%d EXIT", &timestamp) == 1) {
			stop_timestamp = timestamp;
            printf("EXIT command received at timestamp %d. Terminating.\n", timestamp);
            break;
        } else {
            fprintf(stderr, "Invalid command format: %s", line);
        }
    }

    // Cleanup
    fclose(commands_file);
    fclose(text_file);

    for (int i = 0; i < max_children; i++) {
        if (children[i].active) {
            terminate_child(i, stop_timestamp);
        }
    }

    shmdt(shared_memory);
    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);

    return 0;
}