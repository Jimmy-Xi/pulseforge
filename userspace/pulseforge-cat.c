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
#include <sys/epoll.h>
#include <unistd.h>

#include "../include/pulseforge_uapi.h"

#define BATCH_FRAMES 256

static uint32_t ieee_crc32(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint32_t crc = ~0U;
    size_t index;
    unsigned int bit;

    for (index = 0; index < length; ++index) {
        crc ^= bytes[index];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
    }
    return crc ^ ~0U;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--device PATH] [--count N] [--csv]\n"
            "Read and verify PulseForge frames through epoll.\n",
            program);
}

int main(int argc, char **argv)
{
    static const struct option options[] = {
        { "device", required_argument, NULL, 'd' },
        { "count", required_argument, NULL, 'n' },
        { "csv", no_argument, NULL, 'c' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };
    const char *device_path = "/dev/pulseforge0";
    uint64_t target_count = 100;
    uint64_t valid_count = 0;
    uint64_t corrupt_count = 0;
    bool csv = false;
    struct pulseforge_sample samples[BATCH_FRAMES];
    struct epoll_event event = { 0 };
    int device_fd;
    int epoll_fd;
    int option;

    _Static_assert(sizeof(struct pulseforge_sample) == 32,
                   "PulseForge frame ABI must remain 32 bytes");

    while ((option = getopt_long(argc, argv, "d:n:ch", options, NULL)) != -1) {
        switch (option) {
        case 'd':
            device_path = optarg;
            break;
        case 'n':
            target_count = strtoull(optarg, NULL, 0);
            break;
        case 'c':
            csv = true;
            break;
        case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    device_fd = open(device_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (device_fd < 0) {
        fprintf(stderr, "open %s: %s\n", device_path, strerror(errno));
        return EXIT_FAILURE;
    }

    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        fprintf(stderr, "epoll_create1: %s\n", strerror(errno));
        close(device_fd);
        return EXIT_FAILURE;
    }
    event.events = EPOLLIN;
    event.data.fd = device_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, device_fd, &event) < 0) {
        fprintf(stderr, "epoll_ctl: %s\n", strerror(errno));
        close(epoll_fd);
        close(device_fd);
        return EXIT_FAILURE;
    }

    if (csv)
        puts("device_time_ns,sequence,signal_milli,temperature_milli,status,crc32");

    while (target_count == 0 || valid_count < target_count) {
        struct epoll_event ready;
        ssize_t bytes_read;
        size_t frame_count;
        size_t index;

        if (epoll_wait(epoll_fd, &ready, 1, -1) < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "epoll_wait: %s\n", strerror(errno));
            break;
        }

        bytes_read = read(device_fd, samples, sizeof(samples));
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            fprintf(stderr, "read: %s\n", strerror(errno));
            break;
        }
        if ((size_t)bytes_read % sizeof(samples[0]) != 0) {
            fprintf(stderr, "protocol error: partial frame (%zd bytes)\n", bytes_read);
            break;
        }

        frame_count = (size_t)bytes_read / sizeof(samples[0]);
        for (index = 0; index < frame_count; ++index) {
            struct pulseforge_sample *sample = &samples[index];
            uint32_t expected = ieee_crc32(sample, sizeof(*sample) - sizeof(sample->crc32));

            if (sample->crc32 != expected) {
                corrupt_count++;
                continue;
            }

            if (csv) {
                printf("%" PRIu64 ",%" PRIu64 ",%" PRId32 ",%" PRId32
                       ",%" PRIu32 ",%08" PRIx32 "\n",
                       sample->device_time_ns, sample->sequence,
                       sample->signal_milli, sample->temperature_milli,
                       sample->status, sample->crc32);
            } else {
                printf("{\"device_time_ns\":%" PRIu64
                       ",\"sequence\":%" PRIu64
                       ",\"signal_milli\":%" PRId32
                       ",\"temperature_milli\":%" PRId32
                       ",\"status\":%" PRIu32
                       ",\"crc32\":%" PRIu32 "}\n",
                       sample->device_time_ns, sample->sequence,
                       sample->signal_milli, sample->temperature_milli,
                       sample->status, sample->crc32);
            }
            valid_count++;
            if (target_count && valid_count >= target_count)
                break;
        }
    }

    fprintf(stderr, "pulseforge-cat: valid=%" PRIu64 ", corrupt=%" PRIu64 "\n",
            valid_count, corrupt_count);
    close(epoll_fd);
    close(device_fd);
    return corrupt_count ? EXIT_FAILURE : EXIT_SUCCESS;
}

