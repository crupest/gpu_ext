// SPDX-License-Identifier: GPL-2.0
/*
 * test_preempt_ioctl.c - Standalone test for GPU TSG preempt/control via ioctl
 *
 * Usage:
 *   sudo ./test_preempt_ioctl preempt <hClient> <hTsg> [timeout_ms]
 *   sudo ./test_preempt_ioctl timeslice <hClient> <hTsg> <us>
 *   sudo ./test_preempt_ioctl interleave <hClient> <hTsg> <level>
 *
 * Example:
 *   sudo ./test_preempt_ioctl preempt 0xc1e00013 0xcaf00002 100
 *   sudo ./test_preempt_ioctl timeslice 0xc1e00013 0xcaf00002 1
 *
 * Requires custom nvidia module with escape.c security bypass loaded.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <stdint.h>

/* NVIDIA ioctl constants */
#define NV_IOCTL_MAGIC      'F'
#define NV_IOCTL_BASE       200
#define NV_ESC_RM_CONTROL   0x2A
#define NV_ESC_IOCTL_XFER_CMD   (NV_IOCTL_BASE + 11)

/* RM control commands */
#define NVA06C_CTRL_CMD_PREEMPT              0xa06c0105
#define NVA06C_CTRL_CMD_SET_TIMESLICE        0xa06c0103
#define NVA06C_CTRL_CMD_SET_INTERLEAVE_LEVEL 0xa06c0104

typedef struct {
    uint32_t cmd;
    uint32_t size;
    void    *ptr __attribute__((aligned(8)));
} nv_ioctl_xfer_t;

typedef struct {
    uint32_t hClient;
    uint32_t hObject;
    uint32_t cmd;
    uint32_t flags;
    void    *params __attribute__((aligned(8)));
    uint32_t paramsSize;
    uint32_t status;
} NVOS54_PARAMETERS;

typedef struct {
    uint8_t  bWait;
    uint8_t  bManualTimeout;
    uint32_t timeoutUs;
} NVA06C_CTRL_PREEMPT_PARAMS;

typedef struct {
    uint64_t timesliceUs;
} NVA06C_CTRL_TIMESLICE_PARAMS;

typedef struct {
    uint32_t tsgInterleaveLevel;
} NVA06C_CTRL_INTERLEAVE_LEVEL_PARAMS;

static int rm_control(int fd, uint32_t hClient, uint32_t hObject,
                      uint32_t cmd, void *params, uint32_t paramsSize)
{
    NVOS54_PARAMETERS ctrl;
    nv_ioctl_xfer_t xfer;
    int ret;

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.hClient = hClient;
    ctrl.hObject = hObject;
    ctrl.cmd = cmd;
    ctrl.flags = 0;
    ctrl.params = params;
    ctrl.paramsSize = paramsSize;
    ctrl.status = 0;

    memset(&xfer, 0, sizeof(xfer));
    xfer.cmd = NV_ESC_RM_CONTROL;
    xfer.size = sizeof(ctrl);
    xfer.ptr = &ctrl;

    ret = ioctl(fd, _IOWR(NV_IOCTL_MAGIC, NV_ESC_IOCTL_XFER_CMD, nv_ioctl_xfer_t), &xfer);

    if (ret < 0) {
        fprintf(stderr, "ioctl failed: %s (errno=%d)\n", strerror(errno), errno);
        return -errno;
    }
    return ctrl.status;
}

int main(int argc, char **argv)
{
    uint32_t hClient, hTsg;
    int fd, ret;
    struct timespec ts_start, ts_end;

    if (argc < 4) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s preempt <hClient> <hTsg> [timeout_ms]\n", argv[0]);
        fprintf(stderr, "  %s timeslice <hClient> <hTsg> <us>\n", argv[0]);
        fprintf(stderr, "  %s interleave <hClient> <hTsg> <level>\n", argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    hClient = strtoul(argv[2], NULL, 16);
    hTsg = strtoul(argv[3], NULL, 16);

    fd = open("/dev/nvidiactl", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/nvidiactl");
        return 1;
    }

    if (strcmp(cmd, "preempt") == 0) {
        uint32_t timeout_ms = (argc > 4) ? strtoul(argv[4], NULL, 10) : 100;
        NVA06C_CTRL_PREEMPT_PARAMS params = {0};
        params.bWait = 1;
        params.bManualTimeout = 1;
        params.timeoutUs = timeout_ms * 1000;

        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        ret = rm_control(fd, hClient, hTsg, NVA06C_CTRL_CMD_PREEMPT, &params, sizeof(params));
        clock_gettime(CLOCK_MONOTONIC, &ts_end);

        uint64_t dur = (ts_end.tv_sec - ts_start.tv_sec) * 1000000ULL +
                       (ts_end.tv_nsec - ts_start.tv_nsec) / 1000;
        printf("PREEMPT hClient=0x%x hTsg=0x%x status=%d duration=%lu us\n",
               hClient, hTsg, ret, (unsigned long)dur);
    } else if (strcmp(cmd, "timeslice") == 0) {
        if (argc < 5) { fprintf(stderr, "Missing timeslice_us\n"); close(fd); return 1; }
        NVA06C_CTRL_TIMESLICE_PARAMS params = {0};
        params.timesliceUs = strtoull(argv[4], NULL, 10);

        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        ret = rm_control(fd, hClient, hTsg, NVA06C_CTRL_CMD_SET_TIMESLICE, &params, sizeof(params));
        clock_gettime(CLOCK_MONOTONIC, &ts_end);

        uint64_t dur = (ts_end.tv_sec - ts_start.tv_sec) * 1000000ULL +
                       (ts_end.tv_nsec - ts_start.tv_nsec) / 1000;
        printf("SET_TIMESLICE hClient=0x%x hTsg=0x%x timeslice=%llu us status=%d duration=%lu us\n",
               hClient, hTsg, (unsigned long long)params.timesliceUs, ret, (unsigned long)dur);
    } else if (strcmp(cmd, "interleave") == 0) {
        if (argc < 5) { fprintf(stderr, "Missing level\n"); close(fd); return 1; }
        NVA06C_CTRL_INTERLEAVE_LEVEL_PARAMS params = {0};
        params.tsgInterleaveLevel = strtoul(argv[4], NULL, 10);

        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        ret = rm_control(fd, hClient, hTsg, NVA06C_CTRL_CMD_SET_INTERLEAVE_LEVEL, &params, sizeof(params));
        clock_gettime(CLOCK_MONOTONIC, &ts_end);

        uint64_t dur = (ts_end.tv_sec - ts_start.tv_sec) * 1000000ULL +
                       (ts_end.tv_nsec - ts_start.tv_nsec) / 1000;
        printf("SET_INTERLEAVE hClient=0x%x hTsg=0x%x level=%u status=%d duration=%lu us\n",
               hClient, hTsg, params.tsgInterleaveLevel, ret, (unsigned long)dur);
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        close(fd);
        return 1;
    }

    close(fd);
    return ret != 0 ? 1 : 0;
}
