#pragma once
#include <benchmark/benchmark.h>
#include <chrono>
#include <span>
#include <string_view>
#include <thread>

constexpr benchmark::IterationCount benchmark_iterations{
#ifdef NDEBUG
  1'024 * 128 
#else
  1'024
#endif
};

extern std::span<const std::string_view> benchmark_messages;

template <class Logger>
Logger& get_logger() {
  static Logger logger;
  static const std::jthread thread{ std::bind_front(&Logger::run, &logger) };
  return logger;
}

template <class Logger>
void string(benchmark::State& state) {
  auto& logger = get_logger<Logger>();
  const auto size = benchmark_messages.size();
  logger.post(benchmark_messages[0]);  // warmup
  std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
  std::size_t i{};
  for (auto _ : state) {
    logger.post(benchmark_messages[i++ % size]);
  }
}

template <class Logger>
void format(benchmark::State& state) {
  auto& logger = get_logger<Logger>();
  if constexpr (requires { requires Logger::tests_templated_post; }) {
    logger.template post<"{} format test message">(0);  // warmup
  } else {
    logger.post("{} format test message", 0);  // warmup
  }
  std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
  std::size_t i{};
  for (auto _ : state) {
    if constexpr (requires { requires Logger::tests_templated_post; }) {
      logger.template post<"{} format test message">(i++);
    } else {
      logger.post("{} format test message", i++);
    }
  }
}
