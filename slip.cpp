#include "slip.h"

enum slip_result slip_encode(const inBuffer_t &frame, size_t frameLength,
                             Buffer_t &output, size_t *outputSize)
{
    size_t outputIndex = 0;
    for (size_t inIndex = 0; inIndex < frameLength; inIndex++) {
        // Check if we ran out of space on the output
        if (output.size() <= outputIndex) {
            SPDLOG_ERROR("SLIP buffer overflow error!");
            return SLIP_BUFFER_OVERFLOW;
        }

        // Grab one byte from the input and check if we need to escape it
        uint8_t c = frame[inIndex];
        switch (c) {
        case SLIP_END:
            output[outputIndex] = SLIP_ESC;
            output[outputIndex + 1] = SLIP_ESC_END;
            outputIndex += 2;
            break;
        case SLIP_ESC:
            output[outputIndex] = SLIP_ESC;
            output[outputIndex + 1] = SLIP_ESC_ESC;
            outputIndex += 2;
            break;
        default:
            // No need to escape, copy as it is
            output[outputIndex] = c;
            outputIndex += 1;
            break;
        }
    }

    // Mark the frame end
    output[outputIndex] = SLIP_END;

    // Return the output size
    *outputSize = outputIndex + 1;
    return SLIP_OK;
}

enum slip_result slip_decode(const inBuffer_t &encodedFrame, size_t frameLength,
                             Buffer_t &output, size_t *outputSize)
{
    int invalidEscape = 0;

    size_t outputIndex = 0;
    for (size_t inIndex = 0; inIndex < frameLength; inIndex++) {
        // Check if we ran out of space on the output buffer
        if (output.size() <= outputIndex) {
            SPDLOG_ERROR("SLIP buffer overflow error!");
            return SLIP_BUFFER_OVERFLOW;
        }

        uint8_t inByte = encodedFrame[inIndex];
        if (inByte == SLIP_ESC) {
            switch (encodedFrame[inIndex + 1]) {
            case SLIP_ESC_END:
                output[outputIndex] = SLIP_END;
                break;
            case SLIP_ESC_ESC:
                output[outputIndex] = SLIP_ESC;
                break;
            default:
                // Escape sequence invalid, complain on stderr
                output[outputIndex] = SLIP_ESC;
                invalidEscape = 1;
                SPDLOG_ERROR(
                    "SLIP escape error! (Input bytes at({}): {:#04x}, {:#04x})",
                    inIndex, inByte, encodedFrame[inIndex + 1]);
                break;
            }
            inIndex += 1;
        } else if (inByte == SLIP_END) {
            // End of packet, stop the loop
            // NOTE: NO! outputIndex++; XXX CK
            break;
        } else {
            output[outputIndex] = inByte;
        }

        outputIndex++;
    }

    *outputSize = outputIndex;
    if (invalidEscape) {
        return SLIP_INVALID_ESCAPE;
    }
    return SLIP_OK;
}
