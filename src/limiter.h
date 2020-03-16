#ifndef DD_OPENTRACING_LIMITER_H
#define DD_OPENTRACING_LIMITER_H

#include <mutex>
#include <vector>
#include "clock.h"

namespace datadog {
namespace opentracing {

struct LimitResult {
  bool allowed;
  double effective_rate;
};

class Limiter {
 public:
  Limiter(TimeProvider clock, long max_tokens, double refresh_rate, long tokens_per_refresh);

  LimitResult allow();
  LimitResult allow(long tokens);

 private:
  mutable std::mutex mutex_;
  TimeProvider now_func_;
  long num_tokens_;
  long max_tokens_;
  std::chrono::steady_clock::duration refresh_interval_;
  long tokens_per_refresh_;
  std::chrono::steady_clock::time_point next_refresh_;
  // effective rate fields
  std::vector<double> previous_rates_;
  double previous_rates_sum_;
  std::chrono::steady_clock::time_point current_period_;
  long num_allowed_ = 0;
  long num_requested_ = 0;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_LIMITER_H
