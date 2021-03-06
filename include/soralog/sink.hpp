/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SORALOG_SINK
#define SORALOG_SINK

#include <memory>
#include <string>
#include <string_view>

#include <soralog/circular_buffer.hpp>
#include <soralog/event.hpp>

#ifdef NDEBUG
#define IF_RELEASE true
#else
#define IF_RELEASE false
#endif

namespace soralog {

  /**
   * @class Sink
   * This is base class of all sink.
   * It is accumulate events in inner lock-free circular buffer and drop it into
   * destination place on demand or condition
   */
  class Sink {
   public:
    enum class ThreadInfoType {
      NONE,  //!< No log thread info
      NAME,  //!< Log thread name
      ID     //!< Log thread id
    };

    Sink() = delete;
    Sink(const Sink &) = delete;
    Sink(Sink &&) noexcept = delete;
    virtual ~Sink() = default;
    Sink &operator=(Sink const &) = delete;
    Sink &operator=(Sink &&) noexcept = delete;

    Sink(std::string name, ThreadInfoType thread_info_type, size_t max_events,
         size_t max_message_length, size_t max_buffer_size, size_t latency)
        : name_(std::move(name)),
          thread_info_type_(thread_info_type),
          max_message_length_(max_message_length),
          max_buffer_size_(max_buffer_size),
          latency_(latency),
          events_(max_events, max_message_length) {
      // Auto-fix buffer size
      if (max_buffer_size_ < max_message_length * 2) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast,-warnings-as-errors)
        const_cast<size_t &>(max_buffer_size_) = max_message_length * 2;
      }
    }

    Sink(std::string name, std::vector<std::shared_ptr<Sink>> sinks)
        : name_(std::move(name)),
          thread_info_type_(),
          max_message_length_(),
          max_buffer_size_(),
          latency_(),
          events_(0, 0),
          underlying_sinks_(std::move(sinks)){};

    /**
     * @returns name of sink
     */
    const std::string &name() const noexcept {
      return name_;
    }

    /**
     * Emplaces new log event
     * @param name is name of logger
     * @param level is level log event
     * @param format is format of message
     * @param args arguments is of log message
     */
    template <typename Format, typename... Args>
    void push(std::string_view name, Level level, const Format &format,
              const Args &...args) noexcept(IF_RELEASE) {
      if (underlying_sinks_.empty()) {
        while (true) {
          auto node = events_.put(name, thread_info_type_, level, format,
                                  max_message_length_, args...);

          // Event is queued successfully
          if (node) {
            size_ += node->message().size();
            node.release();
            break;
          }

          // Events queue is full. Flush immediately and try to push again
          flush();
        }

        if (latency_ == std::chrono::milliseconds::zero()) {
          flush();
        } else if (size_ >= max_buffer_size_ * 4 / 5) {
          async_flush();
        }
      } else {
        for (const auto &sink : underlying_sinks_) {
          sink->push(name, level, format, args...);
        }
      }
    }

    /**
     * Does writing all events in destination place immediately
     */
    virtual void flush() noexcept = 0;

    /**
     * Does writing all events in destination place asynchronously
     */
    virtual void async_flush() noexcept = 0;

    /**
     * Does some actions to rorate log data (e.g. reopen log-file)
     */
    virtual void rotate() noexcept = 0;

   protected:
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    const std::string name_;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    const ThreadInfoType thread_info_type_;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    const size_t max_buffer_size_;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    const std::chrono::milliseconds latency_;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    const size_t max_message_length_;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    CircularBuffer<Event> events_;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    std::atomic_size_t size_ = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    const std::vector<std::shared_ptr<Sink>> underlying_sinks_{};
  };

}  // namespace soralog

#endif  // SORALOG_SINK
