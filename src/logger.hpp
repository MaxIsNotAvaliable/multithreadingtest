#pragma once

#include <format>
#include <stop_token>
#include <algorithm>
#include <array>
#include <atomic>
#include <string_view>
#include <thread>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern void (*print)(std::string_view message);

class logger {
public:
  logger()/* : cells_(std::make_unique<cell[]>(capacity))*/ {
    for (std::size_t i = 0; i < capacity; ++i) {
      cells_[i].seq.store(i, std::memory_order_relaxed);
    }
  }

  logger(const logger&) = delete;
  logger& operator=(const logger&) = delete;

  // Вызывается из многих потоков. Копирует сообщение в очередь
  void post(std::string_view message) {
    std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

    for (;;) {
      cell& c = cells_[pos & mask];

      const std::size_t seq = c.seq.load(std::memory_order_acquire);
      const auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

      if (diff == 0) {
        // Слот свободен: пробуем зарезервировать позицию
        if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
          const std::size_t n = std::min(message.size(), max_message);
          c.len = n;
          if (n != 0) {
            std::memcpy(c.data, message.data(), n);
          }

          // Публикуем сообщение для consumer-потока
          c.seq.store(pos + 1, std::memory_order_release);
          return;
        }

        // CAS не удался — pos уже обновлён compare_exchange_weak
      } else if (diff < 0) {
        // Очередь заполнена: ждём, пока consumer освободит слот
        std::this_thread::yield();
        pos = enqueue_pos_.load(std::memory_order_relaxed);
      } else {
        pos = enqueue_pos_.load(std::memory_order_relaxed);
      }
    }
  }

  // Вызывается из потоков, форматирует и ставит сообщение в очередь
  template <class... Args>
  void post(std::format_string<Args...> fmt, Args&&... args) {
    post(std::format(fmt, std::forward<Args>(args)...));
  }

  // Вызывается из одного потока. Печатает сообщения по одному разу
  void run(std::stop_token stop) {
    for (;;) {
      if (try_pop_and_print()) {
        continue;
      }

      // Все producer-потоки уже joined, очередь пуста
      if (stop.stop_requested()) {
        return;
      }

      std::this_thread::yield();
    }
  }

private:
  static constexpr std::size_t max_message = 256;
  static constexpr std::size_t capacity = (1 << 10); // степень двойки позволяет нам использовать pos & mask, что быстрее чем %
  static constexpr std::size_t mask = capacity - 1; //

  static_assert((capacity & (capacity - 1)) == 0, "capacity must be a power of two");

  struct cell {
    std::atomic<std::size_t> seq;
    std::size_t len;
    char data[max_message];
  };

  bool try_pop_and_print() {
    const std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
    cell& c = cells_[pos & mask];

    if (c.seq.load(std::memory_order_acquire) != pos + 1) {
      return false;
    }

    char buffer[max_message];
    const std::size_t len = c.len;
    if (len != 0) {
      std::memcpy(buffer, c.data, len);
    }

    // Освобождаем слот до вызова print, чтобы producers реже блокировались
    c.seq.store(pos + capacity, std::memory_order_release);
    dequeue_pos_.store(pos + 1, std::memory_order_relaxed);

    print(std::string_view(buffer, len));
    return true;
  }

  std::array<cell, capacity> cells_;
  alignas(64) std::atomic<std::size_t> enqueue_pos_{ 0 };
  alignas(64) std::atomic<std::size_t> dequeue_pos_{ 0 };
};
