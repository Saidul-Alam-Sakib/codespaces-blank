#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>

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

// ================ Command Parser =====================
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
// =====================================================

typedef struct {
    Queue *input_q;
    Queue *output_q;
    char **cmds;
    int num_cmds;
} WorkerArg;

typedef struct {
    Queue *q;
    int n;
} ReceiverArg;

// ================ Worker Function ====================
void *worker_func(void *arg) {
    WorkerArg *wa = (WorkerArg *)arg;
    int k = wa->num_cmds;
    int num_pipes = k + 1;
    int **pipefds = malloc(num_pipes * sizeof(int *));
    for (int i = 0; i < num_pipes; i++) {
        pipefds[i] = malloc(2 * sizeof(int));
        if (pipe(pipefds[i]) == -1) {
            perror("pipe");
            exit(1);
        }
    }

    pid_t *pids = malloc(k * sizeof(pid_t));
    for (int j = 0; j < k; j++) {
        pids[j] = fork();
        if (pids[j] == -1) {
            perror("fork");
            exit(1);
        }
        if (pids[j] == 0) {
            dup2(pipefds[j][0], STDIN_FILENO);
            dup2(pipefds[j + 1][1], STDOUT_FILENO);
            for (int p = 0; p < num_pipes; p++) {
                close(pipefds[p][0]);
                close(pipefds[p][1]);
            }
            execl("/bin/sh", "sh", "-c", wa->cmds[j], (char *)NULL);
            perror("execl");
            exit(1);
        } else {
            close(pipefds[j][0]);
            close(pipefds[j + 1][1]);
        }
    }

    int write_fd = pipefds[0][1];
    int read_fd = pipefds[num_pipes - 1][0];
    fcntl(read_fd, F_SETFL, O_NONBLOCK);

    while (1) {
        char *line = dequeue(wa->input_q);
        if (line == NULL) break;
        write(write_fd, line, strlen(line));
        free(line);
    }
    close(write_fd);

    char buf[4096];
    char *out_buf = NULL;
    size_t out_size = 0;
    ssize_t r;
    while (1) {
        r = read(read_fd, buf, sizeof(buf));
        if (r == 0) break;
        if (r == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000);
                continue;
            } else {
                perror("read");
                break;
            }
        }
        out_buf = realloc(out_buf, out_size + r);
        if (!out_buf) {
            perror("realloc");
            exit(1);
        }
        memcpy(out_buf + out_size, buf, r);
        out_size += r;

        char *ptr = out_buf;
        char *nl;
        while ((nl = memchr(ptr, '\n', out_size - (ptr - out_buf)))) {
            *nl = '\0';
            enqueue(wa->output_q, strdup(ptr));
            ptr = nl + 1;
        }
        size_t leftover = out_size - (ptr - out_buf);
        memmove(out_buf, ptr, leftover);
        out_size = leftover;
    }
    if (out_size > 0) {
        out_buf = realloc(out_buf, out_size + 1);
        out_buf[out_size] = '\0';
        enqueue(wa->output_q, strdup(out_buf));
    }
    free(out_buf);
    close(read_fd);

    for (int j = 0; j < k; j++) {
        waitpid(pids[j], NULL, 0);
    }

    for (int i = 0; i < num_pipes; i++) {
        free(pipefds[i]);
    }
    free(pipefds);
    free(pids);

    enqueue(wa->output_q, NULL);
    return NULL;
}
// =====================================================

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
        printf("%s\n", line);
        free(line);
    }
    return NULL;
}

// ====================== MAIN =========================
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

    Queue *input_queues = malloc(num_threads * sizeof(Queue));
    if (!input_queues) {
        perror("malloc");
        exit(1);
    }
    for (int i = 0; i < num_threads; i++) {
        init_queue(&input_queues[i]);
    }

    Queue output_q;
    init_queue(&output_q);

    ReceiverArg rarg = {&output_q, num_threads};
    pthread_t receiver_tid;
    if (pthread_create(&receiver_tid, NULL, receiver_func, &rarg)) {
        perror("pthread_create");
        exit(1);
    }

    pthread_t *worker_tids = malloc(num_threads * sizeof(pthread_t));
    WorkerArg *wargs = malloc(num_threads * sizeof(WorkerArg));
    for (int i = 0; i < num_threads; i++) {
        wargs[i].input_q = &input_queues[i];
        wargs[i].output_q = &output_q;
        wargs[i].cmds = cmds;
        wargs[i].num_cmds = num_cmds;
        if (pthread_create(&worker_tids[i], NULL, worker_func, &wargs[i])) {
            perror("pthread_create");
            exit(1);
        }
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read_len;
    int thread_idx = 0;
    while ((read_len = getline(&line, &len, stdin)) != -1) {
        char *dup = strdup(line);
        if (!dup) {
            perror("strdup");
            exit(1);
        }
        enqueue(&input_queues[thread_idx], dup);
        thread_idx = (thread_idx + 1) % num_threads;
    }
    free(line);

    for (int i = 0; i < num_threads; i++) {
        enqueue(&input_queues[i], NULL);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(worker_tids[i], NULL);
    }

    pthread_join(receiver_tid, NULL);

    for (int i = 0; i < num_cmds; i++) free(cmds[i]);
    free(cmds);
    free(input_queues);
    free(worker_tids);
    free(wargs);

    return 0;
}
