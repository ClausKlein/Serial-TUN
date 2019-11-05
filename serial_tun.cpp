#include "slip.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <libserialport.h>
#include <pthread.h>

struct CommDevices
{
    int tunFileDescriptor;
    struct sp_port *serialPort;
};

char adapterName[IF_NAMESIZE];
char serialPortName[128];
unsigned serialBaudRate = 9600;

static void *serialToTun(void *ptr);
static void *tunToSerial(void *ptr);

/**
 * Handles getting packets from the serial port and writing them to the TUN
 * interface
 * @param ptr       - Pointer to the CommDevices struct
 */
static void *serialToTun(void *ptr)
{
    // Grab thread parameters
    struct CommDevices *args = static_cast<struct CommDevices *>(ptr);

    int tunFd = args->tunFileDescriptor;
    struct sp_port *serialPort = args->serialPort;

    // Create two buffers, one to store raw data from the serial port and
    // one to store SLIP frames
    Buffer_t inBuffer(SLIP_IN_FRAME_LENGTH);
    Buffer_t outBuffer(SLIP_OUT_FRAME_LENGTH);
    size_t outSize = 0;
    int inIndex = 0;

    // Incoming byte count
    size_t count;

    // Serial result
    enum sp_return serialResult;

    // Add 'RX ready' event to serial port
    struct sp_event_set *eventSet;
    sp_new_event_set(&eventSet);
    sp_add_port_events(eventSet, serialPort, SP_EVENT_RX_READY);

    while (true) {
        // Wait for the event (RX Ready)
        sp_wait(eventSet, 0);
        count = sp_input_waiting(serialPort); // Bytes ready for reading

        // Read bytes from serial
        serialResult =
            sp_blocking_read(serialPort, &inBuffer[inIndex], count, 0);

        if (serialResult < 0) {
            std::cerr << "Serial error! " << serialResult << std::endl;
        } else {
            // We need to check if there is an SLIP_END sequence in the new
            // bytes
            for (int i = 0; i < serialResult; i++) {
                if (inBuffer[inIndex] == SLIP_END) {
                    // Decode the packet that is marked by SLIP_END
                    slip_decode(inBuffer, inIndex, outBuffer, &outSize);

                    // Write the packet to the virtual interface
                    write(tunFd, outBuffer.data(), outSize);

                    // Copy the remaining data (belonging to the next packet)
                    // to the start of the buffer
                    memcpy(inBuffer.data(), &inBuffer[inIndex + 1],
                           serialResult - i - 1);
                    inIndex = serialResult - i - 1;
                    break;
                }

                inIndex++;
            }
        }
    }

    return ptr;
}

static void *tunToSerial(void *ptr)
{
    // Grab thread parameters
    struct CommDevices *args = static_cast<struct CommDevices *>(ptr);

    int tunFd = args->tunFileDescriptor;
    struct sp_port *serialPort = args->serialPort;

    // Create TUN buffer
    Buffer_t inBuffer(SLIP_IN_FRAME_LENGTH);
    Buffer_t outBuffer(SLIP_OUT_FRAME_LENGTH);

    // Incoming byte count
    ssize_t count;

    // Encoded data size
    unsigned long encodedLength = 0;

    // Serial error messages
    enum sp_return serialResult;

    while (true) {
        count = read(tunFd, inBuffer.data(), inBuffer.size());
        if (count < 0) {
            std::cerr << "Could not read from interface\n";
            continue;
        }

        // Encode data
        slip_encode(inBuffer, (size_t)count, outBuffer, &encodedLength);

        // Write to serial port
        serialResult =
            sp_nonblocking_write(serialPort, outBuffer.data(), encodedLength);
        if (serialResult < 0) {
            std::cerr << "Could not send data to serial port: " << serialResult
                      << std::endl;
        }
    }

    return ptr;
}

int main(int argc, char *argv[])
{
    // Grab parameters
    int param;
    while ((param = getopt(argc, argv, "i:p:b:")) > 0) {
        switch (param) {
        case 'i':
            strncpy(static_cast<char *>(adapterName), optarg, IFNAMSIZ - 1);
            break;
        case 'p':
            strncpy(static_cast<char *>(serialPortName), optarg,
                    sizeof(serialPortName) - 1);
            break;
        case 'b':
            serialBaudRate = strtoul(optarg, NULL, 10);
            break;
        default:
            std::cerr << "Unknown parameter " << param << std::endl;
            break;
        }
    }

    if (adapterName[0] == '\0') {
        std::cerr << "Adapter name required (-i)\n";
        return EXIT_FAILURE;
    }
    if (serialPortName[0] == '\0') {
        std::cerr << "Serial port required (-p)\n";
        return EXIT_FAILURE;
    }

    int tunFd = tun_open_common(static_cast<char *>(adapterName), VTUN_P2P);
    if (tunFd < 0) {
        std::cerr << "Could not open /dev/net/tun\n";
        return EXIT_FAILURE;
    }

    // Configure & open serial port
    struct sp_port *serialPort;
    sp_get_port_by_name(static_cast<char *>(serialPortName), &serialPort);

    enum sp_return status = sp_open(serialPort, SP_MODE_READ_WRITE);

    sp_set_bits(serialPort, 8);
    sp_set_parity(serialPort, SP_PARITY_NONE);
    sp_set_stopbits(serialPort, 1);
    sp_set_baudrate(serialPort, serialBaudRate);
    sp_set_xon_xoff(serialPort, SP_XONXOFF_DISABLED);
    sp_set_flowcontrol(serialPort, SP_FLOWCONTROL_NONE);

    if (status < 0) {
        std::cerr << "Could not open serial port: ";
        switch (status) {
        case SP_ERR_ARG:
            std::cerr << "Invalid argument\n";
            break;
        case SP_ERR_FAIL:
            std::cerr << "System error\n";
            break;
        case SP_ERR_MEM:
            std::cerr << "Memory allocation error\n";
            break;
        case SP_ERR_SUPP:
            std::cerr << "Operation not supported by device\n";
            break;
        default:
            std::cerr << "Unknown error\n";
            break;
        }
        return EXIT_FAILURE;
    }

    // Create threads
    pthread_t tun2serial, serial2tun;
    struct CommDevices threadParams = {};
    threadParams.tunFileDescriptor = tunFd;
    threadParams.serialPort = serialPort;

    puts("Starting threads");
    pthread_create(&tun2serial, NULL, tunToSerial, (void *)&threadParams);
    pthread_create(&serial2tun, NULL, serialToTun, (void *)&threadParams);

    pthread_join(tun2serial, NULL);
    puts("Thread tun-to-network returned");
    pthread_join(serial2tun, NULL);
    puts("Thread network-to-tun returned");

    return EXIT_SUCCESS;
}
