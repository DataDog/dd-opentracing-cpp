#ifndef DD_OPENTRACING_CLOCK_H
#define DD_OPENTRACING_CLOCK_H

#include <chrono>
#include <functional>

namespace datadog {
namespace opentracing {

using namespace std::chrono;

// TimePoint represents a single point in time, measured by both system_clock (to get a calendar
// time to base Spans off of) and a steady_clock (to get accurate durations).
struct TimePoint {
  system_clock::time_point absolute_time;
  steady_clock::time_point relative_time;

  // Returns the duration between two points, as given by the steady_clock.
  steady_clock::duration operator-(const TimePoint& other) {
    return relative_time - other.relative_time;
  }
};

// TimeProvider represents a way to determine the current time.
typedef std::function<TimePoint()> TimeProvider;

// getRealTime returns the actual system time.
inline TimePoint getRealTime() { return {system_clock::now(), steady_clock::now()}; };

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_CLOCK_H
