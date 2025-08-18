#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

int num_threads;
char *command;

void *thread_func(void *arg) {
    FILE *cmd = popen(command, "w");
    if (!cmd) {
        perror("popen");
        pthread_exit(NULL);
    }

    // Just for testing: send fixed data
    fprintf(cmd, "abc\n123\nabc123\nxyz\n");
    pclose(cmd);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 5 || strcmp(argv[1], "-n") != 0 || strcmp(argv[3], "-c") != 0) {
        fprintf(stderr, "Usage: cat file | ./parapipe -n <num_threads> -c \"cmd\"\n");
        exit(1);
    }

    num_threads = atoi(argv[2]);
    if (num_threads <= 0) {
        fprintf(stderr, "Invalid number of threads\n");
        exit(1);
    }

    command = argv[4];
    printf("Threads: %d, Commands: %s\n", num_threads, command);

    pthread_t threads[num_threads];
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, thread_func, NULL);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
