#include "tun-driver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef __linux__
#    include <linux/if_tun.h>
#endif

const uint16_t FRAME_LEN_MASK(0x7fff);
const uint16_t FRAME_LENGTH(0x8000);

struct CommDevices
{
    int tapFileDescriptor;
    int serialFd;
};

char adapterName[IFNAMSIZ] = {};
char serialDevice[_POSIX_PATH_MAX] = {};

/* IO cancelation */
extern volatile bool __io_canceled;

static inline void io_init() { __io_canceled = false; }

static inline void io_cancel() { __io_canceled = true; }

volatile bool __io_canceled = false;

static void *serialToTap(void *ptr);
static void *tapToSerial(void *ptr);

static void signal_handler(int /*sig*/) { io_cancel(); }

/* Read exactly len bytes (Signal safe)*/
static inline int read_n(int fd, char *buf, size_t len)
{
    int res = 0;
    int rlen;

    while (!__io_canceled && len > 0) {
        if ((rlen = read(fd, buf, len)) < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            return -1;
        }
        if (!rlen)
            return 0;

        len -= rlen;
        buf += rlen;
        res += rlen;
    }

    return res;
}

/* Write exactly len bytes (Signal safe)*/
static inline int write_n(int fd, char *buf, size_t len)
{
    int res = 0;
    int wlen;

    while (!__io_canceled && len > 0) {
        if ((wlen = write(fd, buf, len)) < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            return -1;
        }
        if (!wlen)
            return 0;

        len -= wlen;
        buf += wlen;
        res += wlen;
    }

    return res;
}

/* Functions to read/write frames. */
int frame_write(int fd, char *buf, size_t len)
{
    char *ptr;
    int wlen;

    // TODO: prevent this hack with writev()! CK
    ptr = buf - sizeof(uint16_t);

    *((uint16_t *)ptr) = htons(len);
    len = (len & FRAME_LEN_MASK) + sizeof(uint16_t);

    while (true) {
        if ((wlen = write(fd, ptr, len)) < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            if (errno == ENOBUFS) {
                return 0;
            }
        }

        /* Even if we wrote only part of the frame we can't use second write
         * since it will produce another frame */
        return wlen;
    }
}

int frame_read(int fd, char *buf, size_t len)
{
    uint16_t hdr;
    uint16_t flen;
    struct iovec iv[2];
    int rlen;

    /* Read frame */
    iv[0].iov_len = sizeof(uint16_t);
    iv[0].iov_base = (char *)&hdr;
    iv[1].iov_len = len;
    iv[1].iov_base = buf;

    while (true) {
        if ((rlen = readv(fd, iv, 2)) < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            return rlen;
        }
        hdr = ntohs(hdr);
        flen = hdr & FRAME_LEN_MASK;

        if (rlen < 2 || (rlen - 2) != flen) {
            return -1;
        }

        return hdr;
    }
}

/* Read N bytes with timeout */
int readn_t(int fd, char *buf, size_t count, time_t timeout)
{
    fd_set fdset;
    struct timeval tv;

    tv.tv_usec = 0;
    tv.tv_sec = timeout;

    FD_ZERO(&fdset);
    FD_SET(fd, &fdset);
    if (select(fd + 1, &fdset, NULL, NULL, &tv) <= 0)
        return -1;

    return read_n(fd, buf, count);
}

/**
 * Handles getting packets from the serial port and writing them to the TAP
 * interface
 * @param ptr       - Pointer to the CommDevices struct
 */
static void *serialToTap(void *ptr)
{
    // Grab thread parameters
    struct CommDevices *args = (struct CommDevices *)ptr;

    int tapFd = args->tapFileDescriptor;
    int serialFd = args->serialFd;

    char inBuffer[FRAME_LENGTH];
    size_t outSize = 0;
    int inIndex = 0;

    // Incoming byte count
    size_t count;
    ssize_t serialResult;

    while (true) {
        // Read bytes from serial
        serialResult = frame_read(serialFd, &inBuffer[0], sizeof(inBuffer));

        if (serialResult < 0) {
            perror("Serial read error!");
        } else {
            // Write the packet to the virtual interface
            count = write(tapFd, inBuffer, serialResult);
            if (count != serialResult) {
                perror("TAP write error!");
            }
        }
    }

    return ptr;
}

static void *tapToSerial(void *ptr)
{
    // Grab thread parameters
    struct CommDevices *args = (struct CommDevices *)ptr;

    int tapFd = args->tapFileDescriptor;
    int serialFd = args->serialFd;

    // Create TAP buffer
    char inBuffer[FRAME_LENGTH];

    // Incoming byte count
    ssize_t count;
    int serialResult;

    while (true) {
        // NOTE: we need space for fram length! CK
        count = read(tapFd, inBuffer + sizeof(uint16_t),
                     sizeof(inBuffer) - sizeof(uint16_t));
        if (count < 0) {
            perror("Could not read from TAP!");
            continue;
        }

        // Write to serial port
        serialResult = frame_write(serialFd, inBuffer, count);
        if (serialResult < 0) {
            perror("Could not write to serial!");
        }
    }

    return ptr;
}

int main(int argc, char *argv[])
{
    // Grab parameters
    int param;
    while ((param = getopt(argc, argv, "i:d:")) > 0) {
        switch (param) {
        case 'i':
            strncpy(adapterName, optarg, IFNAMSIZ - 1);
            break;
        case 'd':
            strncpy(serialDevice, optarg, sizeof(serialDevice) - 1);
            break;
        default:
            fprintf(stderr, "Usage: %s -i tun0 -d /dev/spidip2.0\n", *argv);
            return EXIT_FAILURE;
        }
    }

#ifdef __linux__
    if (adapterName[0] == '\0') {
        fprintf(stderr, "Tuntap interface name required (-i)\n");
        return EXIT_FAILURE;
    }
#endif

    if (serialDevice[0] == '\0') {
        fprintf(stderr, "Serial port required (-d)\n");
        return EXIT_FAILURE;
    }

    int tapFd = tun_open_common(adapterName, MODE_TAP);
    if (tapFd < 0) {
        perror(adapterName);
        fprintf(stderr, "Could not open tuntap interface!\n");
        return EXIT_FAILURE;
    }

    int serialFd = open(serialDevice, O_RDWR | O_CLOEXEC);
    if (serialFd < 0) {
        perror(serialDevice);
        fprintf(stderr, "Could not open serial port!\n");
        close(tapFd);
        return EXIT_FAILURE;
    }

    // register signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGHUP, &sa, NULL);

    // Create threads
    pthread_t serial2tap;
    pthread_t tap2serial;
    int ret1;
    int ret2;

    struct CommDevices threadParams;
    threadParams.tapFileDescriptor = tapFd;
    threadParams.serialFd = serialFd;

    printf("Starting threads\n");
    ret1 =
        pthread_create(&tap2serial, NULL, tapToSerial, (void *)&threadParams);
    ret2 =
        pthread_create(&serial2tap, NULL, serialToTap, (void *)&threadParams);

    pthread_join(tap2serial, NULL);
    printf("Thread tap-to-network returned %d\n", ret1);
    pthread_join(serial2tap, NULL);
    printf("Thread network-to-tap returned %d\n", ret2);

    close(tapFd);
    close(serialFd);
    return EXIT_SUCCESS;
}
