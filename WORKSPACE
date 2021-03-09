workspace(name = "com_github_datadog_dd_opentracing_cpp")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "io_opentracing_cpp",
    sha256 = "5b170042da4d1c4c231df6594da120875429d5231e9baa5179822ee8d1054ac3",
    strip_prefix = "opentracing-cpp-1.6.0",
    urls = ["https://github.com/opentracing/opentracing-cpp/archive/v1.6.0.tar.gz"],
)

http_archive(
    name = "com_github_msgpack_msgpack_c",
    sha256 = "433cbcd741e1813db9ae4b2e192b83ac7b1d2dd7968a3e11470eacc6f4ab58d2",
    strip_prefix = "msgpack-3.2.1",
    urls = [
        "https://github.com/msgpack/msgpack-c/releases/download/cpp-3.2.1/msgpack-3.2.1.tar.gz",
    ],
    build_file = "@//:bazel/external/msgpack.BUILD"
)
