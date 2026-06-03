cmake_minimum_required(VERSION 3.20)
project(filegroup VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC")

# Optional: hardware CRC32C support
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-msse4.2" COMPILER_SUPPORTS_SSE42)
if(COMPILER_SUPPORTS_SSE42)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2")
endif()

# Find required packages
find_package(OpenSSL REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(GTest QUIET)

# Common library (Phase 1 foundation)
add_library(filegroup_common OBJECT
    src/common/crc32c.cc
    src/common/clock.cc
)
target_include_directories(filegroup_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Config library
add_library(filegroup_config OBJECT
    src/config/config.cc
    src/config/config_resolver.cc
)
target_include_directories(filegroup_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_config PRIVATE filegroup_common)

# File header library
add_library(filegroup_file_header OBJECT
    src/file_header/file_header.cc
)
target_include_directories(filegroup_file_header PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_file_header PRIVATE filegroup_common)

# Manifest library
add_library(filegroup_manifest OBJECT
    src/manifest/manifest_writer.cc
    src/manifest/manifest_reader.cc
    src/manifest/file_index.cc
)
target_include_directories(filegroup_manifest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_manifest PRIVATE filegroup_common)

# Raft library
add_library(filegroup_raft OBJECT
    src/raft/raft.cc
)
target_include_directories(filegroup_raft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_raft PRIVATE filegroup_common)

# Segment library
add_library(filegroup_segment OBJECT
    src/segment/segment_writer.cc
    src/segment/segment_reader.cc
)
target_include_directories(filegroup_segment PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_segment PRIVATE filegroup_common)

# Distribution library
add_library(filegroup_distribution OBJECT
    src/distribution/distribution.cc
)
target_include_directories(filegroup_distribution PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_distribution PRIVATE filegroup_common)

# Encryption library
add_library(filegroup_encryption OBJECT
    src/encryption/encryption.cc
)
target_include_directories(filegroup_encryption PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_encryption PRIVATE filegroup_common OpenSSL::Crypto)

# Compaction library
add_library(filegroup_compaction OBJECT
    src/compaction/compaction.cc
)
target_include_directories(filegroup_compaction PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_compaction PRIVATE filegroup_common filegroup_segment)

# Registry Service library
add_library(filegroup_registry OBJECT
    src/registry/registry_service.cc
)
target_include_directories(filegroup_registry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_registry PRIVATE filegroup_common filegroup_raft filegroup_manifest filegroup_config filegroup_file_header filegroup_encryption filegroup_segment)

# Storage Node Service library
add_library(filegroup_storage OBJECT
    src/storage/storage_node_service.cc
)
target_include_directories(filegroup_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_storage PRIVATE filegroup_common filegroup_segment)

# Upload Protocol library
add_library(filegroup_upload OBJECT
    src/upload/upload_protocol.cc
)
target_include_directories(filegroup_upload PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_upload PRIVATE filegroup_common filegroup_distribution filegroup_encryption filegroup_storage)

# Read Protocol library
add_library(filegroup_read OBJECT
    src/read/read_protocol.cc
)
target_include_directories(filegroup_read PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_read PRIVATE filegroup_common filegroup_storage)

# Versioning Service library
add_library(filegroup_versioning OBJECT
    src/versioning/versioning_service.cc
)
target_include_directories(filegroup_versioning PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_versioning PRIVATE filegroup_common)

# Heartbeat Service library
add_library(filegroup_heartbeat OBJECT
    src/heartbeat/heartbeat_service.cc
)
target_include_directories(filegroup_heartbeat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_heartbeat PRIVATE filegroup_common)

# Engine Recovery library
add_library(filegroup_recovery OBJECT
    src/recovery/engine_recovery.cc
    src/recovery/storage_recovery.cc
)
target_include_directories(filegroup_recovery PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_recovery PRIVATE filegroup_common)

# Scrubber Service library
add_library(filegroup_scrubber OBJECT
    src/scrubber/scrubber_service.cc
)
target_include_directories(filegroup_scrubber PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_scrubber PRIVATE filegroup_common)

# Expiry Service library
add_library(filegroup_expiry OBJECT
    src/expiry/expiry_service.cc
)
target_include_directories(filegroup_expiry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_expiry PRIVATE filegroup_common)

# Engine Service library
add_library(filegroup_engine OBJECT
    src/engine/engine.cc
    src/engine/engine_service.cc
)
target_include_directories(filegroup_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_engine PRIVATE
    filegroup_common filegroup_config filegroup_raft filegroup_registry
    filegroup_storage filegroup_segment filegroup_distribution
    filegroup_encryption filegroup_manifest filegroup_file_header)

# Metrics library
add_library(filegroup_metrics OBJECT
    src/metrics/metrics.cc
)
target_include_directories(filegroup_metrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# TLS library
add_library(filegroup_tls OBJECT
    src/tls/tls_init.cc
)
target_include_directories(filegroup_tls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(filegroup_tls PRIVATE OpenSSL::SSL OpenSSL::Crypto)

# dbctl CLI tool
add_executable(dbctl
    src/cli/dbctl.cc
    $<TARGET_OBJECTS:filegroup_common>
    $<TARGET_OBJECTS:filegroup_config>
    $<TARGET_OBJECTS:filegroup_file_header>
    $<TARGET_OBJECTS:filegroup_manifest>
    $<TARGET_OBJECTS:filegroup_raft>
    $<TARGET_OBJECTS:filegroup_segment>
    $<TARGET_OBJECTS:filegroup_distribution>
    $<TARGET_OBJECTS:filegroup_encryption>
    $<TARGET_OBJECTS:filegroup_registry>
    $<TARGET_OBJECTS:filegroup_storage>
    $<TARGET_OBJECTS:filegroup_engine>
    $<TARGET_OBJECTS:filegroup_metrics>
    $<TARGET_OBJECTS:filegroup_tls>
)
target_include_directories(dbctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(dbctl PRIVATE OpenSSL::SSL OpenSSL::Crypto)

# Main executable — gRPC server (wraps EngineServer, exposed on :8443)

# Compile engine.proto → C++
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

set(PROTO_FILE "${PROTO_DIR}/engine.proto")
set(PROTO_PB_CC  "${PROTO_GEN_DIR}/engine.pb.cc")
set(PROTO_PB_H   "${PROTO_GEN_DIR}/engine.pb.h")
set(PROTO_GRPC_CC "${PROTO_GEN_DIR}/engine.grpc.pb.cc")
set(PROTO_GRPC_H  "${PROTO_GEN_DIR}/engine.grpc.pb.h")

add_custom_command(
    OUTPUT ${PROTO_PB_CC} ${PROTO_PB_H} ${PROTO_GRPC_CC} ${PROTO_GRPC_H}
    COMMAND protobuf::protoc
        --proto_path=${PROTO_DIR}
        --cpp_out=${PROTO_GEN_DIR}
        --grpc_out=${PROTO_GEN_DIR}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating gRPC code from engine.proto"
)

set(PROTO_GEN_SRCS ${PROTO_PB_CC} ${PROTO_GRPC_CC})
set(PROTO_GEN_HDRS ${PROTO_PB_H} ${PROTO_GRPC_H})

# gRPC server binary
add_executable(engine_grpc
    src/main.cc
    ${PROTO_GEN_SRCS}
    $<TARGET_OBJECTS:filegroup_common>
    $<TARGET_OBJECTS:filegroup_config>
    $<TARGET_OBJECTS:filegroup_file_header>
    $<TARGET_OBJECTS:filegroup_manifest>
    $<TARGET_OBJECTS:filegroup_raft>
    $<TARGET_OBJECTS:filegroup_segment>
    $<TARGET_OBJECTS:filegroup_distribution>
    $<TARGET_OBJECTS:filegroup_encryption>
    $<TARGET_OBJECTS:filegroup_compaction>
    $<TARGET_OBJECTS:filegroup_registry>
    $<TARGET_OBJECTS:filegroup_storage>
    $<TARGET_OBJECTS:filegroup_engine>
)
target_include_directories(engine_grpc PRIVATE ${PROTO_GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(engine_grpc PRIVATE
    OpenSSL::SSL OpenSSL::Crypto
    protobuf::libprotobuf
    gRPC::grpc++
)

# Unit tests
if(GTest_FOUND)
    enable_testing()
    
    add_executable(test_crc32c tests/unit/test_crc32c.cc
        $<TARGET_OBJECTS:filegroup_common>
    )
    target_include_directories(test_crc32c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_crc32c PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME CRC32C COMMAND test_crc32c)

    add_executable(test_config_resolver tests/unit/test_config_resolver.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_config>
    )
    target_include_directories(test_config_resolver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_config_resolver PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME ConfigResolver COMMAND test_config_resolver)

    add_executable(test_file_header tests/unit/test_file_header.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_file_header>
    )
    target_include_directories(test_file_header PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_file_header PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME FileHeader COMMAND test_file_header)

    add_executable(test_manifest tests/unit/test_manifest.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_manifest>
    )
    target_include_directories(test_manifest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_manifest PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME Manifest COMMAND test_manifest)

    add_executable(test_file_index tests/unit/test_file_index.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_manifest>
    )
    target_include_directories(test_file_index PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_file_index PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME FileIndex COMMAND test_file_index)

    add_executable(test_raft tests/unit/test_raft.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_raft>
        $<TARGET_OBJECTS:filegroup_manifest>
        $<TARGET_OBJECTS:filegroup_config>
        $<TARGET_OBJECTS:filegroup_file_header>
        $<TARGET_OBJECTS:filegroup_segment>
        $<TARGET_OBJECTS:filegroup_encryption>
    )
    target_include_directories(test_raft PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_raft PRIVATE GTest::gtest GTest::gtest_main OpenSSL::Crypto)
    add_test(NAME Raft COMMAND test_raft)

    add_executable(test_segment tests/unit/test_segment.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_segment>
    )
    target_include_directories(test_segment PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_segment PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME Segment COMMAND test_segment)

    add_executable(test_distribution tests/unit/test_distribution.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_distribution>
    )
    target_include_directories(test_distribution PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_distribution PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME Distribution COMMAND test_distribution)

    add_executable(test_encryption tests/unit/test_encryption.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_encryption>
    )
    target_include_directories(test_encryption PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_encryption PRIVATE GTest::gtest GTest::gtest_main OpenSSL::Crypto)
    add_test(NAME Encryption COMMAND test_encryption)

    add_executable(test_compaction tests/unit/test_compaction.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_segment>
        $<TARGET_OBJECTS:filegroup_compaction>
    )
    target_include_directories(test_compaction PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_compaction PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME Compaction COMMAND test_compaction)

    add_executable(test_registry_service tests/unit/test_registry_service.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_raft>
        $<TARGET_OBJECTS:filegroup_registry>
        $<TARGET_OBJECTS:filegroup_manifest>
        $<TARGET_OBJECTS:filegroup_config>
        $<TARGET_OBJECTS:filegroup_file_header>
        $<TARGET_OBJECTS:filegroup_segment>
        $<TARGET_OBJECTS:filegroup_distribution>
        $<TARGET_OBJECTS:filegroup_encryption>
    )
    target_include_directories(test_registry_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_registry_service PRIVATE GTest::gtest GTest::gtest_main OpenSSL::Crypto)
    add_test(NAME RegistryService COMMAND test_registry_service)

    add_executable(test_storage_node_service tests/unit/test_storage_node_service.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_segment>
        $<TARGET_OBJECTS:filegroup_storage>
    )
    target_include_directories(test_storage_node_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_storage_node_service PRIVATE GTest::gtest GTest::gtest_main OpenSSL::Crypto)
    add_test(NAME StorageNodeService COMMAND test_storage_node_service)

    add_executable(test_upload_protocol tests/unit/test_upload_protocol.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_segment>
        $<TARGET_OBJECTS:filegroup_upload>
        $<TARGET_OBJECTS:filegroup_distribution>
        $<TARGET_OBJECTS:filegroup_encryption>
        $<TARGET_OBJECTS:filegroup_storage>
    )
    target_include_directories(test_upload_protocol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_upload_protocol PRIVATE GTest::gtest GTest::gtest_main OpenSSL::Crypto)
    add_test(NAME UploadProtocol COMMAND test_upload_protocol)

    add_executable(test_read_protocol tests/unit/test_read_protocol.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_segment>
        $<TARGET_OBJECTS:filegroup_read>
        $<TARGET_OBJECTS:filegroup_storage>
    )
    target_include_directories(test_read_protocol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_read_protocol PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME ReadProtocol COMMAND test_read_protocol)

    add_executable(test_versioning_heartbeat_recovery_scrubbing tests/unit/test_versioning_heartbeat_recovery_scrubbing.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_versioning>
        $<TARGET_OBJECTS:filegroup_heartbeat>
        $<TARGET_OBJECTS:filegroup_recovery>
        $<TARGET_OBJECTS:filegroup_scrubber>
    )
    target_include_directories(test_versioning_heartbeat_recovery_scrubbing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_versioning_heartbeat_recovery_scrubbing PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME VersioningHeartbeatRecoveryScrubbing COMMAND test_versioning_heartbeat_recovery_scrubbing)

    add_executable(test_expiry_storage_recovery tests/unit/test_expiry_storage_recovery.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_expiry>
        $<TARGET_OBJECTS:filegroup_recovery>
    )
    target_include_directories(test_expiry_storage_recovery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_expiry_storage_recovery PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME ExpiryStorageRecovery COMMAND test_expiry_storage_recovery)

    add_executable(test_engine_service tests/unit/test_engine_service.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_config>
        $<TARGET_OBJECTS:filegroup_file_header>
        $<TARGET_OBJECTS:filegroup_manifest>
        $<TARGET_OBJECTS:filegroup_raft>
        $<TARGET_OBJECTS:filegroup_registry>
        $<TARGET_OBJECTS:filegroup_segment>
        $<TARGET_OBJECTS:filegroup_distribution>
        $<TARGET_OBJECTS:filegroup_encryption>
        $<TARGET_OBJECTS:filegroup_storage>
        $<TARGET_OBJECTS:filegroup_engine>
    )
    target_include_directories(test_engine_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_engine_service PRIVATE GTest::gtest GTest::gtest_main OpenSSL::Crypto)
    add_test(NAME EngineService COMMAND test_engine_service)

    # Integration test: full upload → read round-trip
    add_executable(test_integration_upload_read tests/integration/test_upload_read.cc
        $<TARGET_OBJECTS:filegroup_common>
        $<TARGET_OBJECTS:filegroup_config>
        $<TARGET_OBJECTS:filegroup_file_header>
        $<TARGET_OBJECTS:filegroup_manifest>
        $<TARGET_OBJECTS:filegroup_raft>
        $<TARGET_OBJECTS:filegroup_registry>
        $<TARGET_OBJECTS:filegroup_segment>
        $<TARGET_OBJECTS:filegroup_distribution>
        $<TARGET_OBJECTS:filegroup_encryption>
        $<TARGET_OBJECTS:filegroup_storage>
        $<TARGET_OBJECTS:filegroup_engine>
    )
    target_include_directories(test_integration_upload_read PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_integration_upload_read PRIVATE GTest::gtest GTest::gtest_main OpenSSL::Crypto)
    add_test(NAME IntegrationUploadRead COMMAND test_integration_upload_read)
endif()

