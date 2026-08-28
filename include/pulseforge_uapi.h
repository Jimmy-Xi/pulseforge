#ifndef PULSEFORGE_UAPI_H
#define PULSEFORGE_UAPI_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
typedef __u64 pf_u64;
typedef __u32 pf_u32;
typedef __s32 pf_s32;
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint64_t pf_u64;
typedef uint32_t pf_u32;
typedef int32_t pf_s32;
#endif

#define PF_STATUS_SPIKE  (1U << 0)
#define PF_STATUS_FREEZE (1U << 1)
#define PF_STATUS_OVERRUN (1U << 2)

/* Stable, little-endian, 32-byte frame shared by all PulseForge backends. */
struct pulseforge_sample {
    pf_u64 device_time_ns;
    pf_u64 sequence;
    pf_s32 signal_milli;
    pf_s32 temperature_milli;
    pf_u32 status;
    pf_u32 crc32;
} __attribute__((packed));

struct pulseforge_config {
    pf_u32 period_us;
    pf_s32 amplitude_milli;
    pf_u32 noise_milli;
    pf_u32 spike_every;
    pf_u32 drop_every;
    pf_u32 freeze_every;
    pf_s32 drift_ppm;
    pf_u32 seed;
};

struct pulseforge_stats {
    pf_u64 generated;
    pf_u64 delivered;
    pf_u64 dropped;
    pf_u64 overruns;
    pf_u64 spikes;
    pf_u64 freezes;
};

#define PF_IOC_MAGIC 0xF5
#define PF_IOC_SET_CONFIG _IOW(PF_IOC_MAGIC, 0x01, struct pulseforge_config)
#define PF_IOC_GET_CONFIG _IOR(PF_IOC_MAGIC, 0x02, struct pulseforge_config)
#define PF_IOC_GET_STATS _IOR(PF_IOC_MAGIC, 0x03, struct pulseforge_stats)
#define PF_IOC_RESET _IO(PF_IOC_MAGIC, 0x04)

#endif /* PULSEFORGE_UAPI_H */

