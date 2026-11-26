#pragma once

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ch09/batch_audit_writer.hpp"
#include "ch09/buffer_pool.hpp"
#include "ch09/telemetry_sink.hpp"

namespace ch09 {

namespace detail {

// Closes a file descriptor on destruction. Move-only. Used instead of a
// bare int member so EventLoopConsumer's three descriptors close themselves
// automatically, in declaration order reversed, the same mechanism
// std::jthread gives EventLoopConsumer's worker_ (see below).
class ScopedFd {
public:
    ScopedFd() = default;
    explicit ScopedFd(int fd) : fd_(fd) {}
    ~ScopedFd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    ScopedFd& operator=(ScopedFd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }

private:
    int fd_ = -1;
};

}  // namespace detail

// The off-path background consumer: an event loop, on its own thread, that
// epoll_wait()s on two sources, the handoff queue's eventfd (new work is
// ready) and a timerfd (the batch has waited long enough; flush it anyway),
// so a batch flushes either once it is full or once max_batch_delay has
// passed, whichever comes first. This is the multiplexing this chapter
// teaches: one thread, two independent readiness sources, no busy-poll.
//
// Templated on the queue type only so the chapter's two queue sizes
// (HandoffQueue for the headline lane, SmallHandoffQueue for the
// backpressure lane) share one implementation.
template <typename QueueT>
class EventLoopConsumer {
public:
    EventLoopConsumer(QueueT& queue, BatchAuditWriter& writer, BufferPool<AuditRecord>& pool,
                       std::size_t batch_capacity, std::chrono::milliseconds max_batch_delay,
                       TelemetrySink* telemetry = nullptr)
        : queue_(queue),
          writer_(writer),
          pool_(pool),
          batch_capacity_(batch_capacity),
          telemetry_(telemetry),
          stop_fd_(make_eventfd()),
          timer_fd_(make_timerfd(max_batch_delay)),
          epoll_fd_(make_epoll_fd()) {
        register_fd(queue_.event_fd());
        register_fd(timer_fd_.get());
        register_fd(stop_fd_.get());
        // Started last: by the time worker_ exists, every fd it epoll_wait()s
        // on is already registered.
        worker_ = std::jthread([this](const std::stop_token& token) { run(token); });
    }

    ~EventLoopConsumer() = default;

    EventLoopConsumer(const EventLoopConsumer&) = delete;
    EventLoopConsumer& operator=(const EventLoopConsumer&) = delete;
    EventLoopConsumer(EventLoopConsumer&&) = delete;
    EventLoopConsumer& operator=(EventLoopConsumer&&) = delete;

private:
    static int make_eventfd() {
        const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (fd < 0) {
            throw std::runtime_error("EventLoopConsumer: eventfd() failed");
        }
        return fd;
    }

    static int make_timerfd(std::chrono::milliseconds period) {
        const int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (fd < 0) {
            throw std::runtime_error("EventLoopConsumer: timerfd_create() failed");
        }
        const auto period_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(period);
        itimerspec spec{};
        spec.it_value.tv_sec = period_ns.count() / 1'000'000'000;
        spec.it_value.tv_nsec = period_ns.count() % 1'000'000'000;
        spec.it_interval = spec.it_value;
        if (::timerfd_settime(fd, 0, &spec, nullptr) != 0) {
            ::close(fd);
            throw std::runtime_error("EventLoopConsumer: timerfd_settime() failed");
        }
        return fd;
    }

    static int make_epoll_fd() {
        const int fd = ::epoll_create1(EPOLL_CLOEXEC);
        if (fd < 0) {
            throw std::runtime_error("EventLoopConsumer: epoll_create1() failed");
        }
        return fd;
    }

    void register_fd(int fd) const {
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = fd;
        if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, fd, &event) != 0) {
            throw std::runtime_error("EventLoopConsumer: epoll_ctl() failed");
        }
    }

    void clear_timer() const {
        std::uint64_t expirations = 0;
        [[maybe_unused]] const ssize_t ignored = ::read(timer_fd_.get(), &expirations, sizeof(expirations));
    }

    // Drains up to batch_capacity_ records and, if any came out, flushes
    // them as one batch and records the flush's own duration. Returns the
    // number of records drained (0 means the queue was empty).
    std::size_t drain_and_flush() {
        auto buffer = pool_.acquire();
        const std::size_t popped = queue_.drain(*buffer, batch_capacity_);
        if (!buffer->empty()) {
            const auto start = std::chrono::steady_clock::now();
            writer_.flush_batch(*buffer);
            const double duration_us =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            if (telemetry_ != nullptr) {
                telemetry_->record_batch_flush(buffer->size(), duration_us);
            }
        }
        pool_.release(std::move(buffer));
        return popped;
    }

    void run(const std::stop_token& token) {
        // Registered on this thread so request_stop() (called from
        // ~std::jthread, on whichever thread destroys this object) writes
        // stop_fd_ and wakes the epoll_wait() below immediately, rather than
        // waiting for the next queue signal or timer tick.
        const std::stop_callback wake_on_stop(token, [this] {
            constexpr std::uint64_t one = 1;
            [[maybe_unused]] const ssize_t ignored = ::write(stop_fd_.get(), &one, sizeof(one));
        });

        bool stopping = false;
        while (!stopping) {
            std::array<epoll_event, 3> events{};
            const int ready = ::epoll_wait(epoll_fd_.get(), events.data(), static_cast<int>(events.size()), -1);
            if (ready < 0) {
                continue;  // EINTR or a transient error; the loop condition re-checks stop state next pass
            }
            bool should_flush = false;
            for (int i = 0; i < ready; ++i) {
                const int fd = events[static_cast<std::size_t>(i)].data.fd;
                if (fd == queue_.event_fd()) {
                    queue_.clear_event();
                    should_flush = true;
                } else if (fd == timer_fd_.get()) {
                    clear_timer();
                    should_flush = true;
                } else if (fd == stop_fd_.get()) {
                    stopping = true;
                }
            }
            if (should_flush) {
                drain_and_flush();
            }
        }
        // Final drain: guarantees every record already in the queue when
        // stop was requested is flushed before the consumer thread exits,
        // not left behind for a flush that never comes.
        while (drain_and_flush() > 0) {
        }
    }

    QueueT& queue_;
    BatchAuditWriter& writer_;
    BufferPool<AuditRecord>& pool_;
    std::size_t batch_capacity_;
    TelemetrySink* telemetry_;

    // Declared before worker_ so they outlive it: worker_'s std::jthread
    // destructor joins the background thread before these ScopedFds close,
    // the same ordering lesson "Exploiting concurrency" names for
    // ch06::ThreadPool (thread-owning members declare last).
    detail::ScopedFd stop_fd_;
    detail::ScopedFd timer_fd_;
    detail::ScopedFd epoll_fd_;
    std::jthread worker_;
};

}  // namespace ch09
