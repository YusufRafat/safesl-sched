#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "../bpf/trace.h"

static volatile sig_atomic_t exiting = 0;
static FILE *csv_file = NULL;

/* Handle SIGINT (Ctrl+C) and SIGTERM for graceful shutdown */
static void sig_handler(int sig) {
    (void)sig;
    exiting = 1;
}

/* Callback triggered by libbpf whenever a new sched_event arrives in the Ring Buffer */
__attribute__((unused)) static int handle_event(void *ctx, void *data, size_t data_sz) {
    (void)ctx;
    if (data_sz < sizeof(struct sched_event) || !csv_file) {
        return 0;
    }

    const struct sched_event *e = (const struct sched_event *)data;

    fprintf(csv_file, "%u,%u,%u,%d,%u,%llu,%llu,%llu\n",
            e->pid,
            e->core_id,
            e->rq_depth,
            e->nice_value,
            e->preempt_state,
            (unsigned long long)e->arrival_timestamp,
            (unsigned long long)e->wait_time,
            (unsigned long long)e->burst_time);

    fflush(csv_file);
    return 0;
}

int main(void) {
    struct ring_buffer *rb = NULL;
    int err = 0;

    /* 1. Register termination signals */
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* 2. Open dataset.csv and write the 8-column header matching bpf/trace.h */
    csv_file = fopen("dataset.csv", "w");
    if (!csv_file) {
        perror("[SafeSL-Daemon] Failed to open dataset.csv");
        return 1;
    }

    fprintf(csv_file, "pid,core_id,rq_depth,nice_value,preempt_state,arrival_timestamp,wait_time,burst_time\n");
    fflush(csv_file);

    printf("[SafeSL-Daemon] Compiled in C with libbpf successfully.\n");

    /* 3. Ring Buffer setup (uncomment once Kernel BPF skeleton is generated) */
#ifdef USE_BPF_SKELETON
    /*
    struct tracer_bpf *skel = tracer_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "[SafeSL-Daemon] Failed to open and load BPF skeleton\n");
        fclose(csv_file);
        return 1;
    }
    err = tracer_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "[SafeSL-Daemon] Failed to attach BPF skeleton\n");
        tracer_bpf__destroy(skel);
        fclose(csv_file);
        return 1;
    }
    rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), handle_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "[SafeSL-Daemon] Failed to create ring buffer\n");
        tracer_bpf__destroy(skel);
        fclose(csv_file);
        return 1;
    }
    */
#else
    printf("[SafeSL-Daemon] Waiting for Kernel BPF skeleton integration (enable USE_BPF_SKELETON once tracer.skel.h is generated).\n");
#endif

    /* 4. High-efficiency polling loop */
    while (!exiting && rb != NULL) {
        err = ring_buffer__poll(rb, 100 /* timeout, ms */);
        if (err == -EINTR) {
            err = 0;
            break;
        }
        if (err < 0) {
            fprintf(stderr, "[SafeSL-Daemon] Error polling ring buffer: %d\n", err);
            break;
        }
    }

    /* 5. Graceful cleanup */
    if (rb) {
        ring_buffer__free(rb);
    }
    if (csv_file) {
        fflush(csv_file);
        fclose(csv_file);
    }

    return err < 0 ? 1 : 0;
}
