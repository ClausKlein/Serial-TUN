/**
 * @file SLIP encode/decode functions
 * @author Thanasis Georgiou
 */

#pragma once

#include <cstdint>
#include <cstdio>

enum
{
    SLIP_END = 0xC0,
    SLIP_ESC = 0xDB,
    SLIP_ESC_END = 0xDC,
    SLIP_ESC_ESC = 0xDD,
};

enum slip_result
{
    SLIP_OK = 0,
    SLIP_INVALID_ESCAPE = 1,
    SLIP_BUFFER_OVERFLOW = 2,
};

/**
 * Encode a piece of data according to the SLIP standard
 * @param frame             Data to encode
 * @param frameLength       Data length
 * @param output            Where to store the encoded frame
 * @param maxOutputSize     Max output size
 * @param outputSize        Where to store output length
 * @return SLIP_OK for success, otherwise error code
 */
enum slip_result slip_encode(const uint8_t *frame, size_t frameLength,
                             uint8_t *output, size_t maxOutputSize,
                             size_t *outputSize);

/**
 * Decode a SLIP packet
 * @param encodedFrame      Data to decode
 * @param frameLength       Data length
 * @param output            Where to store the decoded data
 * @param maxOutputSize     Max output size
 * @param outputSize        Where to store output length
 * @return SLIP_OK for success, otherwise error code
 */
enum slip_result slip_decode(const uint8_t *encodedFrame, size_t frameLength,
                             uint8_t *output, size_t maxOutputSize,
                             size_t *outputSize);
