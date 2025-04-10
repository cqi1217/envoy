load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "http1_bridge_filter_test",
    srcs = ["http1_bridge_filter_test.cc"],
    extension_names = ["envoy.filters.http.grpc_http1_bridge"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/grpc_http1_bridge:config",
        "//source/extensions/filters/http/grpc_http1_bridge:http1_bridge_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:global_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.grpc_http1_bridge"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/grpc_http1_bridge:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "grpc_http1_bridge_integration_test",
    size = "large",
    srcs = [
        "grpc_http1_bridge_integration_test.cc",
    ],
    extension_names = ["envoy.filters.http.grpc_http1_bridge"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/http/grpc_http1_bridge:config",
        "//source/extensions/filters/http/health_check:config",
        "//test/integration:http_integration_lib",
    ],
)
