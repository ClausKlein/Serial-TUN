#include "tun-driver.h"

#include <array>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <thread>
using namespace std::literals;

class ExtensionPoint
{
public:
    enum Channel
    {
        IN_BOUND = 0,
        OUT_BOUND = 1
    };

    ExtensionPoint() {}
    virtual ~ExtensionPoint() {}

    virtual ssize_t read(Channel fd, void *buf, size_t count) noexcept = 0;
    virtual ssize_t write(Channel fd, const void *buf,
                          size_t count) noexcept = 0;
};

class Pipe : public ExtensionPoint
{
public:
    Pipe()
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        if (pipe_open(fd) < 0) {
            // TODO: Trace, log ...
        }
    }

    ~Pipe() override
    {
        close(fd[IN_BOUND]);
        close(fd[OUT_BOUND]);
    }

    ssize_t read(Channel id, void *buf, size_t count) noexcept override
    {
        return ::read(fd[id], buf, count);
    }
    ssize_t write(Channel id, const void *buf, size_t count) noexcept override
    {
        return ::write(fd[id], buf, count);
    }

private:
    int fd[2]{-1, -1};
};

class CommDevices
{
public:
    typedef std::shared_ptr<ExtensionPoint> extensionPtr_t;

    CommDevices(int tapFd, int serialFd, enum tun_mode_t _mode,
                extensionPtr_t optional)
        : tapFileDescriptor(tapFd), serialFileDescriptor(serialFd), mode(_mode),
          extensionPoint(optional)
    {}

    void serialToTap();
    void tapToSerial();

    static void wait100ms() { std::this_thread::sleep_for(100ms); }

private:
    const int tapFileDescriptor;
    const int serialFileDescriptor;
    const enum tun_mode_t mode;
    extensionPtr_t extensionPoint;
};

char adapterName[IF_NAMESIZE] = {};
char serialDevice[_POSIX_PATH_MAX] = {};

volatile bool __io_canceled = false;

static void signal_handler(int sig)
{
    SPDLOG_INFO("signal_handler() called with sig={}", sig);
    io_cancel();
}

/**
 * Handles getting packets from the serial port and writing them to the TAP
 * interface
 */
void CommDevices::serialToTap()
{
    // Grab thread parameters
    const int tapFd = this->tapFileDescriptor;
    const int serialFd = this->serialFileDescriptor;

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
            if (this->mode == VTUN_PIPE) {
                char msg[] = "Hallo again";
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
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
}

/**
 * Handles getting packets from the TAP interface and writing them to the serial
 * port
 */
void CommDevices::tapToSerial()
{
    // Grab thread parameters
    const int tapFd = this->tapFileDescriptor;
    const int serialFd = this->serialFileDescriptor;

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
        if (this->mode == VTUN_PIPE) {
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
}

int main(int argc, char *argv[])
{
    enum tun_mode_t mode = VTUN_ETHER;
    bool red_node = false;

    // Grab parameters
    int param;
    while ((param = getopt(argc, argv, "i:d:prv")) > 0) {
        switch (param) {
        case 'i':
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            strncpy(adapterName, optarg, IF_NAMESIZE - 1);
            break;
        case 'd':
            strncpy(serialDevice, optarg, sizeof(serialDevice) - 1);
            break;
        case 'r':
            red_node = true;
            break;
        case 'p':
            mode = VTUN_PIPE;
            // fallthrough
        case 'v':
            spdlog::set_level(spdlog::level::trace); // Set global log level
            break;
        default:
            fprintf(stderr,
                    "Usage: %s -i tun0 -d /dev/spidip2.0 [-r] [-p] [-v]\n",
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
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        if (pipe_open(fd) < 0) {
            fprintf(stderr, "Can't create pipe. %s(%d)", strerror(errno),
                    errno);
            return EXIT_FAILURE;
        }
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
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
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        (void)write_n(tapFd, pingMsg, sizeof(pingMsg));
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
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        (void)frame_write(serialFd, pingMsg, sizeof(pingMsg));
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

    SPDLOG_INFO("Starting threads");
    try {
        CommDevices::extensionPtr_t
            extension; // default = std::shared_ptr<Pipe>();
        if (red_node) {
            extension = std::make_shared<Pipe>();
        }
        CommDevices threadParams(tapFd, serialFd, mode, extension);

        // Create threads
        std::thread tap2serial(
            std::bind(&CommDevices::tapToSerial, threadParams));
        std::thread serial2tap(
            std::bind(&CommDevices::serialToTap, threadParams));

        if (mode == VTUN_PIPE) {
            alarm(4); // NOTE: XXX watchdog timer: send unhandled SIGALRM after
                      // 4 sec! CK
            char msg[] = "Hallo\n";
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            pipe_write(1, msg, sizeof(msg)); // stdout
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            if (readn_t(0, msg, sizeof(msg), 3) < 0) {
                SPDLOG_INFO("Timeout while read from stdin");
            } else {
                sleep(1);
            }

            shutdown(serialFd, SHUT_WR);
            shutdown(tapFd, SHUT_WR);
        }

        tap2serial.join();
        SPDLOG_INFO("Thread tapToSerial joined ");
        close(tapFd);

        serial2tap.join();
        SPDLOG_INFO("Thread serialToTap joined ");
        close(serialFd);

        return EXIT_SUCCESS;
    } catch (std::exception &e) {
        SPDLOG_ERROR("Exception {}", e.what());
    }

    close(tapFd);
    close(serialFd);
    return EXIT_FAILURE;
}
