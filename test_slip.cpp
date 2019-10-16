#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "slip.h"

#include <doctest/doctest.h>

#include <string.h>

TEST_CASE("testDecode")
{
    {
        unsigned char inBuffer[8] = {0, 1, 2, 3, 4, SLIP_END, 0xfe, 0xff};
        unsigned char outBuffer[8] = {0};
        size_t outSize = 0;
        int inIndex = 8;

        // Decode the packet that is marked by SLIP_END
        enum slip_result result;
        result = slip_decode(inBuffer, inIndex, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == 5);
        CHECK(memcmp(inBuffer, outBuffer, outSize) == 0);
    }

    {
        unsigned char inBuffer[16] = {
            0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC_END, SLIP_END};
        unsigned char expected[16] = {0, 1, 2, 3, 4, SLIP_END};
        unsigned char outBuffer[16] = {0};
        size_t outSize = 0;
        int inIndex = 7;

        // Decode the packet that is marked by SLIP_END
        enum slip_result result;
        result = slip_decode(inBuffer, inIndex, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == 6);
        CHECK(memcmp(expected, outBuffer, outSize) == 0);
    }

    {
        unsigned char inBuffer[16] = {
            0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC_ESC, SLIP_END};
        unsigned char expected[16] = {0, 1, 2, 3, 4, SLIP_ESC};
        unsigned char outBuffer[16] = {0};
        size_t outSize = 0;
        int inIndex = 7;

        // Decode the packet that is marked by SLIP_END
        enum slip_result result;
        result = slip_decode(inBuffer, inIndex, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == 6);
        CHECK(memcmp(expected, outBuffer, outSize) == 0);
    }
}

TEST_CASE("testEncode")
{
    {
        unsigned char expected[16] = {0, 1, 2, 3, 4, SLIP_END};
        unsigned char inBuffer[16] = {0, 1, 2, 3, 4};
        unsigned char outBuffer[16] = {0};
        size_t outSize = 0;
        int length = 5;

        enum slip_result result;
        result = slip_encode(inBuffer, length, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == 6);
        CHECK(memcmp(expected, outBuffer, outSize) == 0);
    }

    {
        unsigned char expected[16] = {
            0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC_END, SLIP_END};
        unsigned char inBuffer[16] = {0, 1, 2, 3, 4, SLIP_END};
        unsigned char outBuffer[16] = {0};
        size_t outSize = 0;
        int length = 6;

        enum slip_result result;
        result = slip_encode(inBuffer, length, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == 8);
        CHECK(memcmp(expected, outBuffer, outSize) == 0);
    }

    {
        unsigned char expected[16] = {
            0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC_ESC, SLIP_END};
        unsigned char inBuffer[16] = {0, 1, 2, 3, 4, SLIP_ESC};
        unsigned char outBuffer[16] = {0};
        size_t outSize = 0;
        int length = 6;

        enum slip_result result;
        result = slip_encode(inBuffer, length, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == 8);
        CHECK(memcmp(expected, outBuffer, outSize) == 0);
    }
}

TEST_CASE("testDecodeErrors")
{
    {
        unsigned char inBuffer[8] = {
            0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC, SLIP_END};
        unsigned char outBuffer[6] = {0};
        size_t outSize = 0;
        int inIndex = 7;

        // Decode the packet that is marked by SLIP_END
        enum slip_result result;
        result = slip_decode(inBuffer, inIndex, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_INVALID_ESCAPE);
        CHECK(outSize == 6);
    }
    
    {
        unsigned char inBuffer[6] = {0, 1, 2, 3, 4, SLIP_END};
        unsigned char outBuffer[5] = {0};
        size_t outSize = 0;
        int inIndex = 6;

        // Decode the packet that is marked by SLIP_END
        enum slip_result result;
        result = slip_decode(inBuffer, inIndex, outBuffer, sizeof(outBuffer),
                             &outSize);

        CHECK(result == SLIP_BUFFER_OVERFLOW);
        CHECK(outSize == 0);
    }
}

TEST_CASE("testEncodeErrors")
{
    unsigned char inBuffer[8] = {
        0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC_END, SLIP_END};
    unsigned char outBuffer[5] = {0};
    size_t outSize = 0;
    int inIndex = 7;

    enum slip_result result;
    result = slip_encode(inBuffer, inIndex, outBuffer, sizeof(outBuffer),
                         &outSize);

    CHECK(result == SLIP_BUFFER_OVERFLOW);
    CHECK(outSize == 0);
}

