# SafeSL-Sched: Automated Systems Benchmarking Suite

This repository contains the automated performance benchmarking harness for **SafeSL-Sched**. The suite is designed to rigorously evaluate and compare kernel scheduler performance (such as the default Linux scheduler versus custom schedulers like `SafeSL-Sched`) by measuring **Throughput (req/sec)** and **p99 Tail Latency (ms)** on Ubuntu VMs under controlled, randomized CPU and memory contention.

## Table of Contents

1. [Prerequisites & Installation](#1-prerequisites--installation)

2. [Component Breakdown](#2-component-breakdown)

   * `systems_benchmark.py`

   * `baseline_metrics.csv`

3. [Standalone Comparison Reference (Unloaded Baseline)](#3-standalone-comparison-reference-unloaded-baseline)

4. [How to Run the Full Workflow](#4-how-to-run-the-full-workflow)

5. [Configuration & Tuning](#5-configuration--tuning)

## 1. Prerequisites & Installation

Before running the benchmark suite, ensure your target Ubuntu environment has the required dependencies installed. Run the following commands in your terminal:

```
# Update package lists and install required tools
sudo apt update
sudo apt install -y redis-server redis-tools stress-ng python3

```

* **`redis-server` & `redis-tools`**: Provides the target database and the native RESP traffic generator (`redis-benchmark`).

* **`stress-ng`**: Used for injecting controlled, multi-threaded CPU and memory background noise.

* **`python3`**: Executes the automation orchestrator.

## 2. Component Breakdown

### A. `systems_benchmark.py` (The Automation Orchestrator)

`systems_benchmark.py` is a Python script that automates the entire test life cycle to ensure data consistency and reproducibility across multiple iterations. For every test iteration, the script performs the following sequence:

1. **Spins up an isolated instance of `redis-server`** with persistence disabled (`--save "" --appendonly no`) to keep measurements strictly in-memory.

2. **Launches `stress-ng`** in the background, dynamically calculating memory tiers based on available RAM, and applying randomized CPU workers (`--cpu 2` to `4`), memory workers (`--vm 1` to `2`), and safe memory thresholds (`--oom-avoid`) to prevent system crashes or disk swapping.

3. **Hammers Redis using `redis-benchmark`** continuously throughout the test window, capturing raw RESP protocol responses.

4. **Parses the output** using regular expressions to extract real-time average throughput and worst-case p99 tail latency.

5. **Cleans up** by terminating background stress and database processes safely before starting the next iteration.

### B. `baseline_metrics.csv` (The Results Spreadsheet)

`baseline_metrics.csv` is the structured output file automatically generated and populated by `systems_benchmark.py`.

* **Behavior:** When the script starts, it initializes a fresh CSV file with column headers. As each iteration completes, it **appends** a new row of performance data so historical records are preserved throughout the run.

* **Schema Columns:**

  * `Iteration`: The sequential test run index (e.g., 1, 2, ... 20).

  * `CpuWorkers`: Number of active `stress-ng` CPU threads saturating the run-queue.

  * `VmBytes`: The amount of virtual memory thrashed by `stress-ng` (auto-scaled relative to the host VM's available RAM).

  * `Throughput_ReqsPerSec`: Average successful Redis `GET` requests processed per second under that specific contention tier.

  * `P99_TailLatency_ms`: The 99th percentile response latency in milliseconds, highlighting worst-case scheduler jitter.

## 3. Standalone Comparison Reference (Unloaded Baseline)

To understand how much degradation background contention causes, you can establish an **unloaded reference point** (zero noise) before running the automated script.

Run this single-line command in your terminal:

```
redis-server --daemonize yes --save "" --appendonly no && redis-benchmark -h 127.0.0.1 -p 6379 -t get -n 100000 -c 50 --precision 3 && redis-cli shutdown

```

* **What to analyze:** Look at the `throughput summary` and `p99` lines in the output.

* **Expected Result:** When you run `systems_benchmark.py` afterward under `stress-ng` noise, your throughput will drop and your p99 latency will increase—proving that your CPU and memory contention workload is actively challenging the scheduler.

## 4. How to Run the Full Workflow

1. **Clone or copy the repository files** (`systems_benchmark.py`) into your workspace.

2. **Run the baseline reference test** (Optional, detailed in Section 3) to record unconstrained hardware performance.

3. **Execute the automated benchmark script:**

   ```
   python3 systems_benchmark.py
   
   ```

4. **Inspect the results:** Open `baseline_metrics.csv` to review the generated performance points.

## 5. Configuration & Tuning

At the top of `systems_benchmark.py`, you can adjust the test scale constants depending on whether you are verifying or collecting final data:

* `ITERATIONS = 2` — Set to a low number (e.g., 2) for quick script verification. **Set to 20+** for your final data collection dataset.

* `TEST_DURATION = 10` — Duration (in seconds) of stress-ng and benchmark hammer per iteration. **Set to 60** for reliable, statistically stable production runs.