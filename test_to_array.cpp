#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

#if __has_include(<experimental/array>)
#    include <experimental/array>

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
#endif

int main()
{

#if __has_include(<experimental/array>)
    auto arr = std::experimental::make_array(1, 2, 3, 4, 5);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    assert(arr[0] == 1);

    int fd = tempfd("/tmp/test.XXXXXX");
    int err = close(fd);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    assert(err == 0);
#endif

    constexpr size_t len{6};
    std::array<char, len> test = {"Test:"};
    std::array<char, len> text = {"Hallo"};

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    assert(text[5] == 0);
    // C++ N4687: static const char __func__[] = "function-name";
    puts(__func__); // warning: do not implicitly decay an array into a pointer;

    if (test == text) {
        std::cout << static_cast<const char *>(__func__) << std::endl; // OK
        std::cout << static_cast<const char *>(__FUNCTION__) << std::endl;
        std::cout << static_cast<const char *>(__PRETTY_FUNCTION__) << std::endl;
    }
}
