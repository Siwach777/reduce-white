#!/usr/bin/env python3
import subprocess
import time
import os
import signal
import sys

def get_daemon_pid():
    try:
        pid_str = subprocess.check_output(["pgrep", "-f", "reduce-white --daemon"]).decode().strip()
        if pid_str:
            return int(pid_str.split('\n')[0])
    except subprocess.CalledProcessError:
        pass
    return None

def get_memory_usage(pid):
    try:
        # Get memory usage in KB (RSS)
        rss = subprocess.check_output(["ps", "-o", "rss=", "-p", str(pid)]).decode().strip()
        return int(rss) / 1024.0  # Convert to MB
    except Exception:
        return 0.0

def main():
    print("--- Reduce White Point Benchmark ---")
    
    # Check if daemon is running, if not start it for benchmarking
    pid = get_daemon_pid()
    started_daemon = False
    
    if not pid:
        print("Daemon not running. Starting daemon in the background...")
        # Start daemon
        daemon_proc = subprocess.Popen(["./build/reduce-white", "--daemon"])
        time.sleep(1) # wait for it to initialize
        pid = daemon_proc.pid
        started_daemon = True
    else:
        print(f"Found running daemon with PID {pid}.")

    print("\nMeasuring Base Memory Footprint...")
    base_mem = get_memory_usage(pid)
    print(f"Base Memory (RSS): {base_mem:.2f} MB")

    iterations = 1000
    print(f"\nBenchmarking IPC Response Time ({iterations} sequential requests)...")
    start_time = time.time()
    
    for i in range(iterations):
        # We alternate between increasing and decreasing so we don't just max out opacity
        cmd = ["./build/reduce-white", "--increase"] if i % 2 == 0 else ["./build/reduce-white", "--decrease"]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
    end_time = time.time()
    
    total_time = end_time - start_time
    avg_latency = (total_time / iterations) * 1000 # in ms
    
    print(f"Total time for {iterations} requests: {total_time:.4f} seconds")
    print(f"Average latency per request: {avg_latency:.2f} ms")
    
    print("\nMeasuring Active Memory Footprint...")
    active_mem = get_memory_usage(pid)
    print(f"Active Memory (RSS) after stress test: {active_mem:.2f} MB")
    
    if started_daemon:
        print("\nCleaning up benchmarking daemon...")
        os.kill(pid, signal.SIGTERM)
        daemon_proc.wait()
    else:
        print("\nLeaving existing daemon running. Restoring to 0.3 opacity.")
        subprocess.run(["reduce-white", "--set", "0.3"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
    print("\nBenchmark Complete!")
    print("If memory usage stays constant and IPC is <50ms, resource usage is optimal.")

if __name__ == "__main__":
    main()
