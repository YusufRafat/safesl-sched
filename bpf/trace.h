#ifndef SAFESL_TRACE_H
#define SAFESL_TRACE_H

#include <linux/types.h>

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

#endif /* SAFESL_TRACE_H */
