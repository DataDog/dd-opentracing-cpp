cc_library(
    name = "nlohmann",
    hdrs = ["include/nlohmann/json.hpp"],
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
)

cc_library(
    name = "catch2",
    hdrs = ["include/catch2/catch.hpp"],
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
)

filegroup(
    name = "sanitizers_cmake",
    srcs = glob([
        "sanitizers-cmake/*",
    ]),
    visibility = ["//visibility:public"],
)
