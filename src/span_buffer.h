#ifndef DD_OPENTRACING_SPAN_BUFFER_H
#define DD_OPENTRACING_SPAN_BUFFER_H

#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "propagation.h"
#include "sample.h"
#include "span.h"

namespace datadog {
namespace opentracing {

class Writer;
class SpanContext;
using TraceData = std::vector<std::unique_ptr<SpanData>>;

struct SamplingStatus {
  bool is_set = false;
  bool is_propagated = false;
  SampleResult sample_result;
};

class ActiveTrace {
 public:
  ActiveTrace(std::shared_ptr<const Logger> logger, std::shared_ptr<Writer> writer,
              uint64_t trace_id)
      : logger_(logger), writer_(writer), trace_id_(trace_id) {}
  ActiveTrace(std::shared_ptr<const Logger> logger, std::shared_ptr<Writer> writer,
              uint64_t trace_id, SamplingStatus sampling_status)
      : logger_(logger), writer_(writer), trace_id_(trace_id), sampling_status_(sampling_status) {}

  void addSpan(uint64_t span_id);
  void finishSpan(SpanContext& context, std::unique_ptr<SpanData> span_data);
  void setSamplingPriority(UserSamplingPriority priority);
  void setSampleResult(SampleResult result);
  void setPropagated();
  SamplingStatus samplingStatus();

 private:
  std::shared_ptr<const Logger> logger_;
  std::shared_ptr<Writer> writer_;
  uint64_t trace_id_;
  std::mutex mutex_;
  TraceData finished_spans_;
  std::unordered_set<uint64_t> expected_spans_;
  SamplingStatus sampling_status_;
  std::string hostname_;
  double analytics_rate_ = std::nan("");
};

}  // namespace opentracing
}  // namespace datadog

#endif  // DD_OPENTRACING_SPAN_BUFFER_H
