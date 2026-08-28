// SPDX-License-Identifier: MIT
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../include/pulseforge_uapi.h"

static void print_config(const struct pulseforge_config *cfg)
{
    printf("{\"period_us\":%" PRIu32
           ",\"amplitude_milli\":%" PRId32
           ",\"noise_milli\":%" PRIu32
           ",\"spike_every\":%" PRIu32
           ",\"drop_every\":%" PRIu32
           ",\"freeze_every\":%" PRIu32
           ",\"drift_ppm\":%" PRId32
           ",\"seed\":%" PRIu32 "}\n",
           cfg->period_us, cfg->amplitude_milli, cfg->noise_milli,
           cfg->spike_every, cfg->drop_every, cfg->freeze_every,
           cfg->drift_ppm, cfg->seed);
}

static void print_stats(const struct pulseforge_stats *stats)
{
    printf("{\"generated\":%" PRIu64
           ",\"delivered\":%" PRIu64
           ",\"dropped\":%" PRIu64
           ",\"overruns\":%" PRIu64
           ",\"spikes\":%" PRIu64
           ",\"freezes\":%" PRIu64 "}\n",
           stats->generated, stats->delivered, stats->dropped,
           stats->overruns, stats->spikes, stats->freezes);
}

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS]\n"
            "  --device PATH       device node (default /dev/pulseforge0)\n"
            "  --period-us N       sample period\n"
            "  --amplitude N       signal amplitude in milli-units\n"
            "  --noise N           noise amplitude in milli-units\n"
            "  --spike-every N     inject a spike every N attempts (0 disables)\n"
            "  --drop-every N      drop every N attempts (0 disables)\n"
            "  --freeze-every N    freeze every N attempts (0 disables)\n"
            "  --drift-ppm N       linear drift rate\n"
            "  --seed N            deterministic uint32 seed\n"
            "  --stats             print device counters\n"
            "  --reset             clear the ring and reset deterministic state\n",
            program);
}

static uint32_t parse_u32(const char *text, const char *name)
{
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 0);

    if (!text[0] || !end || *end || value > UINT32_MAX) {
        fprintf(stderr, "invalid %s: %s\n", name, text);
        exit(EXIT_FAILURE);
    }
    return (uint32_t)value;
}

static int32_t parse_s32(const char *text, const char *name)
{
    char *end = NULL;
    long long value = strtoll(text, &end, 0);

    if (!text[0] || !end || *end || value < INT32_MIN || value > INT32_MAX) {
        fprintf(stderr, "invalid %s: %s\n", name, text);
        exit(EXIT_FAILURE);
    }
    return (int32_t)value;
}

int main(int argc, char **argv)
{
    enum {
        OPT_PERIOD = 1000,
        OPT_AMPLITUDE,
        OPT_NOISE,
        OPT_SPIKE,
        OPT_DROP,
        OPT_FREEZE,
        OPT_DRIFT,
        OPT_SEED,
        OPT_STATS,
        OPT_RESET,
    };
    enum {
        CH_PERIOD = 1U << 0,
        CH_AMPLITUDE = 1U << 1,
        CH_NOISE = 1U << 2,
        CH_SPIKE = 1U << 3,
        CH_DROP = 1U << 4,
        CH_FREEZE = 1U << 5,
        CH_DRIFT = 1U << 6,
        CH_SEED = 1U << 7,
    };
    static const struct option options[] = {
        { "device", required_argument, NULL, 'd' },
        { "period-us", required_argument, NULL, OPT_PERIOD },
        { "amplitude", required_argument, NULL, OPT_AMPLITUDE },
        { "noise", required_argument, NULL, OPT_NOISE },
        { "spike-every", required_argument, NULL, OPT_SPIKE },
        { "drop-every", required_argument, NULL, OPT_DROP },
        { "freeze-every", required_argument, NULL, OPT_FREEZE },
        { "drift-ppm", required_argument, NULL, OPT_DRIFT },
        { "seed", required_argument, NULL, OPT_SEED },
        { "stats", no_argument, NULL, OPT_STATS },
        { "reset", no_argument, NULL, OPT_RESET },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };
    const char *device_path = "/dev/pulseforge0";
    struct pulseforge_config config;
    struct pulseforge_stats stats;
    unsigned int changed = 0;
    bool show_stats = false;
    bool reset = false;
    int fd;
    int option;

    memset(&config, 0, sizeof(config));
    while ((option = getopt_long(argc, argv, "d:h", options, NULL)) != -1) {
        switch (option) {
        case 'd': device_path = optarg; break;
        case OPT_PERIOD: config.period_us = parse_u32(optarg, "period-us"); changed |= CH_PERIOD; break;
        case OPT_AMPLITUDE: config.amplitude_milli = parse_s32(optarg, "amplitude"); changed |= CH_AMPLITUDE; break;
        case OPT_NOISE: config.noise_milli = parse_u32(optarg, "noise"); changed |= CH_NOISE; break;
        case OPT_SPIKE: config.spike_every = parse_u32(optarg, "spike-every"); changed |= CH_SPIKE; break;
        case OPT_DROP: config.drop_every = parse_u32(optarg, "drop-every"); changed |= CH_DROP; break;
        case OPT_FREEZE: config.freeze_every = parse_u32(optarg, "freeze-every"); changed |= CH_FREEZE; break;
        case OPT_DRIFT: config.drift_ppm = parse_s32(optarg, "drift-ppm"); changed |= CH_DRIFT; break;
        case OPT_SEED: config.seed = parse_u32(optarg, "seed"); changed |= CH_SEED; break;
        case OPT_STATS: show_stats = true; break;
        case OPT_RESET: reset = true; break;
        case 'h': usage(argv[0]); return EXIT_SUCCESS;
        default: usage(argv[0]); return EXIT_FAILURE;
        }
    }

    fd = open(device_path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", device_path, strerror(errno));
        return EXIT_FAILURE;
    }

    if (changed) {
        struct pulseforge_config current;
        if (ioctl(fd, PF_IOC_GET_CONFIG, &current) < 0) {
            fprintf(stderr, "get config: %s\n", strerror(errno));
            close(fd);
            return EXIT_FAILURE;
        }
        if (changed & CH_PERIOD) current.period_us = config.period_us;
        if (changed & CH_AMPLITUDE) current.amplitude_milli = config.amplitude_milli;
        if (changed & CH_NOISE) current.noise_milli = config.noise_milli;
        if (changed & CH_SPIKE) current.spike_every = config.spike_every;
        if (changed & CH_DROP) current.drop_every = config.drop_every;
        if (changed & CH_FREEZE) current.freeze_every = config.freeze_every;
        if (changed & CH_DRIFT) current.drift_ppm = config.drift_ppm;
        if (changed & CH_SEED) current.seed = config.seed;
        if (ioctl(fd, PF_IOC_SET_CONFIG, &current) < 0) {
            fprintf(stderr, "set config: %s\n", strerror(errno));
            close(fd);
            return EXIT_FAILURE;
        }
    }

    if (reset && ioctl(fd, PF_IOC_RESET) < 0) {
        fprintf(stderr, "reset: %s\n", strerror(errno));
        close(fd);
        return EXIT_FAILURE;
    }

    if (show_stats) {
        if (ioctl(fd, PF_IOC_GET_STATS, &stats) < 0) {
            fprintf(stderr, "get stats: %s\n", strerror(errno));
            close(fd);
            return EXIT_FAILURE;
        }
        print_stats(&stats);
    } else {
        if (ioctl(fd, PF_IOC_GET_CONFIG, &config) < 0) {
            fprintf(stderr, "get config: %s\n", strerror(errno));
            close(fd);
            return EXIT_FAILURE;
        }
        print_config(&config);
    }

    close(fd);
    return EXIT_SUCCESS;
}
