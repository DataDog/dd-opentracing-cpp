Configuring Trace Sampling
==========================
If instrumented services produce too high a volume of tracing data, then the
services can be configured to send tracing data for only a subset of processed
requests.  This is called trace sampling.

By default, the rate at which instrumented services sample traces is governed by
the Datadog Agent, which dynamically adjusts the sampling rates of its clients
in order to reach a [configured target number][1] of traces per second.

For fine-grained control over trace sampling, instrumented services can be
configured with _sampling rules_.  What follows is a description of how
trace sampling may be configured in the Datadog C++ tracing library.

Sampling Rules
--------------
It is the _first_ service in a trace (the "root service") that determines
whether the trace will be sent to Datadog.  Subsequent services in the trace
follow whichever decision was made by the root service.

The root service may define rules that assign different sampling rates to
different kinds of traces.  In these rules, traces are distinguished by the
"service" and "operation name" associated with the root span.  Typically, the
root span of a service is always assocated with the same "service" and
"operation name."  However, services acting as hosts to multiple services may
produce different "service" spans for different requests.

For example, consider the following array of rules:
```json
[
    {"service": "usersvc", "name": "healthcheck", "sample_rate": 0.0},
    {"service": "usersvc", "sample_rate": 0.5},
    {"service": "authsvc", "sample_rate": 1.0},
    {"sample_rate": 0.1}
]
```
These rules stipulate the following trace sampling behavior:

- `usersvc` requests whose operation name is `healthcheck` are never sampled.
- Other `usersvc` are sampled 50% of the time.
- `authsvc` requests are sampling 100% of the time.
- All other requests are sampled 10% of the time.

Note that this behavior is relevant only if the tracer being configured is the
_first_ in the trace.

When a trace is created, its root span is evaluated against each sampling rule
in order.  The first rule that matches determines the probability that the
trace will be sampled.  If no rule matches, then the trace is subject to the
sampling rates governed by the Datadog Agent, as explained above.

Sampling rules can be configured programmatically in `std::string
TracerOptions::sampling_rules` or via the environment variable
`DD_TRACE_SAMPLING_RULES`.  In either case, the rules are expressed as a JSON
array of objects.  Each object supports the following properties:
```
[{
    "service": <the root span's service name, or any if absent>,
    "name": <the root span's operation name, or any if absent>,
    "sample_rate": <the probability of sampling the trace, or 1.0 if absent>
}, ...]
```

`DD_TRACE_SAMPLE_RATE`
----------------------
Setting a (numeric) value for the `DD_TRACE_SAMPLE_RATE` environment variable
effectively appends a sampling rule to the tracer's array of sampling rules:
```
[
    ...,
    {"sample_rate": $DD_TRACE_SAMPLE_RATE
]
```
Now there is a sampling rule that matches _any_ trace, and so traces that do
not match an earlier sampling rule are subject to the configured sampling rate.

Note that using `DD_TRACE_SAMPLE_RATE` means that the Datadog Agent no longer
governs the sampling rate of any traces produced by the tracer.  The implicit
"catch-all" rule, with the configured sampling rate, always takes precedence
over the Agent-based fallback.

`double TracerOptions::sample_rate`
-----------------------------------
This configuration option has the same meaning as the `DD_TRACE_SAMPLE_RATE`
environment variable.  Note that the environment variable overrides the
`TracerOptions` field if both are specified. 

`DD_TRACE_RATE_LIMIT`
---------------------
Sampling rules (and, by extension, `DD_TRACE_SAMPLE_RATE`) specify the
_probability_ that a trace will be sampled, but they do not specify the maximum
number of traces that may be produced in a given time period.

`DD_TRACE_RATE_LIMIT` is the maximum number of traces, per second, that may be
sampled by the tracer on account of sampling rules or `DD_TRACE_SAMPLE_RATE`.
The limit applies globally across all applicable traces, i.e. there is not a
separate limit for each sampling rule.

`DD_TRACE_RATE_LIMIT` is a floating point number, but is ususally specified as an integer, e.g.
```shell
export DD_TRACE_RATE_LIMIT=200
```
for a limit of 200 traces per second.

If this limit is not configured, its default value is 100 traces per second.

`double TracerOptions::sampling_limit_per_second`
-------------------------------------------------
This configuration option has the same meaning as the `DD_TRACE_RATE_LIMIT`
environment variable.  Note that the environment variable overrides the
`TracerOptions` field if both are specified.

[1]: https://docs.datadoghq.com/tracing/trace_ingestion/mechanisms/?tab=environmentvariables#in-the-agent