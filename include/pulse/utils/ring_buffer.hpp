#pragma once

#include <cstddef>
#include <deque>
#include <stdexcept>

namespace pulse {

// Keep the newest fixed number of metric samples for terminal graphs.
template <typename T> class RingBuffer {
  public:
    explicit RingBuffer(std::size_t capacity) : capacity_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("ring buffer capacity must be positive");
        }
    }

    // Append a value and discard the oldest value when the buffer is full.
    void push(T value) {
        if (values_.size() == capacity_) {
            values_.pop_front();
        }
        values_.push_back(std::move(value));
    }

    [[nodiscard]] const std::deque<T>& values() const noexcept { return values_; }
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
    [[nodiscard]] const T& back() const { return values_.back(); }

  private:
    std::size_t capacity_;
    std::deque<T> values_;
};

} // namespace pulse
