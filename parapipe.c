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

// ================= Thread-Safe Queue =================
typedef struct Node {
    char *data;
    struct Node *next;
} Node;

typedef struct {
    Node *head;
    Node *tail;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} Queue;

void init_queue(Queue *q) {
    q->head = q->tail = NULL;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
}

void enqueue(Queue *q, char *data) {
    Node *node = malloc(sizeof(Node));
    if (!node) {
        perror("malloc");
        exit(1);
    }
    node->data = data;
    node->next = NULL;
    pthread_mutex_lock(&q->lock);
    if (q->tail) {
        q->tail->next = node;
    } else {
        q->head = node;
    }
    q->tail = node;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
}

char *dequeue(Queue *q) {
    pthread_mutex_lock(&q->lock);
    while (q->head == NULL) {
        pthread_cond_wait(&q->cond, &q->lock);
    }
    Node *node = q->head;
    char *data = node->data;
    q->head = node->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    pthread_mutex_unlock(&q->lock);
    free(node);
    return data;
}
// =====================================================

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
