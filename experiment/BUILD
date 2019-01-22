load("@org_tensorflow//tensorflow/core:platform/default/build_config.bzl", "tf_proto_library_cc")

tf_proto_library_cc(
    name = "sample0_proto",
    srcs = ["sample.proto"],
    cc_api_version = 2,
    visibility = ["//visibility:public"],
)

proto_library(
    name = "sample1_proto",
    srcs = [],
    deps = ["@com_google_protobuf//:descriptor_proto"],
)

cc_proto_library(
    name = "sample1_proto_cc",
    deps = [":sample1_proto"],
)

cc_binary(
    name = "sample0.so",
    srcs = [],
    deps = [":sample0_proto_cc"],
    linkshared = 1,
    linkstatic = 1,
)

cc_binary(
    name = "sample1.so",
    srcs = [],
    deps = [":sample1_proto_cc"],
    linkshared = 1,
    linkstatic = 1,
)

cc_binary(
    name = "sample2.so",
    srcs = [],
    deps = [":sample0_proto_cc", ":sample1_proto_cc"],
    linkshared = 1,
    linkstatic = 1,
)

