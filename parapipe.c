#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>

// ==================== Thread-Safe Queue ====================
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
// ============================================================

// ==================== Command Parsing =======================
char **parse_commands(char *cmd_str, int *num_cmds) {
    while (isspace(*cmd_str)) cmd_str++;

    int count = 1;
    char *p = cmd_str;
    while ((p = strstr(p, "->"))) {
        count++;
        p += 2;
    }

    char **cmds = malloc(count * sizeof(char *));
    if (!cmds) {
        perror("malloc");
        exit(1);
    }

    p = cmd_str;
    char *start = p;
    int i = 0;
    while ((p = strstr(p, "->"))) {
        char *end = p - 1;
        while (end > start && isspace(*end)) end--;
        end++;

        cmds[i] = malloc(end - start + 1);
        if (!cmds[i]) {
            perror("malloc");
            exit(1);
        }
        strncpy(cmds[i], start, end - start);
        cmds[i][end - start] = '\0';

        p += 2;
        while (isspace(*p)) p++;
        start = p;
        i++;
    }

    char *end = cmd_str + strlen(cmd_str) - 1;
    while (end > start && isspace(*end)) end--;
    end++;

    cmds[i] = malloc(end - start + 1);
    if (!cmds[i]) {
        perror("malloc");
        exit(1);
    }
    strncpy(cmds[i], start, end - start);
    cmds[i][end - start] = '\0';

    *num_cmds = count;
    return cmds;
}
// ============================================================

// =================== Worker & Receiver Structs ============
typedef struct {
    Queue *input_q;
    Queue *output_q;
    char **cmds;
    int num_cmds;
} WorkerArg;

typedef struct {
    Queue *q;
    int n; // number of workers to expect NULLs
} ReceiverArg;
// ============================================================

// =================== Thread Skeletons ======================
void *worker_func(void *arg) {
    WorkerArg *wa = (WorkerArg *)arg;
    // TODO: implement pipeline execution:
    // 1. dequeue from input_q
    // 2. run through cmds
    // 3. enqueue output lines to output_q
    // 4. enqueue NULL when finished
    return NULL;
}

void *receiver_func(void *arg) {
    ReceiverArg *ra = (ReceiverArg *)arg;
    Queue *q = ra->q;
    int n = ra->n;
    int ends = 0;
    while (ends < n) {
        char *line = dequeue(q);
        if (line == NULL) {
            ends++;
            continue;
        }
        printf("%s", line); // line already contains \n
        free(line);
    }
    return NULL;
}
// ============================================================

int main(int argc, char *argv[]) {
    if (argc != 5 || strcmp(argv[1], "-n") != 0 || strcmp(argv[3], "-c") != 0) {
        fprintf(stderr, "Usage: cat file | ./parapipe -n <num_threads> -c \"cmd1 -> cmd2\"\n");
        exit(1);
    }

    int num_threads = atoi(argv[2]);
    if (num_threads <= 0) {
        fprintf(stderr, "Invalid number of threads\n");
        exit(1);
    }

    char *cmd_str = argv[4];
    int num_cmds;
    char **cmds = parse_commands(cmd_str, &num_cmds);

    // Initialize input queues for workers
    Queue *input_queues = malloc(num_threads * sizeof(Queue));
    if (!input_queues) { perror("malloc"); exit(1); }
    for (int i = 0; i < num_threads; i++) init_queue(&input_queues[i]);

    // Initialize shared output queue
    Queue output_q;
    init_queue(&output_q);

    // Start receiver thread
    ReceiverArg rarg = {&output_q, num_threads};
    pthread_t receiver_tid;
    if (pthread_create(&receiver_tid, NULL, receiver_func, &rarg)) {
        perror("pthread_create"); exit(1);
    }

    // Start worker threads
    pthread_t *worker_tids = malloc(num_threads * sizeof(pthread_t));
    WorkerArg *wargs = malloc(num_threads * sizeof(WorkerArg));
    for (int i = 0; i < num_threads; i++) {
        wargs[i].input_q = &input_queues[i];
        wargs[i].output_q = &output_q;
        wargs[i].cmds = cmds;
        wargs[i].num_cmds = num_cmds;
        if (pthread_create(&worker_tids[i], NULL, worker_func, &wargs[i])) {
            perror("pthread_create"); exit(1);
        }
    }

    // Read stdin and distribute lines round-robin
    char *line = NULL;
    size_t len = 0;
    ssize_t read_len;
    int thread_idx = 0;
    while ((read_len = getline(&line, &len, stdin)) != -1) {
        char *dup = strdup(line);
        if (!dup) { perror("strdup"); exit(1); }
        enqueue(&input_queues[thread_idx], dup);
        thread_idx = (thread_idx + 1) % num_threads;
    }
    free(line);

    // Signal end to workers
    for (int i = 0; i < num_threads; i++) {
        enqueue(&input_queues[i], NULL);
    }

    // Join worker threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(worker_tids[i], NULL);
    }

    // Join receiver thread
    pthread_join(receiver_tid, NULL);

    // Cleanup
    for (int i = 0; i < num_cmds; i++) free(cmds[i]);
    free(cmds);
    free(input_queues);
    free(worker_tids);
    free(wargs);

    return 0;
}
