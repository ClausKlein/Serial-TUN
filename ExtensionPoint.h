#pragma once

#include "tun-driver.h"

// XXX #include <boost/core/noncopyable.hpp>

#include <array>
#include <cerrno>

class ExtensionPoint
{
public:
    enum Channel
    {
        OUTER = 0,
        INNER = 1
    };

    ExtensionPoint(const ExtensionPoint &) = delete;
    void operator=(const ExtensionPoint &) = delete;

    ExtensionPoint(ExtensionPoint &&) = delete;
    ExtensionPoint &operator=(ExtensionPoint &&) = delete;

    virtual ~ExtensionPoint() = default;

    virtual ssize_t read(Channel fd, void *buf, size_t count) noexcept = 0;
    virtual ssize_t write(Channel fd, const void *buf,
                          size_t count) noexcept = 0;

protected:
    ExtensionPoint() = default;
};

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class Pipe : public ExtensionPoint // XXX , private boost::noncopyable
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
        close(fd[OUTER]);
        close(fd[INNER]);
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
