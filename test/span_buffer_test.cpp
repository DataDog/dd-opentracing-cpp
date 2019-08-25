#include "../src/span_buffer.h"
#include "../src/sample.h"
#include "mocks.h"

#include <catch2/catch.hpp>
using namespace datadog::opentracing;

TEST_CASE("span buffer") {
  auto sampler = std::make_shared<KeepAllSampler>();
  auto writer_ptr = std::make_shared<MockWriter>(sampler);
  MockWriter* writer = writer_ptr.get();
  auto buffer = std::make_shared<WritingSpanBuffer>(writer_ptr, WritingSpanBufferOptions{});

  auto context_from_span = [](const TestSpanData& span) -> SpanContext {
    return SpanContext{span.span_id, span.trace_id, "", {}};
  };

  SECTION("can write a single-span trace") {
    auto span = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420, 420, 0,
                                               123, 456, 0);
    buffer->registerSpan(context_from_span(*span));
    buffer->finishSpan(std::move(span), sampler);
    REQUIRE(writer->traces.size() == 1);
    REQUIRE(writer->traces[0].size() == 1);
    auto& result = writer->traces[0][0];
    REQUIRE(result->name == "name");
    REQUIRE(result->service == "service");
    REQUIRE(result->resource == "resource");
    REQUIRE(result->type == "type");
    REQUIRE(result->span_id == 420);
    REQUIRE(result->trace_id == 420);
    REQUIRE(result->parent_id == 0);
    REQUIRE(result->error == 0);
    REQUIRE(result->start == 123);
    REQUIRE(result->duration == 456);
    REQUIRE(result->meta == std::unordered_map<std::string, std::string>{});
  }

  SECTION("can write a multi-span trace") {
    auto rootSpan = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420, 420,
                                                   0, 123, 456, 0);
    buffer->registerSpan(context_from_span(*rootSpan));
    auto childSpan = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420,
                                                    421, 0, 124, 455, 0);
    buffer->registerSpan(context_from_span(*childSpan));
    buffer->finishSpan(std::move(childSpan), sampler);
    buffer->finishSpan(std::move(rootSpan), sampler);
    REQUIRE(writer->traces.size() == 1);
    REQUIRE(writer->traces[0].size() == 2);
    // Although order doesn't actually matter.
    REQUIRE(writer->traces[0][0]->span_id == 421);
    REQUIRE(writer->traces[0][1]->span_id == 420);
  }

  SECTION("can write a multi-span trace, even if the root finishes before a child") {
    auto rootSpan = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420, 420,
                                                   0, 123, 456, 0);
    buffer->registerSpan(context_from_span(*rootSpan));
    auto childSpan = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420,
                                                    421, 0, 124, 455, 0);
    buffer->registerSpan(context_from_span(*childSpan));
    buffer->finishSpan(std::move(rootSpan), sampler);
    buffer->finishSpan(std::move(childSpan), sampler);
    REQUIRE(writer->traces.size() == 1);
    REQUIRE(writer->traces[0].size() == 2);
    // Although order doesn't actually matter.
    REQUIRE(writer->traces[0][0]->span_id == 420);
    REQUIRE(writer->traces[0][1]->span_id == 421);
  }

  SECTION("doesn't write an unfinished trace") {
    auto rootSpan = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420, 420,
                                                   0, 123, 456, 0);
    buffer->registerSpan(context_from_span(*rootSpan));
    auto childSpan = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420,
                                                    421, 0, 124, 455, 0);
    buffer->registerSpan(context_from_span(*childSpan));
    buffer->finishSpan(std::move(childSpan), sampler);
    REQUIRE(writer->traces.size() == 0);  // rootSpan still outstanding
    auto childSpan2 = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420,
                                                     422, 0, 125, 457, 0);
    buffer->registerSpan(context_from_span(*childSpan2));
    buffer->finishSpan(std::move(rootSpan), sampler);
    // Root span finished, but *after* childSpan2 was registered, so childSpan2 still oustanding.
    REQUIRE(writer->traces.size() == 0);
    // Ok now we're done!
    buffer->finishSpan(std::move(childSpan2), sampler);
    REQUIRE(writer->traces.size() == 1);
    REQUIRE(writer->traces[0].size() == 3);
  }

  SECTION("discards spans written without a corresponding startSpan call") {
    // Redirect cerr, so the the terminal output doesn't imply failure.
    std::stringstream error_message;
    std::streambuf* stderr = std::cerr.rdbuf(error_message.rdbuf());

    SECTION("not even a trace") {
      auto rootSpan = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420,
                                                     420, 0, 123, 456, 0);
      buffer->finishSpan(std::move(rootSpan), sampler);
      REQUIRE(writer->traces.size() == 0);
    }
    SECTION("there's a trace but no startSpan call") {
      auto rootSpan = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420,
                                                     420, 0, 123, 456, 0);
      buffer->registerSpan(context_from_span(*rootSpan));
      auto childSpan = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420,
                                                      421, 0, 124, 455, 0);
      buffer->finishSpan(std::move(childSpan), sampler);
      buffer->finishSpan(std::move(rootSpan), sampler);
      REQUIRE(writer->traces.size() == 1);
      REQUIRE(writer->traces[0].size() == 1);  // Only rootSpan got written.
      REQUIRE(writer->traces[0][0]->span_id == 420);
    }

    std::cerr.rdbuf(stderr);  // Restore stderr.
  }

  SECTION("spans written after a trace is submitted just start a new trace") {
    auto rootSpan = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420, 420,
                                                   0, 123, 456, 0);
    buffer->registerSpan(context_from_span(*rootSpan));
    buffer->finishSpan(std::move(rootSpan), sampler);
    REQUIRE(writer->traces.size() == 1);
    auto childSpan = std::make_unique<TestSpanData>("type", "service", "resource", "name", 420,
                                                    421, 0, 123, 456, 0);
    buffer->registerSpan(context_from_span(*childSpan));
    buffer->finishSpan(std::move(childSpan), sampler);
    REQUIRE(writer->traces.size() == 2);
  }

  SECTION("thread safe") {
    std::vector<std::thread> trace_writers;
    // Buffer 5 traces at once.
    for (uint64_t trace_id = 10; trace_id <= 50; trace_id += 10) {
      trace_writers.emplace_back(
          [&](uint64_t trace_id) {
            // For each trace, buffer 5 spans at once.
            std::vector<std::thread> span_writers;
            for (uint64_t span_id = trace_id; span_id < trace_id + 5; span_id++) {
              span_writers.emplace_back(
                  [&](uint64_t span_id) {
                    auto span = std::make_unique<TestSpanData>(
                        "type", "service", "resource", "name", trace_id, span_id, 0, 123, 456, 0);
                    buffer->registerSpan(context_from_span(*span));
                  },
                  span_id);
            }
            // Wait for all spans to be registered before finishing them.
            for (std::thread& span_writer : span_writers) {
              span_writer.join();
            }
            span_writers.clear();
            for (uint64_t span_id = trace_id; span_id < trace_id + 5; span_id++) {
              span_writers.emplace_back(
                  [&](uint64_t span_id) {
                    auto span = std::make_unique<TestSpanData>(
                        "type", "service", "resource", "name", trace_id, span_id, 0, 123, 456, 0);
                    buffer->finishSpan(std::move(span), sampler);
                  },
                  span_id);
            }
            for (std::thread& span_writer : span_writers) {
              span_writer.join();
            }
          },
          trace_id);
    }
    for (std::thread& trace_writer : trace_writers) {
      trace_writer.join();
    }
    // Mostly we REQUIRE that this doesn't SIGABRT :D
    REQUIRE(writer->traces.size() == 5);
    for (int i = 0; i < 5; i++) {
      REQUIRE(writer->traces[i].size() == 5);
    }
  }
}
