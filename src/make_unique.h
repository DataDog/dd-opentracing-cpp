#ifndef DD_OPENTRACING_MAKE_UNIQUE_H
#define DD_OPENTRACING_MAKE_UNIQUE_H

// This component provides a C++11 implementation of the following overload of
// C++14's `std::make_unique`:
//
//     template< class T, class... Args >
//     unique_ptr<T> make_unique( Args&&... args );

#include <memory>

namespace datadog {
namespace opentracing {

// Construct an instance of the specified `Object` type using the specified
// forwarded `constructor_args`, and return a `unique_ptr` to that instance.
template <typename Object, class... Args>
std::unique_ptr<Object> make_unique(Args&&... constructor_args) {
  return std::unique_ptr<Object>(new Object(std::forward<Args>(constructor_args)...));
}

}  // namespace opentracing
}  // namespace datadog

#endif
