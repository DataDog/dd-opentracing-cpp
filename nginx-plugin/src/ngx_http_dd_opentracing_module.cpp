#include "../nginx-opentracing/opentracing/src/load_tracer.h"
#include "../nginx-opentracing/opentracing/src/opentracing_conf.h"
#include "../nginx-opentracing/opentracing/src/opentracing_directive.h"
#include "../nginx-opentracing/opentracing/src/opentracing_handler.h"
#include "../nginx-opentracing/opentracing/src/opentracing_variable.h"
#include "../nginx-opentracing/opentracing/src/utility.h"

#include <datadog/opentracing.h>
#include <opentracing/dynamic_load.h>
#include <opentracing/version.h>

#include <cerrno>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <utility>

namespace inner {
#include "../nginx-opentracing/opentracing/src/ngx_http_opentracing_module.cpp"
}

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

extern ngx_module_t ngx_http_dd_opentracing_module;
}

using namespace ngx_opentracing;

char *configure_tracer(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept try {
  auto main_conf = static_cast<opentracing_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_dd_opentracing_module));
  auto values = static_cast<ngx_str_t *>(cf->args->elts);
  main_conf->tracer_conf_file = values[1];

  // // In order for span context propagation to work, the keys used by a tracer
  // // need to be known ahead of time. OpenTracing-C++ doesn't currently have any
  // // API for this, so we attempt to do this by creating and injecting a dummy
  // // span context.
  // //
  // // See also propagate_opentracing_context.
  // main_conf->span_context_keys =
  //     discover_span_context_keys(cf->pool, cf->log,
  //     to_string(main_conf->tracer_library).c_str(),
  //                                to_string(main_conf->tracer_conf_file).c_str()); //fixme
  // if (main_conf->span_context_keys == nullptr) {
  //   return static_cast<char *>(NGX_CONF_ERROR);
  // }

  return static_cast<char *>(NGX_CONF_OK);
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0, "configure_tracer failed: %s", e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}

static ngx_int_t opentracing_init_worker(ngx_cycle_t *cycle) noexcept try {
  auto main_conf = static_cast<opentracing_main_conf_t *>(
      ngx_http_cycle_get_module_main_conf(cycle, ngx_http_dd_opentracing_module));
  if (!main_conf || !main_conf->tracer_conf_file.data) {
    return NGX_OK;
  }

  const void *error_category = nullptr;
  void *tracer_factory = nullptr;  // DELETEME
  std::string error_message;
  const auto rcode = datadog::opentracing::OpenTracingMakeTracerFactoryFunction(
      OPENTRACING_VERSION, OPENTRACING_ABI_VERSION, &error_category,
      static_cast<void *>(&error_message), &tracer_factory);
  if (rcode != 0) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Failed to load construct tracer: %s ",
                  error_message.c_str());
    return NGX_ERROR;
  }

  // Construct a tracer
  errno = 0;
  const char *config_file = to_string(main_conf->tracer_conf_file).data();
  std::ifstream in{config_file};
  if (!in.good()) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, errno, "Failed to open tracer configuration file %s",
                  config_file);
    return NGX_ERROR;
  }
  std::string tracer_config{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
  if (!in.good()) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, errno, "Failed to read tracer configuration file %s",
                  &config_file);
    return NGX_ERROR;
  }

  auto tracer_maybe = static_cast<opentracing::TracerFactory *>(tracer_factory)
                          ->MakeTracer(tracer_config.c_str(), error_message);
  if (!tracer_maybe) {
    if (!error_message.empty()) {
      ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Failed to construct tracer: %s",
                    error_message.c_str());
    } else {
      ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Failed to construct tracer: %s",
                    tracer_maybe.error().message().c_str());
    }
    return NGX_ERROR;
  }

  opentracing::Tracer::InitGlobal(std::move(*tracer_maybe));
  return NGX_OK;
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "failed to initialize tracer: %s", e.what());
  return NGX_ERROR;
}

static ngx_command_t opentracing_commands[] = {
    inner::opentracing_commands[0],
    inner::opentracing_commands[1],
    inner::opentracing_commands[2],
    inner::opentracing_commands[3],
    inner::opentracing_commands[4],
    inner::opentracing_commands[5],
    inner::opentracing_commands[6],
    inner::opentracing_commands[7],
    {ngx_string("opentracing_configure_tracer"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE1, configure_tracer,
     NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr},
    ngx_null_command};

ngx_module_t ngx_http_dd_opentracing_module = {NGX_MODULE_V1,
                                               &inner::opentracing_module_ctx, /* module context */
                                               opentracing_commands,    /* module directives */
                                               NGX_HTTP_MODULE,         /* module type */
                                               nullptr,                 /* init master */
                                               nullptr,                 /* init module */
                                               opentracing_init_worker, /* init process */
                                               nullptr,                 /* init thread */
                                               nullptr,                 /* exit thread */
                                               inner::opentracing_exit_worker, /* exit process */
                                               nullptr,                        /* exit master */
                                               NGX_MODULE_V1_PADDING};
