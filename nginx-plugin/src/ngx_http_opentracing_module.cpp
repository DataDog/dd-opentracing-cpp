// This is a modified version of
// ../nginx-opentracing/opentracing/src/ngx_http_opentracing_module.cpp
// The modification changes the NGINX module from using dlopen to dynamically load any OpenTracing
// shared plugin to statically referencing our own implementation.

#include "../nginx-opentracing/opentracing/src/load_tracer.h"
#include "../nginx-opentracing/opentracing/src/opentracing_conf.h"
#include "../nginx-opentracing/opentracing/src/opentracing_directive.h"
#include "../nginx-opentracing/opentracing/src/opentracing_handler.h"
#include "../nginx-opentracing/opentracing/src/opentracing_variable.h"
#include "../nginx-opentracing/opentracing/src/utility.h"

#include <opentracing/dynamic_load.h>
#include <opentracing/version.h>
#include "../../src/propagation.h"
#include "../../src/tracer_factory.h"

#include <cerrno>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <utility>

namespace datadog {
namespace opentracing {
class Tracer;
}  // namespace opentracing
}  // namespace datadog

//#############################################################################
// Starting here, this is an unmodified copy from NGINX-OpenTracing's
// ngx_http_opentracing_module.cpp
// From the same version as used in the git submodule ../nginx-opentracing
// Aside from this source file, everything else just compiles against the
// submodule's source.
// https://github.com/opentracing-contrib/nginx-opentracing
//#############################################################################

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

extern ngx_module_t ngx_http_opentracing_module;
}

using namespace ngx_opentracing;

//------------------------------------------------------------------------------
// kDefaultOpentracingTags
//------------------------------------------------------------------------------
const std::pair<ngx_str_t, ngx_str_t> default_opentracing_tags[] = {
    {ngx_string("component"), ngx_string("nginx")},
    {ngx_string("nginx.worker_pid"), ngx_string("$pid")},
    {ngx_string("peer.address"), ngx_string("$remote_addr:$remote_port")},
    {ngx_string("http.method"), ngx_string("$request_method")},
    {ngx_string("http.url"), ngx_string("$scheme://$http_host$request_uri")},
    {ngx_string("http.host"), ngx_string("$http_host")}};

//------------------------------------------------------------------------------
// opentracing_module_init
//------------------------------------------------------------------------------
static ngx_int_t opentracing_module_init(ngx_conf_t *cf) noexcept {
  auto core_main_config = static_cast<ngx_http_core_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module));
  auto main_conf = static_cast<opentracing_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_opentracing_module));

  // Add handlers to create tracing data.
  auto handler = static_cast<ngx_http_handler_pt *>(
      ngx_array_push(&core_main_config->phases[NGX_HTTP_PREACCESS_PHASE].handlers));
  if (handler == nullptr) return NGX_ERROR;
  *handler = on_enter_block;

  handler = static_cast<ngx_http_handler_pt *>(
      ngx_array_push(&core_main_config->phases[NGX_HTTP_LOG_PHASE].handlers));
  if (handler == nullptr) return NGX_ERROR;
  *handler = on_log_request;

  // Add default span tags.
  const auto num_default_tags =
      sizeof(default_opentracing_tags) / sizeof(default_opentracing_tags[0]);
  if (num_default_tags == 0) return NGX_OK;
  main_conf->tags = ngx_array_create(cf->pool, num_default_tags, sizeof(opentracing_tag_t));
  if (!main_conf->tags) return NGX_ERROR;
  for (const auto &tag : default_opentracing_tags)
    if (add_opentracing_tag(cf, main_conf->tags, tag.first, tag.second) != NGX_CONF_OK)
      return NGX_ERROR;
  return NGX_OK;
}

//------------------------------------------------------------------------------
// opentracing_exit_worker
//------------------------------------------------------------------------------
static void opentracing_exit_worker(ngx_cycle_t *cycle) noexcept {
  // Close the global tracer if it's set and release the reference so as to
  // ensure that any dynamically loaded tracer is destructed before the library
  // handle is closed.
  auto tracer = opentracing::Tracer::InitGlobal(nullptr);
  if (tracer != nullptr) {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cycle->log, 0, "closing opentracing tracer");
    tracer->Close();
    tracer.reset();
  }
}

//------------------------------------------------------------------------------
// create_opentracing_main_conf
//------------------------------------------------------------------------------
static void *create_opentracing_main_conf(ngx_conf_t *conf) noexcept {
  auto main_conf = static_cast<opentracing_main_conf_t *>(
      ngx_pcalloc(conf->pool, sizeof(opentracing_main_conf_t)));
  // Default initialize members.
  *main_conf = opentracing_main_conf_t();
  if (!main_conf) return nullptr;
  return main_conf;
}

//------------------------------------------------------------------------------
// create_opentracing_loc_conf
//------------------------------------------------------------------------------
static void *create_opentracing_loc_conf(ngx_conf_t *conf) noexcept {
  auto loc_conf = static_cast<opentracing_loc_conf_t *>(
      ngx_pcalloc(conf->pool, sizeof(opentracing_loc_conf_t)));
  if (!loc_conf) return nullptr;

  loc_conf->enable = NGX_CONF_UNSET;
  loc_conf->enable_locations = NGX_CONF_UNSET;
  loc_conf->trust_incoming_span = NGX_CONF_UNSET;

  return loc_conf;
}

//------------------------------------------------------------------------------
// merge_opentracing_loc_conf
//------------------------------------------------------------------------------
static char *merge_opentracing_loc_conf(ngx_conf_t *, void *parent, void *child) noexcept {
  auto prev = static_cast<opentracing_loc_conf_t *>(parent);
  auto conf = static_cast<opentracing_loc_conf_t *>(child);

  ngx_conf_merge_value(conf->enable, prev->enable, 0);
  ngx_conf_merge_value(conf->enable_locations, prev->enable_locations, 1);

  if (prev->operation_name_script.is_valid() && !conf->operation_name_script.is_valid())
    conf->operation_name_script = prev->operation_name_script;

  if (prev->loc_operation_name_script.is_valid() && !conf->loc_operation_name_script.is_valid())
    conf->loc_operation_name_script = prev->loc_operation_name_script;

  ngx_conf_merge_value(conf->trust_incoming_span, prev->trust_incoming_span, 1);

  // Create a new array that joins `prev->tags` and `conf->tags`. Since tags
  // are set consecutively and setting a tag with the same key as a previous
  // one overwrites it, we need to ensure that the tags in `conf->tags` come
  // after `prev->tags` so as to keep the value from the most specific
  // configuration.
  if (prev->tags && !conf->tags) {
    conf->tags = prev->tags;
  } else if (prev->tags && conf->tags) {
    std::swap(prev->tags, conf->tags);
    auto tags = static_cast<opentracing_tag_t *>(ngx_array_push_n(conf->tags, prev->tags->nelts));
    if (!tags) return static_cast<char *>(NGX_CONF_ERROR);
    auto prev_tags = static_cast<opentracing_tag_t *>(prev->tags->elts);
    for (size_t i = 0; i < prev->tags->nelts; ++i) tags[i] = prev_tags[i];
  }

  return NGX_CONF_OK;
}

//------------------------------------------------------------------------------
// opentracing_module_ctx
//------------------------------------------------------------------------------
static ngx_http_module_t opentracing_module_ctx = {
    add_variables,                /* preconfiguration */
    opentracing_module_init,      /* postconfiguration */
    create_opentracing_main_conf, /* create main configuration */
    nullptr,                      /* init main configuration */
    nullptr,                      /* create server configuration */
    nullptr,                      /* merge server configuration */
    create_opentracing_loc_conf,  /* create location configuration */
    merge_opentracing_loc_conf    /* merge location configuration */
};

//#############################################################################
// Above here, unmodified copy. Below here: modifications made to build an
// all-in-one Datadog NGINX tracer.
//#############################################################################

static ngx_int_t opentracing_init_worker(ngx_cycle_t *cycle) noexcept try {
  auto main_conf = static_cast<opentracing_main_conf_t *>(
      ngx_http_cycle_get_module_main_conf(cycle, ngx_http_opentracing_module));
  if (!main_conf || !main_conf->tracer_conf_file.data) {
    return NGX_OK;
  }
  datadog::opentracing::TracerFactory<datadog::opentracing::Tracer> tracer_factory{};

  // Construct a tracer
  errno = 0;
  std::string config_file = to_string(main_conf->tracer_conf_file);
  std::ifstream in{config_file};
  if (!in.good()) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, errno, "Failed to open tracer configuration file %s",
                  config_file.c_str());
    return NGX_ERROR;
  }

  std::string tracer_config{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
  if (!in.good()) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, errno, "Failed to read tracer configuration file %s",
                  config_file.c_str());
    return NGX_ERROR;
  }

  std::string error_message;
  auto tracer_maybe = tracer_factory.MakeTracer(tracer_config.c_str(), error_message);
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

char *configure_tracer(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept try {
  auto main_conf = static_cast<opentracing_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_opentracing_module));
  auto values = static_cast<ngx_str_t *>(cf->args->elts);
  main_conf->tracer_conf_file = values[1];
  // Presence of main_conf->tracer_library is checked elsewhere as an indication of a
  // successful load. The value here is meaningless, though, since we're not dlopening
  // a plugin, we're statically linked instead.
  main_conf->tracer_library = ngx_string("libdd_opentracing.a");

  // In order for span context propagation to work, the keys used by a tracer
  // need to be known ahead of time.
  // NGINX-OpenTracing normally uses the heuristic of sending a dummy trace, but we can
  // explicitly list them instead.
  main_conf->span_context_keys = ngx_array_create(cf->pool,
                                                  (sizeof(datadog::opentracing::headerWhitelist) /
                                                   sizeof(*datadog::opentracing::headerWhitelist)),
                                                  sizeof(opentracing::string_view));
  if (main_conf->span_context_keys == nullptr) {
    throw std::bad_alloc{};
  }

  for (auto key : datadog::opentracing::headerWhitelist) {
    auto element =
        static_cast<opentracing::string_view *>(ngx_array_push(main_conf->span_context_keys));
    *element = key;
  }

  return static_cast<char *>(NGX_CONF_OK);
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0, "configure_tracer failed: %s", e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}

//------------------------------------------------------------------------------
// opentracing_commands
//------------------------------------------------------------------------------
static ngx_command_t opentracing_commands[] = {
    // Unmodified below here.
    {ngx_string("opentracing"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_flag_slot, NGX_HTTP_LOC_CONF_OFFSET, offsetof(opentracing_loc_conf_t, enable),
     nullptr},
    {ngx_string("opentracing_trace_locations"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_flag_slot, NGX_HTTP_LOC_CONF_OFFSET,
     offsetof(opentracing_loc_conf_t, enable_locations), nullptr},
    {ngx_string("opentracing_propagate_context"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
     propagate_opentracing_context, NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr},
    {ngx_string("opentracing_fastcgi_propagate_context"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
     propagate_fastcgi_opentracing_context, NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr},
    {ngx_string("opentracing_operation_name"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
     set_opentracing_operation_name, NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr},
    {ngx_string("opentracing_location_operation_name"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
     set_opentracing_location_operation_name, NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr},
    {ngx_string("opentracing_trust_incoming_span"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_flag_slot, NGX_HTTP_LOC_CONF_OFFSET,
     offsetof(opentracing_loc_conf_t, trust_incoming_span), nullptr},
    {ngx_string("opentracing_tag"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE2,
     set_opentracing_tag, NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr},
    // Unmodified above here. Next entry is our custom configure directive.
    {ngx_string("opentracing_configure_tracer"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE1, configure_tracer,
     NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr},

    ngx_null_command};

// opentracing_init_worker and opentracing_commands are the only two values here that aren't
// directly supplied by the NGINX-OpenTracing submodule.
ngx_module_t ngx_http_opentracing_module = {NGX_MODULE_V1,
                                            &opentracing_module_ctx, /* module context */
                                            opentracing_commands,    /* module directives */
                                            NGX_HTTP_MODULE,         /* module type */
                                            nullptr,                 /* init master */
                                            nullptr,                 /* init module */
                                            opentracing_init_worker, /* init process */
                                            nullptr,                 /* init thread */
                                            nullptr,                 /* exit thread */
                                            opentracing_exit_worker, /* exit process */
                                            nullptr,                 /* exit master */
                                            NGX_MODULE_V1_PADDING};
