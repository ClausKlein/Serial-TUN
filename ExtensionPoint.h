#pragma once

#include "tun-driver.h"

#include <array>
#include <cerrno>

class ExtensionPoint
{
public:
    enum Channel
    {
        IN_BOUND = 0,
        OUT_BOUND = 1
    };

    ExtensionPoint() {}
    virtual ~ExtensionPoint() {}

    virtual ssize_t read(Channel fd, void *buf, size_t count) noexcept = 0;
    virtual ssize_t write(Channel fd, const void *buf,
                          size_t count) noexcept = 0;
};

class Pipe : public ExtensionPoint
{
public:
    Pipe()
    {
        if (pipe_open(fd.data()) < 0) {
            SPDLOG_ERROR("pipe_open() error({}) {}", errno, strerror(errno));
        }
    }

    ~Pipe() override
    {
        close(fd[IN_BOUND]);
        close(fd[OUT_BOUND]);
    }

    ssize_t read(Channel id, void *buf, size_t count) noexcept override
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        return ::read(fd[id], buf, count);
    }
    ssize_t write(Channel id, const void *buf, size_t count) noexcept override
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        return ::write(fd[id], buf, count);
    }

private:
    std::array<int, 2> fd{{-1, -1}};
};
