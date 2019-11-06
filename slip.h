/**
 * @file SLIP encode/decode functions
 * @author Thanasis Georgiou
 */

#pragma once

#include "tun-driver.h"
//XXX #include "gsl/gsl-lite.hpp"

#include <vector>

enum
{
    SLIP_END = 0xC0,
    SLIP_ESC = 0xDB,
    SLIP_ESC_END = 0xDC,
    SLIP_ESC_ESC = 0xDD,
    SLIP_IN_FRAME_LENGTH = 2048,
    SLIP_OUT_FRAME_LENGTH = 4098,
};

enum slip_result
{
    SLIP_OK = 0,
    SLIP_INVALID_ESCAPE = 1,
    SLIP_BUFFER_OVERFLOW = 2,
};

typedef gsl::span<uint8_t> inBuffer_t;
typedef std::vector<uint8_t> Buffer_t;

/**
 * Encode a piece of data according to the SLIP standard
 * @param frame             Data to encode
 * @param frameLength       Data length
 * @param output            Where to store the encoded frame
 * @param outputSize        Where to store output length
 * @return SLIP_OK for success, otherwise error code
 */
enum slip_result slip_encode(const inBuffer_t &frame, size_t frameLength,
                             Buffer_t &output, size_t *outputSize);

/**
 * Decode a SLIP packet
 * @param encodedFrame      Data to decode
 * @param frameLength       Data length
 * @param output            Where to store the decoded data
 * @param outputSize        Where to store output length
 * @return SLIP_OK for success, otherwise error code
 */
enum slip_result slip_decode(const inBuffer_t &encodedFrame, size_t frameLength,
                             Buffer_t &output, size_t *outputSize);
