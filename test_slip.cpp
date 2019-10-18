#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "slip.h"

#include <doctest/doctest.h>

#include <string.h>

const size_t BUF_MIN = 6;
const size_t BUF_MAX = 8;
const size_t BUF_LAST = 7;

TEST_CASE("testDecode")
{
    // Decode the packet that is marked by SLIP_END
    {
        uint8_t inBuffer[BUF_MAX] = {0, 1,        2,        3,
                                     4, SLIP_END, INT8_MAX, UINT8_MAX};
        uint8_t outBuffer[BUF_MAX] = {0};
        size_t outSize = 0;
        int inIndex = BUF_MAX;  // NOTE: not index of SLIP_END! CK

        enum slip_result result;
        result = slip_decode(inBuffer, inIndex, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == 5);
        CHECK(memcmp(inBuffer, outBuffer, outSize) == 0);
    }

    {
        uint8_t inBuffer[BUF_MAX] = {
            0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC_END, SLIP_END};
        uint8_t expected[BUF_MIN] = {0, 1, 2, 3, 4, SLIP_END};
        uint8_t outBuffer[BUF_MAX] = {0};
        size_t outSize = 0;
        int inIndex = sizeof(inBuffer);

        enum slip_result result;
        result = slip_decode(inBuffer, inIndex, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == BUF_MIN);
        CHECK(memcmp(expected, outBuffer, outSize) == 0);
    }

    {
        uint8_t inBuffer[BUF_MAX] = {
            0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC_ESC, SLIP_END};
        uint8_t expected[BUF_MIN] = {0, 1, 2, 3, 4, SLIP_ESC};
        uint8_t outBuffer[BUF_MAX] = {0};
        size_t outSize = 0;
        int inIndex = sizeof(inBuffer);

        enum slip_result result;
        result = slip_decode(inBuffer, inIndex, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == BUF_MIN);
        CHECK(memcmp(expected, outBuffer, outSize) == 0);
    }
}

TEST_CASE("testEncode")
{
    {
        uint8_t expected[BUF_MAX] = {0, 1, 2, 3, 4, SLIP_END};
        uint8_t inBuffer[] = {0, 1, 2, 3, 4};
        uint8_t outBuffer[BUF_MAX] = {0};
        size_t outSize = 0;
        int length = sizeof(inBuffer);

        enum slip_result result;
        result = slip_encode(inBuffer, length, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == BUF_MIN);
        CHECK(memcmp(expected, outBuffer, outSize) == 0);
    }

    {
        uint8_t expected[BUF_MAX] = {
            0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC_END, SLIP_END};
        uint8_t inBuffer[BUF_MIN] = {0, 1, 2, 3, 4, SLIP_END};
        uint8_t outBuffer[BUF_MAX] = {0};
        size_t outSize = 0;
        int length = sizeof(inBuffer);

        enum slip_result result;
        result = slip_encode(inBuffer, length, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == BUF_MAX);
        CHECK(memcmp(expected, outBuffer, outSize) == 0);
    }

    {
        uint8_t expected[BUF_MAX] = {
            0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC_ESC, SLIP_END};
        uint8_t inBuffer[BUF_MIN] = {0, 1, 2, 3, 4, SLIP_ESC};
        uint8_t outBuffer[BUF_MAX] = {0};
        size_t outSize = 0;
        int length = sizeof(inBuffer);

        enum slip_result result;
        result = slip_encode(inBuffer, length, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == BUF_MAX);
        CHECK(memcmp(expected, outBuffer, outSize) == 0);
    }
}

TEST_CASE("testDecodeErrors")
{
    // Decode the packet that is marked by SLIP_END
    {
        uint8_t inBuffer[BUF_MAX] = {0, 1,        2,        3,
                                     4, SLIP_ESC, SLIP_ESC, SLIP_END};
        uint8_t outBuffer[BUF_MIN] = {0};
        size_t outSize = 0;
        int inIndex = BUF_LAST;

        enum slip_result result;
        result = slip_decode(inBuffer, inIndex, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_INVALID_ESCAPE);
        CHECK(outSize == BUF_MIN);
    }

    {
        uint8_t inBuffer[BUF_MIN] = {0, 1, 2, 3, 4, SLIP_END};
        uint8_t outBuffer[BUF_MIN / 2] = {0};
        size_t outSize = 0;
        int inIndex = BUF_LAST;

        enum slip_result result;
        result = slip_decode(inBuffer, inIndex, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_BUFFER_OVERFLOW);
        CHECK(outSize == 0);
    }
}

TEST_CASE("testEncodeErrors")
{
    uint8_t inBuffer[BUF_MAX] = {0,       1, 2, 3, 4, SLIP_ESC, SLIP_ESC_END,
                                 SLIP_END};
    uint8_t outBuffer[BUF_MAX / 2] = {0};
    size_t outSize = 0;
    int inIndex = BUF_LAST;

    enum slip_result result;
    result =
        slip_encode(inBuffer, inIndex, outBuffer, sizeof(outBuffer), &outSize);

    CHECK(result == SLIP_BUFFER_OVERFLOW);
    CHECK(outSize == 0);
}
