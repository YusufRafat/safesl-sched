#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

// 1. الـ Struct الأساسي اللي بيحتوي على الـ Features المطلوبة (مطابق لـ trace.h)
struct sched_event {
    __u32 pid;
    __u32 core_id;
    __u32 rq_depth;
    __s32 nice_value;
    __u32 preempt_state;
    __u64 arrival_timestamp;
    __u64 wait_time;
    __u64 burst_time;
};

// Struct داخلي بنحفظ فيه التوقيتات المؤقتة لكل Thread
struct task_timing {
    __u64 arrival_ts;
    __u64 start_ts;
    __u32 rq_depth_at_wakeup;
};

// جدول (Hash Map) لحفظ بيانات التوقيت لكل PID
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);
    __type(value, struct task_timing);
} timing_map SEC(".maps");

// قناة الإرسال (Ring Buffer) اللي بتبعت البيانات لبرنامج الـ C++ في User-space
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} events SEC(".maps");

// دالة مساعدة بتجيب عدد العمليات اللي مستنية في الطابور (Queue Depth)
static __always_inline __u32 get_rq_depth(struct task_struct *p) {
    struct cfs_rq *cfs_rq = BPF_CORE_READ(p, se.cfs_rq);
    if (!cfs_rq)
        return 0;
    return BPF_CORE_READ(cfs_rq, h_nr_queued);
}

// =========================================================================
// Hook 1: بيشتغل لما الـ Thread يصحى ويدخل طابور الانتظار (لسه مشتغلش على الـ CPU)
// =========================================================================
SEC("tp_btf/sched_wakeup")
int BPF_PROG(handle_sched_wakeup, struct task_struct *p) {
    __u32 pid = BPF_CORE_READ(p, pid);
    if (pid == 0) // بنتجاهل عملية الخمول (Idle task)
        return 0;

    __u64 now = bpf_ktime_get_ns(); // الوقت الحالي بالنانو ثانية
    struct task_timing *info = bpf_map_lookup_elem(&timing_map, &pid);

    if (info) {
        info->arrival_ts = now;
        info->rq_depth_at_wakeup = get_rq_depth(p);
    } else {
        struct task_timing new_info = {
            .arrival_ts = now,
            .start_ts = 0,
            .rq_depth_at_wakeup = get_rq_depth(p),
        };
        bpf_map_update_elem(&timing_map, &pid, &new_info, BPF_ANY);
    }
    return 0;
}

// =========================================================================
// Hook 2: بيشتغل لما الـ Core يطلع Thread قديم (prev) ويدخل Thread جديد (next)
// =========================================================================
SEC("tp_btf/sched_switch")
int BPF_PROG(handle_sched_switch, bool preempt, struct task_struct *prev, struct task_struct *next) {
    __u64 now = bpf_ktime_get_ns();
    __u32 core_id = bpf_get_smp_processor_id(); // رقم الـ Core الحالي

    // أولاً: حساب بيانات الـ Thread القديم اللي خلص وخرج من الـ CPU
    __u32 prev_pid = BPF_CORE_READ(prev, pid);
    if (prev_pid != 0) {
        struct task_timing *prev_info = bpf_map_lookup_elem(&timing_map, &prev_pid);
        if (prev_info && prev_info->start_ts > 0) {
            // حجز مكان في الـ Ring Buffer لإرسال البيانات
            struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
            if (e) {
                e->pid = prev_pid;
                e->core_id = core_id;
                e->rq_depth = prev_info->rq_depth_at_wakeup;

                // حساب الأولوية (Nice value)
                int static_prio = BPF_CORE_READ(prev, static_prio);
                e->nice_value = static_prio - 120;

                // حالة الخروج: 1 لو اتطرد بالعافية (Preempted)، 0 لو خرج بمزاجه (I/O Sleep)
                e->preempt_state = preempt ? 1 : 0;

                e->arrival_timestamp = prev_info->arrival_ts;
                e->wait_time = (prev_info->start_ts > prev_info->arrival_ts) ?
                               (prev_info->start_ts - prev_info->arrival_ts) : 0;
                e->burst_time = (now > prev_info->start_ts) ?
                                (now - prev_info->start_ts) : 0;

                // إرسال البيانات فعلياً
                bpf_ringbuf_submit(e, 0);
            }
            prev_info->start_ts = 0;
        }
    }

    // ثانياً: تسجيل وقت بداية شغل الـ Thread الجديد اللي دخل الـ CPU دلوقتي
    __u32 next_pid = BPF_CORE_READ(next, pid);
    if (next_pid != 0) {
        struct task_timing *next_info = bpf_map_lookup_elem(&timing_map, &next_pid);
        if (next_info) {
            next_info->start_ts = now;
        } else {
            struct task_timing new_info = {
                .arrival_ts = now,


                .start_ts = now,
                .rq_depth_at_wakeup = get_rq_depth(next),
            };
            bpf_map_update_elem(&timing_map, &next_pid, &new_info, BPF_ANY);
        }
    }

    return 0;
}

char LICENSE[] SEC("license") = "GPL";



