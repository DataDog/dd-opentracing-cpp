#ifndef DD_OPENTRACING_OVERLOAD_H
#define DD_OPENTRACING_OVERLOAD_H

// This component provides a function template, `overload`, that returns an invokable object
// that contains an `operator()` overload corresponding to each of the arguments passed to
// `overload`.
//
// The intended usage of `overload` is to combine lambda expressions for use as
// a visitor to a variant.  It is convenient to be able to utilitize the
// closure capture syntax of lambda expressions, rather than having to write a
// dedicated visitor class.
//
// For example usage, see `pickSamplingRate` in `span_buffer.cpp`.

#include <utility>

namespace datadog {
namespace opentracing {

template <typename Func, typename... Funcs>
struct OverloadedInvokable : OverloadedInvokable<Func>, OverloadedInvokable<Funcs...> {
  OverloadedInvokable(Func func, Funcs... funcs)
      : OverloadedInvokable<Func>(func), OverloadedInvokable<Funcs...>(funcs...) {}

  using OverloadedInvokable<Func>::operator();
  using OverloadedInvokable<Funcs...>::operator();
};

template <typename Func>
struct OverloadedInvokable<Func> : Func {
  OverloadedInvokable(Func func) : Func(func) {}

  using Func::operator();
};

template <class... Funcs>
auto overload(Funcs&&... funcs) {
  return OverloadedInvokable<Funcs...>(std::forward<Funcs>(funcs)...);
}

}  // namespace opentracing
}  // namespace datadog

#endif
