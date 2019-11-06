#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <array>
#include <iostream>
#include <string>
#include <vector>

#if __has_include(<experimental/array>)
#    include <cstdio>
#    include <cstdlib>
#    include <experimental/array>
#    include <unistd.h>

// mkstemp(3) that works
// warning: do not declare C-style arrays, use std::array<> instead
template <std::size_t N> int tempfd(char const (&tmpl)[N])
{
    auto s = std::experimental::to_array(tmpl);
    int fd = mkstemp(s.data());
    if (fd != -1) {
        unlink(s.data());
    }

    return fd;
}

TEST_CASE("Make_array")
{
    auto arr = std::experimental::make_array(1, 2, 3, 4, 5);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    CHECK(arr[0] == 1);

    int fd = tempfd("/tmp/test.XXXXXX");
    int err = close(fd);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    CHECK(err == 0);

    // C++ N4687: static const char __func__[] = "function-name";
    puts(__func__); // warning: do not implicitly decay an array into a pointer;
}
#endif

TEST_CASE("test__array")
{
    constexpr size_t len{6};
    constexpr std::array<char, len> test = {"Test:"};
    constexpr std::array<char, len> text = {"Hallo"};

    CHECK(text.front() == 'H');
    CHECK(text.back() == 0);
    CHECK(test != text);

    if (test == text) {
        std::cout << static_cast<const char *>(__func__) << std::endl; // OK
        std::cout << static_cast<const char *>(__FUNCTION__) << std::endl;
        std::cout << static_cast<const char *>(__PRETTY_FUNCTION__)
                  << std::endl;
    }
}

TEST_CASE("test_vector")
{
    constexpr size_t size{6};
    std::vector<char> buffer;
    buffer.reserve(size);
    CHECK(buffer.size() == 0);
    CHECK(buffer.capacity() == size);

    std::vector<std::string> plain(size);
    CHECK(plain.size() == size);
    CHECK(plain.capacity() == size);

    // words is {"Mo", "Mo", "Mo", "Mo", "Mo", "Mo"}
    std::vector<std::string> words(size, "Mo");
    for (const auto &w : words) {
        CHECK(w == "Mo");
    }
}
