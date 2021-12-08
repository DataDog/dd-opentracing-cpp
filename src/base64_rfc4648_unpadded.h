#ifndef DD_OPENTRACING_BASE64_RFC4648_UNPADDED_H
#define DD_OPENTRACING_BASE64_RFC4648_UNPADDED_H

// This component extends the `cppcodec` library by defining a base64 codec
// that does not use padding, but is otherwise compatible with RFC 4648.
//
// Example usage:
//
//     std::string base64_greeting = base64_rfc4648_unpadded::encode("hello");
//     std::string greeting = base64_rfc4648_unpadded::decode(base64_greeting);
//     assert(greeting == "hello");

#include <cppcodec/base64_rfc4648.hpp>
#include <cppcodec/detail/codec.hpp>
#include <cppcodec/detail/base64.hpp>

namespace datadog {
namespace opentracing {
    
class base64_rfc4648_unpadded_policy : public cppcodec::detail::base64_rfc4648 {
public:
    template <typename Codec> using codec_impl = cppcodec::detail::stream_codec<Codec, base64_rfc4648_unpadded_policy>;

    static CPPCODEC_ALWAYS_INLINE constexpr bool generates_padding() { return false; }
    static CPPCODEC_ALWAYS_INLINE constexpr bool requires_padding() { return false; }
    // Other `static` member functions are inherited from the base class.
};

using base64_rfc4648_unpadded = cppcodec::detail::codec<cppcodec::detail::base64<base64_rfc4648_unpadded_policy>>;

}  // namespace opentracing
}  // namespace datadog

#endif
