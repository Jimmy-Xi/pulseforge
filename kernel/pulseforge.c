// SPDX-License-Identifier: MIT
/*
 * PulseForge: fault-injectable virtual telemetry device.
 *
 * The driver deliberately requires no physical hardware.  It emits a
 * deterministic signal through /dev/pulseforge0 so driver behaviour, data
 * pipelines and incident handling can be tested on any Linux machine.
 */

#include <linux/crc32.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/math64.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "../include/pulseforge_uapi.h"

#define PF_DEVICE_NAME "pulseforge0"
#define PF_DEFAULT_RING_DEPTH 4096U
#define PF_MAX_READ_FRAMES 1024U
#define PF_FALLBACK_SEED 0x6D2B79F5U

static unsigned int ring_depth = PF_DEFAULT_RING_DEPTH;
module_param(ring_depth, uint, 0444);
MODULE_PARM_DESC(ring_depth, "number of telemetry frames in the ring buffer");

struct pulseforge_device {
    struct miscdevice miscdev;
    struct hrtimer timer;
    wait_queue_head_t readq;
    spinlock_t lock;
    struct pulseforge_sample *ring;
    unsigned int head;
    unsigned int tail;
    unsigned int count;
    struct pulseforge_config config;
    struct pulseforge_stats stats;
    pf_u32 rng;
    pf_s32 last_value;
};

static struct pulseforge_device pfdev;

static pf_u32 pf_next_random(struct pulseforge_device *dev)
{
    pf_u32 state = dev->rng;

    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    dev->rng = state;
    return state;
}

static s64 pf_triangle(pf_u64 sequence, pf_s32 amplitude)
{
    pf_u64 phase = sequence % 200U;

    if (phase < 100U)
        return -(s64)amplitude + (2LL * amplitude * phase) / 100;
    return amplitude - (2LL * amplitude * (phase - 100U)) / 100;
}

static s64 pf_drift(pf_u64 sequence, pf_s32 amplitude, pf_s32 drift_ppm)
{
    u64 rate = drift_ppm < 0 ? -(s64)drift_ppm : drift_ppm;
    u64 magnitude;

    magnitude = mul_u64_u64_div_u64((u64)amplitude * rate,
                                    sequence, 1000000ULL);
    if (magnitude > INT_MAX)
        return drift_ppm < 0 ? INT_MIN : INT_MAX;
    return drift_ppm < 0 ? -(s64)magnitude : (s64)magnitude;
}

static bool pf_config_valid(const struct pulseforge_config *cfg)
{
    if (cfg->period_us < 100U || cfg->period_us > 10000000U)
        return false;
    if (cfg->amplitude_milli < 1 || cfg->amplitude_milli > 1000000)
        return false;
    if (cfg->noise_milli > (pf_u32)cfg->amplitude_milli)
        return false;
    if (cfg->drift_ppm < -1000000 || cfg->drift_ppm > 1000000)
        return false;
    return true;
}

static void pf_reset_locked(struct pulseforge_device *dev)
{
    memset(&dev->stats, 0, sizeof(dev->stats));
    dev->head = 0;
    dev->tail = 0;
    dev->count = 0;
    dev->rng = dev->config.seed ? dev->config.seed : PF_FALLBACK_SEED;
    dev->last_value = 0;
}

static void pf_push_locked(struct pulseforge_device *dev,
                           struct pulseforge_sample *sample)
{
    if (dev->count == ring_depth) {
        dev->tail = (dev->tail + 1U) % ring_depth;
        dev->count--;
        dev->stats.overruns++;
        sample->status |= PF_STATUS_OVERRUN;
    }

    sample->crc32 = crc32_le(~0U, (u8 *)sample,
                             offsetof(struct pulseforge_sample, crc32)) ^ ~0U;
    dev->ring[dev->head] = *sample;
    dev->head = (dev->head + 1U) % ring_depth;
    dev->count++;
    dev->stats.delivered++;
}

static enum hrtimer_restart pf_timer_callback(struct hrtimer *timer)
{
    struct pulseforge_device *dev = container_of(timer, struct pulseforge_device, timer);
    struct pulseforge_sample sample = { 0 };
    struct pulseforge_config cfg;
    unsigned long flags;
    pf_u64 sequence;
    pf_u32 random_word;
    u64 period_ns;
    s64 noise;
    s64 drift;
    s64 value;

    spin_lock_irqsave(&dev->lock, flags);
    cfg = dev->config;
    sequence = dev->stats.generated++;
    random_word = pf_next_random(dev);

    noise = (s64)(random_word % (2U * cfg.noise_milli + 1U)) - cfg.noise_milli;
    drift = pf_drift(sequence, cfg.amplitude_milli, cfg.drift_ppm);
    value = pf_triangle(sequence, cfg.amplitude_milli) + noise + drift;

    if (cfg.drop_every && (sequence + 1U) % cfg.drop_every == 0U) {
        dev->stats.dropped++;
        goto out_unlock;
    }

    if (cfg.spike_every && (sequence + 1U) % cfg.spike_every == 0U) {
        value += 3LL * cfg.amplitude_milli;
        sample.status |= PF_STATUS_SPIKE;
        dev->stats.spikes++;
    }

    if (cfg.freeze_every && (sequence + 1U) % cfg.freeze_every == 0U) {
        value = dev->last_value;
        sample.status |= PF_STATUS_FREEZE;
        dev->stats.freezes++;
    }

    value = clamp_t(s64, value, INT_MIN, INT_MAX);
    dev->last_value = (pf_s32)value;
    sample.device_time_ns = sequence * (u64)cfg.period_us * 1000ULL;
    sample.sequence = sequence;
    sample.signal_milli = (pf_s32)value;
    sample.temperature_milli = 25000 + (pf_s32)value / 20;
    pf_push_locked(dev, &sample);

out_unlock:
    period_ns = (u64)cfg.period_us * 1000ULL;
    spin_unlock_irqrestore(&dev->lock, flags);
    wake_up_interruptible(&dev->readq);
    hrtimer_forward_now(timer, ns_to_ktime(period_ns));
    return HRTIMER_RESTART;
}

static ssize_t pf_read(struct file *file, char __user *buffer, size_t count,
                       loff_t *offset)
{
    struct pulseforge_device *dev = file->private_data;
    struct pulseforge_sample *samples;
    unsigned long flags;
    unsigned int requested;
    unsigned int available;
    unsigned int copied = 0;
    int ret;

    if (count < sizeof(*samples))
        return -EINVAL;

    requested = min_t(size_t, count / sizeof(*samples), PF_MAX_READ_FRAMES);
    samples = kmalloc_array(requested, sizeof(*samples), GFP_KERNEL);
    if (!samples)
        return -ENOMEM;

    for (;;) {
        spin_lock_irqsave(&dev->lock, flags);
        available = min(requested, dev->count);
        while (copied < available) {
            samples[copied++] = dev->ring[dev->tail];
            dev->tail = (dev->tail + 1U) % ring_depth;
            dev->count--;
        }
        spin_unlock_irqrestore(&dev->lock, flags);

        if (copied)
            break;
        if (file->f_flags & O_NONBLOCK) {
            ret = -EAGAIN;
            goto out_free;
        }
        ret = wait_event_interruptible(dev->readq, READ_ONCE(dev->count) > 0);
        if (ret)
            goto out_free;
    }

    if (copy_to_user(buffer, samples, copied * sizeof(*samples))) {
        ret = -EFAULT;
        goto out_free;
    }
    ret = copied * sizeof(*samples);

out_free:
    kfree(samples);
    return ret;
}

static __poll_t pf_poll(struct file *file, poll_table *wait)
{
    struct pulseforge_device *dev = file->private_data;
    __poll_t mask = 0;

    poll_wait(file, &dev->readq, wait);
    if (READ_ONCE(dev->count) > 0)
        mask |= EPOLLIN | EPOLLRDNORM;
    return mask;
}

static long pf_ioctl(struct file *file, unsigned int command, unsigned long argument)
{
    struct pulseforge_device *dev = file->private_data;
    void __user *argp = (void __user *)argument;
    struct pulseforge_config config;
    struct pulseforge_stats stats;
    unsigned long flags;

    switch (command) {
    case PF_IOC_SET_CONFIG:
        if (copy_from_user(&config, argp, sizeof(config)))
            return -EFAULT;
        if (!pf_config_valid(&config))
            return -EINVAL;
        hrtimer_cancel(&dev->timer);
        spin_lock_irqsave(&dev->lock, flags);
        dev->config = config;
        pf_reset_locked(dev);
        spin_unlock_irqrestore(&dev->lock, flags);
        hrtimer_start(&dev->timer, ns_to_ktime((u64)config.period_us * 1000ULL),
                      HRTIMER_MODE_REL);
        wake_up_interruptible(&dev->readq);
        return 0;

    case PF_IOC_GET_CONFIG:
        spin_lock_irqsave(&dev->lock, flags);
        config = dev->config;
        spin_unlock_irqrestore(&dev->lock, flags);
        return copy_to_user(argp, &config, sizeof(config)) ? -EFAULT : 0;

    case PF_IOC_GET_STATS:
        spin_lock_irqsave(&dev->lock, flags);
        stats = dev->stats;
        spin_unlock_irqrestore(&dev->lock, flags);
        return copy_to_user(argp, &stats, sizeof(stats)) ? -EFAULT : 0;

    case PF_IOC_RESET:
        spin_lock_irqsave(&dev->lock, flags);
        pf_reset_locked(dev);
        spin_unlock_irqrestore(&dev->lock, flags);
        wake_up_interruptible(&dev->readq);
        return 0;

    default:
        return -ENOTTY;
    }
}

static int pf_open(struct inode *inode, struct file *file)
{
    file->private_data = &pfdev;
    return nonseekable_open(inode, file);
}

static const struct file_operations pf_fops = {
    .owner = THIS_MODULE,
    .open = pf_open,
    .read = pf_read,
    .poll = pf_poll,
    .unlocked_ioctl = pf_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = pf_ioctl,
#endif
    .llseek = no_llseek,
};

static int __init pf_init(void)
{
    int ret;

    if (ring_depth < 2U || ring_depth > 1048576U)
        return -EINVAL;

    memset(&pfdev, 0, sizeof(pfdev));
    spin_lock_init(&pfdev.lock);
    init_waitqueue_head(&pfdev.readq);
    pfdev.ring = kcalloc(ring_depth, sizeof(*pfdev.ring), GFP_KERNEL);
    if (!pfdev.ring)
        return -ENOMEM;

    pfdev.config = (struct pulseforge_config) {
        .period_us = 10000,
        .amplitude_milli = 1000,
        .noise_milli = 25,
        .seed = 0x00C0FFEE,
    };
    pf_reset_locked(&pfdev);

    pfdev.miscdev = (struct miscdevice) {
        .minor = MISC_DYNAMIC_MINOR,
        .name = PF_DEVICE_NAME,
        .fops = &pf_fops,
        .mode = 0660,
    };
    ret = misc_register(&pfdev.miscdev);
    if (ret)
        goto out_free;

    hrtimer_init(&pfdev.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    pfdev.timer.function = pf_timer_callback;
    hrtimer_start(&pfdev.timer,
                  ns_to_ktime((u64)pfdev.config.period_us * 1000ULL),
                  HRTIMER_MODE_REL);
    pr_info("pulseforge: /dev/%s ready, ring depth=%u\n", PF_DEVICE_NAME, ring_depth);
    return 0;

out_free:
    kfree(pfdev.ring);
    return ret;
}

static void __exit pf_exit(void)
{
    hrtimer_cancel(&pfdev.timer);
    misc_deregister(&pfdev.miscdev);
    kfree(pfdev.ring);
    pr_info("pulseforge: unloaded\n");
}

module_init(pf_init);
module_exit(pf_exit);

MODULE_AUTHOR("Jimmy-Xi");
MODULE_DESCRIPTION("Deterministic fault-injectable virtual telemetry device");
MODULE_LICENSE("MIT");
MODULE_VERSION("0.1.0");
