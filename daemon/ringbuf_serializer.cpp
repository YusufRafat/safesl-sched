#include <iostream>
#include <fstream>
#include <csignal>
#include <atomic>
#include <cstring>
#include <bpf/libbpf.h>
#include "../bpf/trace.h"

// Uncomment the line below once the Kernel team pushes their .bpf.c and you generate the skeleton:
// #define USE_BPF_SKELETON
#ifdef USE_BPF_SKELETON
#include "../bpf/tracer.skel.h"
#endif

static std::atomic<bool> running(true);
static std::ofstream csv_file;
static uint64_t event_count = 0;

void handle_signal(int sig) {
    running = false;
}

// Callback triggered by libbpf whenever a new sched_event arrives in the Ring Buffer
static int handle_event(void *ctx, void *data, size_t data_sz) {
    if (data_sz < sizeof(struct sched_event)) {
        std::cerr << "[Warning] Received incomplete event packet (" << data_sz << " bytes)\n";
        return 0;
    }

    const struct sched_event *ev = static_cast<const struct sched_event *>(data);

    csv_file << ev->pid << ","
             << ev->core_id << ","
             << ev->rq_depth << ","
             << ev->nice_value << ","
             << ev->preempt_state << ","
             << ev->arrival_timestamp << ","
             << ev->wait_time << ","
             << ev->burst_time << "\n";

    event_count++;
    return 0;
}

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // 1. Open dataset.csv and write the exact 8-column header
    csv_file.open("dataset.csv", std::ios::out | std::ios::trunc);
    if (!csv_file.is_open()) {
        std::cerr << "[Error] Failed to open dataset.csv for writing!\n";
        return 1;
    }
    csv_file << "pid,core_id,rq_depth,nice_value,preempt_state,arrival_timestamp,wait_time,burst_time\n";

    struct ring_buffer *rb = nullptr;
    int ringbuf_fd = -1;

#ifdef USE_BPF_SKELETON
    // 2. Load and attach the Kernel eBPF program via bpftool-generated skeleton
    struct tracer_bpf *skel = tracer_bpf__open_and_load();
    if (!skel) {
        std::cerr << "[Error] Failed to open and load BPF skeleton!\n";
        csv_file.close();
        return 1;
    }
    if (tracer_bpf__attach(skel) != 0) {
        std::cerr << "[Error] Failed to attach BPF hooks!\n";
        tracer_bpf__destroy(skel);
        csv_file.close();
        return 1;
    }
    ringbuf_fd = bpf_map__fd(skel->maps.events);
#else
    std::cout << "[SafeSL-Daemon] Compiled with libbpf successfully.\n";
    std::cout << "[SafeSL-Daemon] Waiting for Kernel BPF skeleton integration (enable USE_BPF_SKELETON once tracer.skel.h is generated).\n";
    csv_file.flush();
    csv_file.close();
    return 0;
#endif

    // 3. Initialize the libbpf Ring Buffer consumer
    rb = ring_buffer__new(ringbuf_fd, handle_event, nullptr, nullptr);
    if (!rb) {
        std::cerr << "[Error] Failed to create BPF ring buffer!\n";
#ifdef USE_BPF_SKELETON
        tracer_bpf__destroy(skel);
#endif
        csv_file.close();
        return 1;
    }

    std::cout << "[SafeSL-Daemon] Listening to BPF Ring Buffer -> dataset.csv (Press Ctrl+C to stop)...\n";

    // 4. High-efficiency polling loop
    while (running) {
        int err = ring_buffer__poll(rb, 100 /* timeout in ms */);
        if (err == -EINTR) {
            break;
        }
        if (err < 0) {
            std::cerr << "[Error] Error polling ring buffer: " << err << "\n";
            break;
        }
    }

    // 5. Graceful shutdown and cleanup
    ring_buffer__free(rb);
#ifdef USE_BPF_SKELETON
    tracer_bpf__destroy(skel);
#endif
    csv_file.flush();
    csv_file.close();

    std::cout << "\n[SafeSL-Daemon] Stopped cleanly. Total events captured: " << event_count << "\n";
    return 0;
}
