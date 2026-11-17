import os
import subprocess
import time
import random
import re
import csv

ITERATIONS = 2        # Change to 20 for your final dataset run
TEST_DURATION = 10    # Change to 60 for full 60-second runs
CSV_FILE = "baseline_metrics.csv"

def parse_redis_benchmark(output):
    tput_match = re.search(r"throughput summary:\s*([\d.]+)\s*requests per second", output)
    throughput = float(tput_match.group(1)) if tput_match else 0.0

    p99 = 0.0
    lat_match = re.search(
        r"latency summary \(msec\):\s+avg\s+min\s+p50\s+p95\s+p99\s+max\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)",
        output
    )
    if lat_match:
        p99 = float(lat_match.group(5))

    return throughput, p99

# Calculate 10%, 18%, and 25% of the machine's available RAM in MB
avail_mb = (os.sysconf('SC_PAGE_SIZE') * os.sysconf('SC_AVPHYS_PAGES')) // (1024 * 1024)
vm_options = [
    f"{max(64, int(avail_mb * 0.10))}M",
    f"{max(128, int(avail_mb * 0.18))}M",
    f"{max(192, int(avail_mb * 0.25))}M"
]

with open(CSV_FILE, mode="w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["Iteration", "CpuWorkers", "VmBytes", "Throughput_ReqsPerSec", "P99_TailLatency_ms"])

print("==================================================")
print("   SAFESL-SCHED: SYSTEMS WORKLOAD GENERATOR")
print(f"   Auto-scaled VM Memory Tiers: {vm_options}")
print("==================================================\n")

for i in range(1, ITERATIONS + 1):
    print(f"[+] --- Starting Test Iteration {i}/{ITERATIONS} ---")

    # 1. Start VIP application (redis-server)
    print("[*] Launching VIP application (redis-server)...")
    redis_proc = subprocess.Popen(
        ["redis-server", "--port", "6379", "--save", "", "--appendonly", "no"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    time.sleep(1)

    # 2. Inject randomized CPU & Memory pressure with --oom-avoid
    cpu_workers = random.randint(2, 4)
    vm_workers = random.randint(1, 2)
    vm_bytes = random.choice(vm_options)
    print(f"[*] Injecting background pressure: --cpu {cpu_workers} --vm {vm_workers} --vm-bytes {vm_bytes}")

    stress_proc = subprocess.Popen(
        [
            "stress-ng",
            "--cpu", str(cpu_workers),
            "--vm", str(vm_workers),
            "--vm-bytes", vm_bytes,
            "--oom-avoid",
            "--timeout", f"{TEST_DURATION}s"
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    # 3. Hammer Redis with redis-benchmark for the duration of the stress window
    print(f"[*] Hammering Redis with redis-benchmark for ~{TEST_DURATION}s...")
    start_time = time.time()
    tputs, p99s = [], []

    while time.time() - start_time < TEST_DURATION:
        bench = subprocess.run(
            ["redis-benchmark", "-h", "127.0.0.1", "-p", "6379", "-t", "get", "-n", "100000", "-c", "50", "--precision", "3"],
            capture_output=True,
            text=True
        )
        t, p = parse_redis_benchmark(bench.stdout)
        if t > 0:
            tputs.append(t)
            p99s.append(p)

    avg_tput = round(sum(tputs) / len(tputs), 2) if tputs else 0.0
    max_p99 = round(max(p99s), 3) if p99s else 0.0

    # 4. Clean up background noise and Redis
    print("[*] Stopping background noise and database...")
    stress_proc.terminate()
    stress_proc.wait()
    redis_proc.terminate()
    redis_proc.wait()

    print(f"[✓] Iteration {i} Complete -> Throughput: {avg_tput} req/sec | P99 Latency: {max_p99} ms\n")

    with open(CSV_FILE, mode="a", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([i, cpu_workers, vm_bytes, avg_tput, max_p99])

print(f"[+] All tests finished! Baseline metrics successfully logged to {CSV_FILE}")
