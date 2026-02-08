#!/bin/bash

# Performance tuning script for Linux systems
# Requires root privileges for most operations

echo "=== System Performance Tuning ==="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo "Warning: Not running as root. Some optimizations will be skipped."
    echo "Run with sudo for full optimizations."
    echo ""
fi

# CPU Information
echo "CPU Information:"
echo "  Cores: $(nproc)"
echo "  Architecture: $(uname -m)"
lscpu | grep "Model name" || echo "  (lscpu not available)"
echo ""

# 1. CPU Governor
echo "1. Setting CPU governor to 'performance'..."
if [ "$EUID" -eq 0 ]; then
    for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        if [ -f "$cpu" ]; then
            echo "performance" > "$cpu" 2>/dev/null && echo "  Set $(dirname $cpu)" || true
        fi
    done
else
    echo "  Skipped (requires root)"
fi
echo ""

# 2. Disable CPU frequency scaling
echo "2. Checking CPU frequency scaling..."
if [ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
    current=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)
    echo "  Intel P-State turbo: $([ $current -eq 0 ] && echo 'enabled' || echo 'disabled')"
else
    echo "  Intel P-State not available"
fi
echo ""

# 3. Check huge pages
echo "3. Huge Pages configuration:"
if [ -f /proc/meminfo ]; then
    grep -i hugepage /proc/meminfo | head -5
else
    echo "  /proc/meminfo not available"
fi
echo ""

# 4. IRQ affinity (if running as root)
echo "4. IRQ Affinity:"
if [ "$EUID" -eq 0 ]; then
    # This is a simplified example - in production you'd want to bind
    # network IRQs to specific cores
    echo "  Network IRQs could be bound to specific cores"
    echo "  (Requires network adapter specific configuration)"
else
    echo "  Skipped (requires root)"
fi
echo ""

# 5. Check NUMA configuration
echo "5. NUMA Configuration:"
if command -v numactl &> /dev/null; then
    numactl --hardware | head -10
else
    echo "  numactl not installed"
fi
echo ""

# 6. Recommended kernel parameters
echo "6. Recommended kernel parameters (add to /etc/sysctl.conf):"
echo "  # Increase maximum locked memory"
echo "  kernel.shmmax = 68719476736"
echo "  # Reduce context switching"
echo "  kernel.sched_min_granularity_ns = 10000000"
echo "  kernel.sched_wakeup_granularity_ns = 15000000"
echo ""

# 7. Check current limits
echo "7. Current process limits:"
ulimit -a | grep -E "(stack size|max locked memory|max memory size)"
echo ""

echo "=== Tuning Recommendations ==="
echo ""
echo "For best performance:"
echo "  1. Run as root or use 'sudo' for system-level optimizations"
echo "  2. Set CPU governor to 'performance'"
echo "  3. Disable CPU frequency scaling"
echo "  4. Enable huge pages: echo 1024 > /proc/sys/vm/nr_hugepages"
echo "  5. Pin benchmark to isolated CPU core: taskset -c 0 ./parser_benchmark"
echo "  6. Increase locked memory limit: ulimit -l unlimited"
echo ""
echo "Example command to run with optimizations:"
echo "  sudo taskset -c 0 nice -n -20 ./parser_benchmark 10000000"
echo ""
