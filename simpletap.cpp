#include "ExtensionPoint.h"

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

class CommDevices
{
public:
    typedef std::shared_ptr<ExtensionPoint> extensionPtr_t;

    CommDevices(int tapFd, int serialFd, enum tun_mode_t _mode,
                extensionPtr_t optional)
        : tapFileDescriptor(tapFd), serialFileDescriptor(serialFd), mode(_mode),
          extensionPoint(std::move(optional))
    {}

    void serialToTap();
    void tapToSerial();
    void readInBound();
    void readOutBound();

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
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
    std::array<char, ETHER_FRAME_LENGTH> inBuffer{};

    while (io_is_enabled()) {
        // Read bytes from serial
        // Incoming byte count
        ssize_t serialResult =
            frame_read(serialFd, inBuffer.data(), inBuffer.size());
        if (serialResult <= 0) {
            SPDLOG_ERROR("Serial read error({}) {}", errno, strerror(errno));
            wait100ms();

#ifndef NDEBUG
        // selftest only:
            if (this->mode == VTUN_PIPE) {
                char pingMsg[] = "\x05\0TapPing";
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
                serialResult = sizeof(pingMsg);
                memcpy(inBuffer.data(), pingMsg, sizeof(pingMsg));
            }
#else
            continue;
#endif
        }

        // Write the packet to the virtual interface
        ssize_t count;
        if (extensionPoint.get() != nullptr) {
            count = extensionPoint->write(ExtensionPoint::OUTER,
                                          inBuffer.data(), serialResult);
        } else {
            count = write(tapFd, inBuffer.data(), serialResult);
        }
        if (count != serialResult) {
            SPDLOG_ERROR("InBound write error({}) {}", errno, strerror(errno));
            wait100ms();
            continue;
        }

#ifndef NDEBUG
        if (extensionPoint.get() == nullptr) {
            SPDLOG_TRACE(" serialToTap {}:{:n}", count,
                         spdlog::to_hex(std::begin(inBuffer),
                                        std::begin(inBuffer) + count));
            wait100ms();
        }
#endif
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
    std::array<char, ETHER_FRAME_LENGTH> inBuffer{};

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
        if (extensionPoint.get() != nullptr) {
            serialResult = extensionPoint->write(ExtensionPoint::INNER,
                                                 inBuffer.data(), count);
#ifndef NDEBUG
        // selftest only:
        } else if (this->mode == VTUN_PIPE) {
            serialResult = pipe_write(serialFd, inBuffer.data(), count);
#endif

        } else {
            serialResult = frame_write(serialFd, inBuffer.data(), count);
        }
        if (serialResult < 0) {
            SPDLOG_ERROR("OutBound write error({}) {}", errno, strerror(errno));
            wait100ms();
            continue;
        }

#ifndef NDEBUG
        if (extensionPoint.get() == nullptr) {
            SPDLOG_TRACE(" tapToSerial {}:{:n}", count,
                         spdlog::to_hex(std::begin(inBuffer),
                                        std::begin(inBuffer) + count));
            wait100ms();
        }
#endif
    }

    SPDLOG_INFO("tapToSerial thread stopped");
}

void CommDevices::readOutBound()
{
    // Grab thread parameters
    if (extensionPoint.get() == nullptr)
        return;

    // Create outgoing buffer
    std::array<char, ETHER_FRAME_LENGTH> inBuffer{};

    while (io_is_enabled()) {
        // Read outgoing byte count
        ssize_t result = extensionPoint->read(ExtensionPoint::OUTER,
                                              inBuffer.data(), inBuffer.size());
        if (result <= 0) {
            SPDLOG_ERROR("OutBound: read error({}) {}", errno, strerror(errno));
            wait100ms();
            continue;
        }

        // Write the packet to the serial interface
        ssize_t count = write(serialFileDescriptor, inBuffer.data(), result);
        if (count != result) {
            SPDLOG_ERROR("Serial write error({}) {}", errno, strerror(errno));
            wait100ms();
            continue;
        }

#ifndef NDEBUG
        SPDLOG_TRACE(
            "readOutBound {}:{:n}", count,
            spdlog::to_hex(std::begin(inBuffer), std::begin(inBuffer) + count));
        wait100ms();
#endif
    }

    SPDLOG_INFO("readOutBound thread stopped");
}

void CommDevices::readInBound()
{
    // Grab thread parameters
    if (extensionPoint.get() == nullptr)
        return;

    // Create incomming buffer
    std::array<char, ETHER_FRAME_LENGTH> inBuffer{};

    while (io_is_enabled()) {
        // read incomming byte count
        ssize_t count = extensionPoint->read(ExtensionPoint::INNER,
                                             inBuffer.data(), inBuffer.size());
        if (count <= 0) {
            SPDLOG_ERROR("InBound: read error({}) {}", errno, strerror(errno));
            wait100ms();
            continue;
        }

        // Write outgoing packet
        ssize_t result = write(tapFileDescriptor, inBuffer.data(), count);
        if (result < 0) {
            SPDLOG_ERROR("readInBound: write error({}) {}", errno,
                         strerror(errno));
            wait100ms();
            continue;
        }

#ifndef NDEBUG
        SPDLOG_TRACE(
            " readInBound {}:{:n}", count,
            spdlog::to_hex(std::begin(inBuffer), std::begin(inBuffer) + count));
        wait100ms();
#endif
    }

    SPDLOG_INFO("readInBound thread stopped");
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
            // selftest only:
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

    if (serialDevice[0] == '\0') {
        fprintf(stderr, "Serial port required (-d /dev/name)\n");
        return EXIT_FAILURE;
    }

    // NOTE: selftest only:
    int fd[2] = {-1, -1};
    if (mode == VTUN_PIPE) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        if (pipe_open(fd) < 0) {
            SPDLOG_ERROR("pipe_open() error({}) {}", errno, strerror(errno));
            return EXIT_FAILURE;
        }
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    int tapFd = tun_open_common(adapterName, mode);
    if (tapFd < 0) {
        SPDLOG_ERROR("tun_open_common() error({}) {}", errno, strerror(errno));
        if (mode != VTUN_PIPE) {
            return EXIT_FAILURE;
        }

        // NOTE: selftest only:
        SPDLOG_INFO("Test mode, use VTUN_PIPE first end!");
        tapFd = fd[0];
        char pingMsg[] = "\x05\0TapPing";
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        (void)write_n(tapFd, pingMsg, sizeof(pingMsg));
    }

    int serialFd = open(serialDevice, O_RDWR | O_CLOEXEC);
    if (serialFd < 0) {
        SPDLOG_ERROR("open() error({}) {}", errno, strerror(errno));
        if (mode != VTUN_PIPE) {
            close(tapFd);
            return EXIT_FAILURE;
        }

        // NOTE: selftest only:
        SPDLOG_INFO("Test mode, use VTUN_PIPE second end!");
        serialFd = fd[1];
        char pingMsg[] = "SerialPong";
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        (void)frame_write(serialFd, pingMsg, sizeof(pingMsg));
    }

    // register signal handler
    struct sigaction sa = {};
    sa.sa_handler = signal_handler;
    sigaction(SIGHUP, &sa, NULL);  // terminal line hangup (1)
    sigaction(SIGQUIT, &sa, NULL); // quit program (3)
    sigaction(SIGPIPE, &sa,
              NULL); // Broken pipe: write to pipe with no readers (13)
    // NO!   sigaction(SIGALRM, &sa, NULL); // XXX timer expired (14)
    // FIXME sigaction(SIGTERM, &sa, NULL); // software termination signal (15)

    SPDLOG_INFO("Starting threads");
    try {
        CommDevices::extensionPtr_t extension;
        if (red_node) {
            extension = std::make_shared<Pipe>();
        }
        CommDevices threadParams(tapFd, serialFd, mode, extension);

        // Create threads
        std::thread tap2serial(
            std::bind(&CommDevices::tapToSerial, threadParams));
        std::thread serial2tap(
            std::bind(&CommDevices::serialToTap, threadParams));

        // NOTE: selftest only:
        if (red_node || (mode == VTUN_PIPE)) {
            std::thread outBound(
                std::bind(&CommDevices::readOutBound, threadParams));
            outBound.detach();
            std::thread inBound(
                std::bind(&CommDevices::readInBound, threadParams));
            inBound.detach();
        }

        // NOTE: selftest only:
        if (mode == VTUN_PIPE) {
            alarm(4); // NOTE: XXX watchdog timer: send unhandled SIGALRM after
                      // 4 sec! CK
            char msg[] = "Hallo\n";
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            (void)write_n(1, msg, sizeof(msg)); // stdout
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            if (readn_t(0, msg, sizeof(msg), 3) < 0) {
                SPDLOG_INFO("Timeout while read from stdin");
            } else {
                sleep(1);
            }

            shutdown(serialFd, SHUT_WR);
            shutdown(tapFd, SHUT_WR);
            extension.reset();
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
