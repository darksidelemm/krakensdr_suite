#include "utils.hpp"
#include "config.hpp"
#include <array>
#include <pthread.h>
#include <sched.h>
#include <cstring>
#include <iostream>

void append_big_endian(std::vector<uint8_t>& vec, uint32_t value) {
    vec.push_back((value >> 24) & 0xFF);
    vec.push_back((value >> 16) & 0xFF);
    vec.push_back((value >> 8) & 0xFF);
    vec.push_back(value & 0xFF);
}

bool set_thread_realtime(const char* name, int rt_priority, int cpu) {
    pthread_t self = pthread_self();
    bool ok = true;

    // Thread name for `top -H`/htop/perf (Linux caps it at 15 chars + NUL).
    if (name) pthread_setname_np(self, name);

    // Optional CPU pinning. Off by default (cpu < 0): real isolation needs
    // isolcpus on the kernel cmdline, which we can't assume on an arbitrary Pi.
    if (cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        if (pthread_setaffinity_np(self, sizeof(set), &set) != 0) ok = false;
    }

    // SCHED_RR (round-robin), NOT SCHED_FIFO. We run up to NUM_DEVICES (5) USB
    // reader threads at the SAME priority on a 4-core Pi. Under SCHED_FIFO equal
    // priorities do not time-slice: if librtlsdr/libusb ever hot-loops on a USB
    // error storm, 4 spinning readers would pin all cores and the 5th could NEVER
    // be scheduled (no preemption among equals) - a hard lockout, made worse
    // because RT throttling is often disabled. SCHED_RR time-slices equal
    // priorities so every reader keeps making progress. These threads normally
    // block on USB fds / queues, so the round-robin quantum is moot in the happy
    // path; it only matters as a safety net against a runaway.
    if (rt_priority > 0) {
        // NB: no RLIMIT_RTTIME. SCHED_RR already prevents the equal-priority
        // lockout, so a runaway reader only burns one core (the box stays
        // responsive) and that channel's desync is caught by the coherence
        // watchdog. An RLIMIT_RTTIME would SIGKILL the whole process on a runaway,
        // losing every channel - worse than the degraded-but-recovering state.
        const int pmin = sched_get_priority_min(SCHED_RR);
        const int pmax = sched_get_priority_max(SCHED_RR);
        struct sched_param param;
        std::memset(&param, 0, sizeof(param));
        param.sched_priority = std::min(std::max(rt_priority, pmin), pmax);
        const int rc = pthread_setschedparam(self, SCHED_RR, &param);
        if (rc != 0) {
            // Almost always EPERM: running without rtprio limits or CAP_SYS_NICE.
            // Not fatal - fall back to the default scheduler. Warn once.
            static bool warned = false;
            if (!warned) {
                std::cerr << "Thread '" << (name ? name : "?")
                          << "': SCHED_RR unavailable (" << std::strerror(rc)
                          << "); running at normal priority. For best USB keep-up under load, "
                             "grant CAP_SYS_NICE or set RLIMIT_RTPRIO "
                             "(/etc/security/limits.conf: '<user> - rtprio 30')." << std::endl;
                warned = true;
            }
            ok = false;
        }
    }
    return ok;
}

const float* iq_lut() {
    static constexpr std::array<float, 256> lut = [] {
        std::array<float, 256> v{};
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = (static_cast<float>(i) - 127.5f) * SCALE;
        return v;
    }();
    return lut.data();
}
