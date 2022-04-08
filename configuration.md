Configuration
=============
The behavior of the Datadog tracer is determined by its configuration.

Configuration can be specified in multiple ways:

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
The name of the host at which the Datadog Agent can be contacted.

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
specified at which the Datadog Agent can be contacted.  This also allows for
other address schemes, such as `unix` for unix domain sockets.  For more
information, see the code comments near the corresponding member of
`TracerOptions` in [datadog/opentracing.h][1].

- **TracerOptions member**: `std::string agent_url`
- **JSON property**: `"agent_url"` _(string)_
- **Environment variable**: `DD_TRACE_AGENT_URL`
- **Default value**: `""`

### Service Name
The default service name to associate with spans produced by the tracer.
Service name can be overridden programmatically on a per-span basis via the
`datadog::opentracing::Span::setServiceName` member function.

- **TracerOptions member**: `std::string service`
- **JSON property**: `"service"` _(string)_
- **Environment variable**: `DD_SERVICE`
- **Required**

### Service Type
The default "service type" to associate with spans produced by the tracer.  The
documentation that describes the meaning and purpose of "service type" has been
removed from the internet.

- **TracerOptions member**: `std::string type`
- **JSON property**: `"type"` _(string)_
- **Default value**: `"web"`

### Environment
The default release environment in which the service is running, e.g. "staging"
or "prod".

- **TracerOptions member**: `std::string environment`
- **JSON property**: `"environment"` _(string)_
- **Environment variable**: `DD_ENV`
- **Default value**: `""`

### Sampling Rate
The default probability that a trace beginning with this tracer will be sampled
for ingestion.  For more information, see the code comments near the
corresponding member of `TracerOptions` in [datadog/opentracing.h][1].

- **TracerOptions member**: `double sample_rate`
- **JSON property**: `"sample_rate"` _(number)_
- **Environment variable**: `DD_TRACE_SAMPLE_RATE`

### Sampling Rules
Sampling rules allow for finer-grained control over the rate at which traces
beginning at this tracer will be sampled for ingestion.  Sampling rules are
specified as a JSON array of objects.  For more information, see the code
comments near the corresponding member of `TracerOptions` in
[datadog/opentracing.h][1].

- **TracerOptions member**: `std::string sampling_rules`
- **JSON property**: `"sampling_rules"` _(array of objects)_
- **Environment variable**: `DD_TRACE_SAMPLING_RULES` _(JSON)_
- **Default value**: `[]`

### Trace Flushing Period
How often a batch of finished traces is sent to the Datadog Agent.

- **TracerOptions member**: `int64_t write_period_ms` _(milliseconds)_
- **Default value**: `1000` _(milliseconds)_

### Operation Name
TODO

- **TracerOptions member**: `std::string operation_name_override`
- **JSON property**: `"operation_name_override"` _(string)_
- **Default value**: `""`

### Trace Context Extraction Styles
TODO

- **TracerOptions member**: `std::set<PropagationStyle> extract`
- **JSON property**: `"propagation_style_extract"` _(array of string)_
- **Environment variable**: `DD_PROPAGATION_STYLE_EXTRACT` _(JSON)_
- **Default value**: `["Datadog"]`

### Trace Context Injection Styles
TODO

- **TracerOptions member**: `std::set<PropagationStyle> inject`
- **JSON property**: `"propagation_style_inject"` _(array of string)_
- **Environment variable**: `DD_PROPAGATION_STYLE_INJECT` _(JSON)_
- **Default value**: `["Datadog"]`

### Host Name Reporting
TODO

- **TracerOptions member**: `bool report_hostname`
- **JSON property**: `"dd.trace.report-hostname"` _(boolean)_
- **Environment variable**: `DD_TRACE_REPORT_HOSTNAME`
- **Default value**: `false`

### Span Tags
TODO

- **TracerOptions member**: `std::map<std::string, std::string> tags`
- **JSON property**: `tags` _(object)_
- **Environment variable**: `DD_TAGS` _(format: `"name:value,name:value,..."`)_
- **Default value**: `{}`

### Application Version
TODO

- **TracerOptions member**: `std::string version`
- **JSON property**: `version` _(string)_
- **Environment variable**: `DD_VERSION`
- **Default value**: `""`

### Logging Function
TODO

- **TracerOptions member**: `std::function<void(LogLevel, opentracing::string_view)> log_func`
- **Default value**: _(prints to `std::cerr`)_

### Limit Traces Sampled Per Second
TODO

- **TracerOptions member**: `double sampling_limit_per_second`
- **JSON property**: `sampling_limit_per_second` _(number)_
- **Environment variable**: `DD_TRACE_RATE_LIMIT`
- **Default value**: `100`

[1]: include/datadog/opentracing.h
[2]: https://docs.datadoghq.com/tracing/setup_overview/proxy_setup/?tab=nginx#nginx-configuration
[3]: https://docs.datadoghq.com/tracing/setup_overview/setup/cpp/?tab=containers#dynamic-loading
[4]: https://docs.datadoghq.com/tracing/setup_overview/setup/cpp/?tab=containers#environment-variables
