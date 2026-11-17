#!/bin/bash
set -e

echo "=== 1. Installing eBPF & Kernel Toolchain ==="
sudo apt update
sudo apt install -y clang llvm libbpf-dev linux-tools-common linux-tools-$(uname -r) linux-headers-$(uname -r) build-essential pkg-config git

echo "=== 2. Installing Python Ecosystem ==="
sudo apt install -y python3 python3-pip python3-venv

echo "=== 3. Installing Evaluation Workload Tools ==="
sudo apt install -y redis-server stress-ng wrk

echo "=== Setup Completed Successfully! ==="
