Configuration
=============
The Datadog tracer's configuration can be specified in multiple ways:

- programmatically in C++ code via the `TracerOptions` object defined in
  [datadog/opentracing.h][1],
- process-wide via setting values for certain [environment variables][4],
- [dynamically][3] by reading JSON-formatted text, as in done in the [nginx
  plugin][2].

Most options support all three methods of configuration.

Environment variables override any corresponding configuration in
`TracerOptions` or loaded from JSON.

Options
-------
### Agent Host
The name of the host at which the Datadog Agent can be contacted, or the host's
IP address.

- **TracerOptions member**: `std::string agent_host`
- **JSON property**: `"agent_host"` _string_
- **Environment variable**: `DD_AGENT_HOST`
- **Default value**: `"localhost"`

### Agent Port
The port on which the Datadog Agent is listening.

- **TracerOptions member**: `uint32_t agent_port`
- **JSON property**: `"agent_port"` _integer_
- **Environment variable**: `DD_TRACE_AGENT_PORT`
- **Default value**: `8126`

### Agent URL
As an alternative to specifying a host and port separately, a URL may be
specified indicating where the Datadog Agent can be contacted.  Both TCP
and Unix domain sockets are supported.  For more information about using a
Unix domain socket, see the [relevant example][5].

If the Agent URL is specified, then it overrides the Agent host and Agent port
settings.

The following forms are supported:

- `http://host` (TCP)
- `http://host:port` (TCP)
- `https://host` (TCP)
- `https://host:port` (TCP)
- `unix://path` (Unix domain socket)
- `path` (Unix domain socket)

- **TracerOptions member**: `std::string agent_url`
- **JSON property**: `"agent_url"` _(string)_
- **Environment variable**: `DD_TRACE_AGENT_URL`
- **Default value**: `""`

### Service Name
The default service name to associate with spans produced by the tracer.
Service name can be overridden programmatically on a per-span basis by setting
a value for the `datadog::tags::service_name` tag.

- **TracerOptions member**: `std::string service`
- **JSON property**: `"service"` _(string)_
- **Environment variable**: `DD_SERVICE`
- **Required**

### Service Type
The default "service type" to associate with spans produced by the tracer.

Service type is used in multiple places throughout Datadog to distinguish
different categories of instrumented service from each other.  For example,
it is used in the following ways:

- to identify whether the service's spans need to be obfuscated
- to control display of the service in the Datadog UI.

Example values for service type are `web`, `db`, and `lambda`.

- **TracerOptions member**: `std::string type`
- **JSON property**: `"type"` _(string)_
- **Default value**: `"web"`

### Environment
The default release environment in which the service is running, e.g. "prod,"
"dev," or "staging."

Environment is one of the core properties associated with a service, together
with its name and version.  See [Unified Service Tagging][9].

- **TracerOptions member**: `std::string environment`
- **JSON property**: `"environment"` _(string)_
- **Environment variable**: `DD_ENV`
- **Default value**: `""`

### Sample Rate
The default probability that a trace beginning at this tracer will be sampled
for ingestion.
  
For more information about the configuration of trace sampling, see
[sampling.md][6].

- **TracerOptions member**: `double sample_rate`
- **JSON property**: `"sample_rate"` _(number)_
- **Environment variable**: `DD_TRACE_SAMPLE_RATE`

### Sampling Rules
Sampling rules allow for fine-grained control over the rate at which traces
beginning at this tracer will be sampled for ingestion.  Sampling rules are
specified as a JSON array of objects.

For more information about the configuration of trace sampling, see
[sampling.md][6].

- **TracerOptions member**: `std::string sampling_rules` _(JSON)_
- **JSON property**: `"sampling_rules"` _(array of objects)_
- **Environment variable**: `DD_TRACE_SAMPLING_RULES` _(JSON)_
- **Default value**: `[]`

### Trace Flushing Period
How often a batch of finished traces is sent to the Datadog Agent.

- **TracerOptions member**: `int64_t write_period_ms` _(milliseconds)_
- **Default value**: `1000` _(milliseconds)_

### Operation Name
The default operation name to associate with spans produced by the tracer.

A span's operation name (sometimes just called "name" or "operation") indicates
which of a service's functions the span represents.

Operation name is often fixed for a given service, e.g. the "nginx" service
entry spans might always have operation name "handle.request".

Operation name is not to be confused with a span's associated resource, also
known as endpoint.  Resource (endpoint) contains information about the
particular request, whereas operation name is more like a subcategory of the
service name.

- **TracerOptions member**: `std::string operation_name_override`
- **JSON property**: `"operation_name_override"` _(string)_
- **Default value**: `""`

### Trace Context Extraction Styles
When one service calls another along a distributed trace, information about the
trace must be propagated in the call; information such as the trace ID, the
parent span ID, and the sampling decision.

Different tracing systems have different standards for how trace context is
propagated, e.g. which HTTP request headers are used.

The Datadog C++ tracer supports two styles of trace context propagation.  The
default style, `Datadog`, decodes trace information from multiple `X-Datadog-*`
request headers.  For compatibility with [other tracing systems][7], another
style, `B3`, is also supported.  The `B3` style decodes trace information from
multiple `X-B3-*` request headers.

The trace context extraction styles setting indicates which styles the tracer
will consider when extracting trace context from a request.  At least one style
must be specified, but multiple may be specified.  If multiple styles are
specified, then trace context must be successfully extractable in at least one
of the styles, and if trace context can be extracted in both styles, the two
extracted contexts must agree.

- **TracerOptions member**: `std::set<PropagationStyle> extract`
- **JSON property**: `"propagation_style_extract"` _(array of string)_
- **Environment variable**: `DD_PROPAGATION_STYLE_EXTRACT` _(space or comma separated symbols)_
- **Default value**: `["Datadog"]`

### Trace Context Injection Styles
Trace context injection styles are analogous to trace context extraction styles
(see the previous section), except that rather than indicating which trace
context encoding are supported when _extracting_ trace context, trace context
injection styles indicate which trace context encoding(s) will be used when
_injecting_ context into a request to the next service along a trace.

Note that even if the `B3` injection style is used, the tracer still may inject
Datadog-specific trace context, such as in the `X-Datadog-Origin` request
header.

- **TracerOptions member**: `std::set<PropagationStyle> inject`
- **JSON property**: `"propagation_style_inject"` _(array of string)_
- **Environment variable**: `DD_PROPAGATION_STYLE_INJECT` _(space or comma separated symbols)_
- **Default value**: `["Datadog"]`

### Host Name Reporting
If `true`, the tracer will look up its host's name on the network using the
[gethostname][8] function and send it to the Datadog backend in a reserved span
tag.

- **TracerOptions member**: `bool report_hostname`
- **JSON property**: `"dd.trace.report-hostname"` _(boolean)_
- **Environment variable**: `DD_TRACE_REPORT_HOSTNAME`
- **Default value**: `false`

### Span Tags
Tags to add to every span produced by the tracer.

When specified as `std::map<std::string, std::string> tags`, each entry in the
map is a (key, value) pair, where the key is the name of the span tag, and the
value is its value.  The value is a string.

When specified as the `DD_TAGS` environment variable, tags are formatted as a
comma-separated list of `key:value` pairs (the key and value are separated by a
colon).

- **TracerOptions member**: `std::map<std::string, std::string> tags`
- **JSON property**: `tags` _(object)_
- **Environment variable**: `DD_TAGS` _(format: `"name:value,name:value,..."`)_
- **Default value**: `{}`

### Application Version
The version of the application that is being instrumented.

If set, the application version is sent to the Datadog backend as the `version`
tag on the first span that the tracer produces in every trace.

- **TracerOptions member**: `std::string version`
- **JSON property**: `version` _(string)_
- **Environment variable**: `DD_VERSION`
- **Default value**: `""`

### Logging Function
The function used by the library to log diagnostics.

The provided function takes two arguments:

- `LogLevel level` is the severity of the diagnostic: `debug`, `info`, or
  `error`.
- `::opentracing::string_view message` is the diagnostic message itself. 

- **TracerOptions member**: `std::function<void(LogLevel, ::opentracing::string_view)> log_func`
- **Default value**: _(prints to `std::cerr`)_

### Limit Traces Sampled Per Second
The maximum number of traces per second that may be sampled on account of
either sampling rules or `DD_TRACE_SAMPLE_RATE`.

For more information about the configuration of trace sampling, see
[sampling.md][6].

- **TracerOptions member**: `double sampling_limit_per_second`
- **JSON property**: `sampling_limit_per_second` _(number)_
- **Environment variable**: `DD_TRACE_RATE_LIMIT`
- **Default value**: `100`

### Trace Tags Propagation Max Length
Certain information, such as the _reason_ for a sampling decision having been
made, is propagated between services along the trace in the form of the
`X-Datadog-Tags` HTTP request header.

`X-Datadog-Tags`'s length is limited to a certain maximum number of bytes in
order to prevent rejection by peers or other HTTP header policies.  This
configuration option is that limit, in bytes.

### Span Sampling Rules
When a trace is dropped, it may still be advantageous to send some of its spans
to Datadog.  For example, if there are [user-defined metrics derived from
spans][10], those metrics account only for spans sent to Datadog.  Also, trace
search queries exclude spans not sent to Datadog.  In these cases, a subset of
the spans in the dropped trace can be nonetheless kept by defining span
sampling rules.

For more information about the configuration of span sampling, see the [Span
Sampling][11] section of [sampling.md][6].

- **TracerOptions member**: `std::string span_sampling_rules` _(JSON)_
- **JSON property**: `"span_sampling_rules"` _(array of objects)_
- **Environment variable**: `DD_SPAN_SAMPLING_RULES` _(JSON)_
- **Default value**: `[]`

### Span Sampling Rules File
Span sampling rules (see above) can be specified in their own file.

- **Environment variable**: `DD_SPAN_SAMPLING_RULES_FILE`

Note that `DD_SPAN_SAMPLING_RULES_FILE` is ignored when
`DD_SPAN_SAMPLING_RULES` is also in the environment.

- **TracerOptions member**: `uint64_t tags_header_size`
- **JSON property**: `tags_header_size` _(number)_
- **Environment variable**: `DD_TRACE_TAGS_PROPAGATION_MAX_LENGTH`
- **Default value**: `512`

[1]: /include/datadog/opentracing.h
[2]: https://docs.datadoghq.com/tracing/setup_overview/proxy_setup/?tab=nginx#nginx-configuration
[3]: https://docs.datadoghq.com/tracing/setup_overview/setup/cpp/?tab=containers#dynamic-loading
[4]: https://docs.datadoghq.com/tracing/setup_overview/setup/cpp/?tab=containers#environment-variables
[5]: /examples/cpp-tracing/unix-domain-socket
[6]: sampling.md
[7]: https://github.com/openzipkin/b3-propagation
[8]: https://pubs.opengroup.org/onlinepubs/9699919799/
[9]: https://docs.datadoghq.com/getting_started/tagging/unified_service_tagging
[10]: https://docs.datadoghq.com/tracing/generate_metrics/
[11]: sampling.md#span-sampling
