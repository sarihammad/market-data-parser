#pragma once

#include <thread>
#include <sched.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fstream>
#include <stdexcept>
#include <cstdint>

namespace fast_market {

/**
 * System-level utilities for performance optimization
 */
class SystemUtils {
public:
    /**
     * Pin current thread to a specific CPU core
     */
    static bool pin_thread_to_core(int core_id) noexcept {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        
        pthread_t current_thread = pthread_self();
        return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) == 0;
    }
    
    /**
     * Set thread scheduling priority (requires root)
     */
    static bool set_realtime_priority(int priority = 99) noexcept {
        struct sched_param param;
        param.sched_priority = priority;
        
        pthread_t current_thread = pthread_self();
        return pthread_setschedparam(current_thread, SCHED_FIFO, &param) == 0;
    }
    
    /**
     * Lock memory to prevent paging
     */
    static bool lock_memory() noexcept {
        // Lock all current and future pages
        return mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
    }
    
    /**
     * Unlock memory
     */
    static void unlock_memory() noexcept {
        munlockall();
    }
    
    /**
     * Get number of available CPU cores
     */
    static int get_cpu_count() noexcept {
        return std::thread::hardware_concurrency();
    }
    
    /**
     * Prefetch data into cache
     */
    template<typename T>
    static void prefetch(const T* ptr) noexcept {
        __builtin_prefetch(ptr, 0, 3);  // Read, high temporal locality
    }
    
    /**
     * Compiler barrier to prevent reordering
     */
    static void compiler_barrier() noexcept {
        asm volatile("" ::: "memory");
    }
    
    /**
     * CPU pause instruction for spin loops
     */
    static void cpu_pause() noexcept {
        __builtin_ia32_pause();
    }
    
    /**
     * Get TSC frequency (approximate)
     */
    static uint64_t get_tsc_frequency() {
        // Read TSC twice with a 1-second sleep
        uint64_t start = rdtsc();
        sleep(1);
        uint64_t end = rdtsc();
        return end - start;
    }
    
    /**
     * Read Time Stamp Counter
     */
    [[gnu::always_inline]] static inline uint64_t rdtsc() noexcept {
        uint32_t lo, hi;
        __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }
    
    /**
     * Read TSC with ordering (serializing)
     */
    [[gnu::always_inline]] static inline uint64_t rdtscp() noexcept {
        uint32_t lo, hi;
        __asm__ __volatile__ ("rdtscp" : "=a" (lo), "=d" (hi) :: "%rcx");
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }
    
    /**
     * Check if huge pages are available
     */
    static bool has_huge_pages() noexcept {
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        
        while (std::getline(meminfo, line)) {
            if (line.find("HugePages_Total:") != std::string::npos) {
                size_t total = std::stoul(line.substr(line.find_last_of(' ') + 1));
                return total > 0;
            }
        }
        
        return false;
    }
    
    /**
     * Allocate memory with huge pages
     */
    static void* allocate_huge_pages(size_t size) {
        void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        
        if (ptr == MAP_FAILED) {
            return nullptr;
        }
        
        return ptr;
    }
    
    /**
     * Free huge page memory
     */
    static void free_huge_pages(void* ptr, size_t size) noexcept {
        if (ptr != nullptr) {
            munmap(ptr, size);
        }
    }
    
    /**
     * Warm up CPU (run busy loop to prevent frequency scaling)
     */
    static void warmup_cpu(int milliseconds = 100) noexcept {
        auto start = rdtsc();
        auto freq = get_tsc_frequency();
        auto target = start + (freq * milliseconds / 1000);
        
        while (rdtsc() < target) {
            cpu_pause();
        }
    }
    
    /**
     * Disable CPU frequency scaling (requires root and cpufreq)
     */
    static bool set_cpu_governor(const std::string& governor) noexcept {
        int num_cpus = get_cpu_count();
        bool success = true;
        
        for (int i = 0; i < num_cpus; ++i) {
            std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(i) 
                             + "/cpufreq/scaling_governor";
            std::ofstream gov_file(path);
            
            if (gov_file.is_open()) {
                gov_file << governor;
                gov_file.close();
            } else {
                success = false;
            }
        }
        
        return success;
    }
};

/**
 * RAII wrapper for CPU pinning
 */
class ScopedCPUPin {
public:
    explicit ScopedCPUPin(int core_id) : pinned_(false) {
        pinned_ = SystemUtils::pin_thread_to_core(core_id);
    }
    
    ~ScopedCPUPin() {
        if (pinned_) {
            // Reset affinity to all CPUs
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            for (int i = 0; i < SystemUtils::get_cpu_count(); ++i) {
                CPU_SET(i, &cpuset);
            }
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        }
    }
    
    [[nodiscard]] bool is_pinned() const noexcept { return pinned_; }
    
private:
    bool pinned_;
};

/**
 * RAII wrapper for memory locking
 */
class ScopedMemoryLock {
public:
    ScopedMemoryLock() : locked_(false) {
        locked_ = SystemUtils::lock_memory();
    }
    
    ~ScopedMemoryLock() {
        if (locked_) {
            SystemUtils::unlock_memory();
        }
    }
    
    [[nodiscard]] bool is_locked() const noexcept { return locked_; }
    
private:
    bool locked_;
};

} // namespace fast_market
