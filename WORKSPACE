workspace(name = "com_github_datadog_dd_opentracing_cpp")

git_repository(
    name = "io_opentracing_cpp",
    remote = "https://github.com/opentracing/opentracing-cpp",
    commit = "ac50154a7713877f877981c33c3375003b6ebfe1",
)

new_http_archive(
    name = "com_github_msgpack_msgpack_c",
    sha256 = "9859d44d336f9b023a79a3026bb6a558b2ea346107ab4eadba58236048650690",
    strip_prefix = "msgpack-3.0.1",
    urls = [
        "https://github.com/msgpack/msgpack-c/releases/download/cpp-3.0.1/msgpack-3.0.1.tar.gz",
    ],
    build_file = "bazel/external/msgpack.BUILD"
)
