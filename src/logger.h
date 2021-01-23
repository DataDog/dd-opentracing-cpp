#ifndef DD_OPENTRACING_LOGGER_H
#define DD_OPENTRACING_LOGGER_H

#include "datadog/opentracing.h"

namespace datadog {
namespace opentracing {

class Logger {
 public:
  virtual void Log(LogLevel level, ot::string_view message) const noexcept = 0;
  virtual void Log(LogLevel level, uint64_t trace_id, ot::string_view message) const noexcept = 0;
  virtual void Log(LogLevel level, uint64_t trace_id, uint64_t span_id,
                   ot::string_view message) const noexcept = 0;
  virtual void Trace(ot::string_view message) const noexcept = 0;
  virtual void Trace(uint64_t trace_id, ot::string_view message) const noexcept = 0;
  virtual void Trace(uint64_t trace_id, uint64_t span_id, ot::string_view message) const
      noexcept = 0;

 protected:
  Logger(LogFunc log_func) : log_func_(log_func) {}
  virtual ~Logger() = default;
  LogFunc log_func_;
};

// The standard logger provides stub implementations of Trace methods, that reduces the
// performance hit when this level of detail is disabled.
class StandardLogger final : public Logger {
 public:
  StandardLogger(LogFunc log_func) : Logger(log_func) {}
  void Log(LogLevel level, ot::string_view message) const noexcept override;
  void Log(LogLevel level, uint64_t trace_id, ot::string_view message) const noexcept override;
  void Log(LogLevel level, uint64_t trace_id, uint64_t span_id, ot::string_view message) const
      noexcept override;
  void Trace(ot::string_view) const noexcept override {}
  void Trace(uint64_t, ot::string_view) const noexcept override {}
  void Trace(uint64_t, uint64_t, ot::string_view) const noexcept override {}
};

class VerboseLogger final : public Logger {
 public:
  VerboseLogger(LogFunc log_func) : Logger(log_func) {}
  void Log(LogLevel level, ot::string_view message) const noexcept override;
  void Log(LogLevel level, uint64_t trace_id, ot::string_view message) const noexcept override;
  void Log(LogLevel level, uint64_t trace_id, uint64_t span_id, ot::string_view message) const
      noexcept override;
  void Trace(ot::string_view message) const noexcept override;
  void Trace(uint64_t trace_id, ot::string_view message) const noexcept override;
  void Trace(uint64_t trace_id, uint64_t span_id, ot::string_view message) const noexcept override;
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_LOGGER_H
