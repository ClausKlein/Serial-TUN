#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "slip.h"

#include <doctest/doctest.h>

#include <cstring>
#include <vector>

const size_t BUF_MIN = 6;
const size_t BUF_MAX = 8;
const size_t BUF_LEN = 5;
typedef std::vector<uint8_t> smallBuffer_t;

TEST_CASE("testDecode")
{
    // Decode the packet that is marked by SLIP_END
    {
        smallBuffer_t inBuffer = {0, 1, 2, 3, 4, SLIP_END, INT8_MAX, UINT8_MAX};
        smallBuffer_t outBuffer(BUF_MAX, 0);
        size_t outSize = 0;

        enum slip_result result;
        result = slip_decode(inBuffer, inBuffer.size(), outBuffer, &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == BUF_LEN);
        CHECK(memcmp(inBuffer.data(), outBuffer.data(), outSize) == 0);
    }

    {
        smallBuffer_t inBuffer = {0,       1, 2, 3, 4, SLIP_ESC, SLIP_ESC_END,
                                  SLIP_END};
        uint8_t expected[BUF_MIN] = {0, 1, 2, 3, 4, SLIP_END};
        gsl::span<uint8_t> arr_view = {expected};
        smallBuffer_t outBuffer(BUF_MAX, 0);
        size_t outSize = 0;

        enum slip_result result;
        result = slip_decode(inBuffer, inBuffer.size(), outBuffer, &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == BUF_MIN);
        CHECK(memcmp(arr_view.data(), outBuffer.data(), outSize) == 0);
    }

    {
        smallBuffer_t inBuffer = {0,       1, 2, 3, 4, SLIP_ESC, SLIP_ESC_ESC,
                                  SLIP_END};
        uint8_t expected[BUF_MIN] = {0, 1, 2, 3, 4, SLIP_ESC};
        gsl::span<uint8_t> arr_view = {expected};
        smallBuffer_t outBuffer(BUF_MAX, 0);
        size_t outSize = 0;

        enum slip_result result;
        result = slip_decode(inBuffer, inBuffer.size(), outBuffer, &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == BUF_MIN);
        CHECK(memcmp(arr_view.data(), outBuffer.data(), outSize) == 0);
    }
}

TEST_CASE("testEncode")
{
    {
        uint8_t expected[BUF_MAX] = {0, 1, 2, 3, 4, SLIP_END};
        gsl::span<uint8_t> arr_view = {expected};
        smallBuffer_t inBuffer = {0, 1, 2, 3, 4};
        smallBuffer_t outBuffer(BUF_MAX, 0);
        size_t outSize = 0;

        enum slip_result result;
        result = slip_encode(inBuffer, inBuffer.size(), outBuffer, &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == BUF_MIN);
        CHECK(memcmp(arr_view.data(), outBuffer.data(), outSize) == 0);
    }

    {
        uint8_t expected[BUF_MAX] = {
            0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC_END, SLIP_END};
        gsl::span<uint8_t> arr_view = {expected};
        smallBuffer_t inBuffer = {0, 1, 2, 3, 4, SLIP_END};
        smallBuffer_t outBuffer(BUF_MAX, 0);
        size_t outSize = 0;

        enum slip_result result;
        result = slip_encode(inBuffer, inBuffer.size(), outBuffer, &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == BUF_MAX);
        CHECK(memcmp(arr_view.data(), outBuffer.data(), outSize) == 0);
    }

    {
        uint8_t expected[BUF_MAX] = {
            0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC_ESC, SLIP_END};
        gsl::span<uint8_t> arr_view = {expected};
        smallBuffer_t inBuffer = {0, 1, 2, 3, 4, SLIP_ESC};
        smallBuffer_t outBuffer(BUF_MAX, 0);
        size_t outSize = 0;

        enum slip_result result;
        result = slip_encode(inBuffer, inBuffer.size(), outBuffer, &outSize);

        CHECK(result == SLIP_OK);
        CHECK(outSize == BUF_MAX);
        CHECK(memcmp(arr_view.data(), outBuffer.data(), outSize) == 0);
    }
}

TEST_CASE("testDecodeErrors")
{
    // Decode the packet that is marked by SLIP_END
    {
        smallBuffer_t inBuffer = {0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC, SLIP_END};
        smallBuffer_t outBuffer(BUF_MAX, 0);
        size_t outSize = 0;

        enum slip_result result;
        result = slip_decode(inBuffer, inBuffer.size(), outBuffer, &outSize);

        CHECK(result == SLIP_INVALID_ESCAPE);
        CHECK(outSize == BUF_MIN);
    }

    {
        smallBuffer_t inBuffer = {0, 1, 2, 3, 4, SLIP_END};
        smallBuffer_t outBuffer(BUF_LEN - 1, 0);
        size_t outSize = 0;

        enum slip_result result;
        result = slip_decode(inBuffer, inBuffer.size(), outBuffer, &outSize);

        CHECK(result == SLIP_BUFFER_OVERFLOW);
        CHECK(outSize == 0);
    }
}

TEST_CASE("testEncodeErrors")
{
    smallBuffer_t inBuffer = {0, 1, 2, 3, 4, SLIP_ESC, SLIP_ESC_END, SLIP_END};
    smallBuffer_t outBuffer(BUF_LEN - 1, 0);
    size_t outSize = 0;

    enum slip_result result;
    result = slip_encode(inBuffer, inBuffer.size(), outBuffer, &outSize);

    CHECK(result == SLIP_BUFFER_OVERFLOW);
    CHECK(outSize == 0);
}
