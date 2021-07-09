#ifndef DD_OPENTRACING_TEST_CATCH_H
#define DD_OPENTRACING_TEST_CATCH_H

#include <cassert>
#include <catch2/catch.hpp>
#include <cstdlib>
#include <type_traits>

// GCC 4.8 wrongfully claims that:
//
//     error: 'const T& Catch::Generators::IGenerator<T>::get() const [with T = ...]', declared
//     using local type 'const ...', is used but never defined [-fpermissive]
//              virtual T const& get() const = 0;
//
// In order to support that compiler, here is an _unused_ definition for that
// template.
namespace Catch {
namespace Generators {

template <typename T>
T const& IGenerator<T>::get() const {
  assert(!"Not implemented");
  std::abort();
  return *static_cast<typename std::remove_reference<T>::type const*>(nullptr);
}

}  // namespace Generators
}  // namespace Catch

#endif
