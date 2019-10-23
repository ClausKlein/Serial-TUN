#include "tun-driver.h"

#include <array>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <thread>
using namespace std::literals;

#define wait100ms() std::this_thread::sleep_for(100ms) // NOLINT

struct CommDevices
{
    int tapFileDescriptor;
    int serialFd;
    enum tun_mode_t mode;
};

char adapterName[IF_NAMESIZE] = {};
char serialDevice[_POSIX_PATH_MAX] = {};

volatile bool __io_canceled = false;

static void *serialToTap(void *ptr);
static void *tapToSerial(void *ptr);

static void signal_handler(int sig)
{
    SPDLOG_INFO("signal_handler() called with sig={}", sig);
    io_cancel();
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

    // Create TAP buffer
    std::array<char, ETHER_FRAME_LENGTH> inBuffer;

    while (io_is_enabled()) {
        // Read bytes from serial
        // Incoming byte count
        ssize_t serialResult =
            frame_read(serialFd, inBuffer.data(), inBuffer.size());
        if (serialResult <= 0) {
            SPDLOG_ERROR("Serial read error({}) {}", errno, strerror(errno));
            wait100ms();

#ifndef NDEBUG
            if (args->mode == VTUN_PIPE) {
                char msg[] = "Hallo again";
                (void)frame_write(tapFd, msg, sizeof(msg));
            }
#endif

            continue;
        }

        // Write the packet to the virtual interface
        ssize_t count = write(tapFd, inBuffer.data(), serialResult);
        if (count != serialResult) {
            SPDLOG_ERROR("TAP write error({}) {}", errno, strerror(errno));
            wait100ms();
            continue;
        }

        SPDLOG_TRACE(
            "serialToTap {}:{:n}", count,
            spdlog::to_hex(std::begin(inBuffer), std::begin(inBuffer) + count));
    }

    SPDLOG_INFO("serialToTap thread stopped");
    return NULL;
}

static void *tapToSerial(void *ptr)
{
    // Grab thread parameters
    struct CommDevices *args = (struct CommDevices *)ptr;
    int tapFd = args->tapFileDescriptor;
    int serialFd = args->serialFd;

    // Create TAP buffer
    std::array<char, ETHER_FRAME_LENGTH> inBuffer;

    while (io_is_enabled()) {
        // Incoming byte count
        ssize_t count = read(tapFd, inBuffer.data(), inBuffer.size());
        if (count <= 0) {
            SPDLOG_ERROR("TAP read error({}) {}", errno, strerror(errno));
            wait100ms();
            continue;
        }

        // Write to serial port
        ssize_t serialResult;

#ifndef NDEBUG
        if (args->mode == VTUN_PIPE) {
            serialResult = pipe_write(serialFd, inBuffer.data(), count);
        } else
#endif

        {
            serialResult = frame_write(serialFd, inBuffer.data(), count);
        }
        if (serialResult < 0) {
            SPDLOG_ERROR("Serial write error({}) {}", errno, strerror(errno));
            wait100ms();
            continue;
        }

        SPDLOG_TRACE(
            "tapToSerial {}:{:n}", count,
            spdlog::to_hex(std::begin(inBuffer), std::begin(inBuffer) + count));
    }

    SPDLOG_INFO("tapToSerial thread stopped");
    return NULL;
}

int main(int argc, char *argv[])
{
    enum tun_mode_t mode = VTUN_ETHER;

    // Grab parameters
    int param;
    while ((param = getopt(argc, argv, "i:d:pv")) > 0) {
        switch (param) {
        case 'i':
            strncpy(adapterName, optarg, IF_NAMESIZE - 1);
            break;
        case 'd':
            strncpy(serialDevice, optarg, sizeof(serialDevice) - 1);
            break;
        case 'p':
            mode = VTUN_PIPE;
            // fallthrough
        case 'v':
            spdlog::set_level(spdlog::level::trace); // Set global log level
            break;
        default:
            fprintf(stderr, "Usage: %s -i tun0 -d /dev/spidip2.0 [-p] [-v]\n",
                    *argv);
            return EXIT_FAILURE;
        }
    }

#if defined(__linux__) && defined(NOT_YET)
    if (adapterName[0] == '\0') {
        fprintf(stderr, "Tuntap interface name required (-i)\n");
        return EXIT_FAILURE;
    }
#endif

    if (serialDevice[0] == '\0') {
        fprintf(stderr, "Serial port required (-d)\n");
        return EXIT_FAILURE;
    }

    int fd[2] = {-1, -1};
    if (mode == VTUN_PIPE) {
        if (pipe_open(fd) < 0) {
            fprintf(stderr, "Can't create pipe. %s(%d)", strerror(errno),
                    errno);
            return EXIT_FAILURE;
        }
    }

    int tapFd = tun_open_common(adapterName, mode);
    if (tapFd < 0) {
        perror(adapterName);
        fprintf(stderr, "Could not open tuntap interface!\n");
        if (mode != VTUN_PIPE) {
            return EXIT_FAILURE;
        }

        fprintf(stderr, "Test mode, use VTUN_PIPE first end!\n");
        tapFd = fd[0];
        char pingMsg[] = "\x05\0Ping";
        ssize_t len = write_n(tapFd, pingMsg, sizeof(pingMsg));
        if (len <= 0) {
            perror("write_n Ping");
        }
    }

    int serialFd = open(serialDevice, O_RDWR | O_CLOEXEC);
    if (serialFd < 0) {
        perror(serialDevice);
        fprintf(stderr, "Could not open serial port!\n");
        if (mode != VTUN_PIPE) {
            close(tapFd);
            return EXIT_FAILURE;
        }

        fprintf(stderr, "Test mode, use VTUN_PIPE second end!\n");
        serialFd = fd[1];
        char pingMsg[] = "Pong";
        ssize_t len = frame_write(serialFd, pingMsg, sizeof(pingMsg));
        if (len <= 0) {
            perror("frame_write Pong");
        }
    }

    // register signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGHUP, &sa, NULL);  // terminal line hangup (1)
    sigaction(SIGQUIT, &sa, NULL); // quit program (3)
    sigaction(SIGPIPE, &sa,
              NULL); // Broken pipe: write to pipe with no readers (13)
    // NO!   sigaction(SIGALRM, &sa, NULL); // XXX timer expired (14)
    // FIXME sigaction(SIGTERM, &sa, NULL); // software termination signal (15)

    // Create threads
    pthread_t serial2tap;
    pthread_t tap2serial;

    struct CommDevices threadParams;
    threadParams.tapFileDescriptor = tapFd;
    threadParams.serialFd = serialFd;
    threadParams.mode = mode;

    SPDLOG_INFO("Starting threads");
    do {
        int err = pthread_create(&tap2serial, NULL, tapToSerial,
                                 (void *)&threadParams);
        if (err != 0) {
            fprintf(stderr, "Can't create tapToSerial thread. %s(%d)",
                    strerror(err), err);
            break;
        }
        err = pthread_create(&serial2tap, NULL, serialToTap,
                             (void *)&threadParams);
        if (err != 0) {
            fprintf(stderr, "Can't create serialToTap thread. %s(%d)",
                    strerror(err), err);
            break;
        }

        if (mode == VTUN_PIPE) {
            alarm(4); // NOTE: XXX watchdog timer: send unhandled SIGALRM after
                      // 4 sec! CK
            char msg[] = "Hallo\n";
            pipe_write(1, msg, sizeof(msg)); // stdout
            if (readn_t(0, msg, sizeof(msg), 3) < 0) {
                fprintf(stderr, "Timeout while read from stdin\n");
            } else {
                sleep(1);
            }

            shutdown(serialFd, SHUT_WR);
            shutdown(tapFd, SHUT_WR);
        }

        err = pthread_join(tap2serial, NULL);
        SPDLOG_INFO("Thread tapToSerial joined {}", err);
        close(tapFd);

        err = pthread_join(serial2tap, NULL);
        SPDLOG_INFO("Thread serialToTap joined {}", err);
        close(serialFd);
        return EXIT_SUCCESS;
    } while (false);

    close(tapFd);
    close(serialFd);
    return EXIT_FAILURE;
}
