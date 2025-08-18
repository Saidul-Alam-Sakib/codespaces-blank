#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>

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

// ============== Command Parsing ======================
char **parse_commands(char *cmd_str, int *num_cmds) {
    // Trim leading spaces
    while (isspace(*cmd_str)) cmd_str++;

    // Count commands
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
        // Trim trailing spaces before ->
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

        // Skip -> and trim leading spaces after
        p += 2;
        while (isspace(*p)) p++;
        start = p;
        i++;
    }

    // Last command
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
// =====================================================

int main(int argc, char *argv[]) {
    if (argc != 5 || strcmp(argv[1], "-n") != 0 || strcmp(argv[3], "-c") != 0) {
        fprintf(stderr, "Usage: cat file | ./parapipe -n <num_threads> -c \"cmd1 -> cmd2\"\n");
        exit(1);
    }

    num_threads = atoi(argv[2]);
    if (num_threads <= 0) {
        fprintf(stderr, "Invalid number of threads\n");
        exit(1);
    }

    char *cmd_str = argv[4];

    int num_cmds;
    char **cmds = parse_commands(cmd_str, &num_cmds);

    printf("Threads: %d, Command count: %d\n", num_threads, num_cmds);  // Debug
    for (int i = 0; i < num_cmds; i++) {
        printf("Cmd %d: %s\n", i, cmds[i]);
    }

    // Free parsed commands
    for (int i = 0; i < num_cmds; i++) free(cmds[i]);
    free(cmds);

    // (Threads and queue setup will be used in later commits)
    return 0;
}
