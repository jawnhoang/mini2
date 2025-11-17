cmake_minimum_required(VERSION 3.20)
project(loop LANGUAGES C CXX)

# ------------------------------------------------------------
# Compiler setup (prefer Homebrew LLVM if available)
# ------------------------------------------------------------
if (EXISTS "/usr/local/opt/llvm/bin/clang")
  set(CMAKE_C_COMPILER "/usr/local/opt/llvm/bin/clang")
  set(CMAKE_CXX_COMPILER "/usr/local/opt/llvm/bin/clang++")
elseif (EXISTS "/opt/homebrew/opt/llvm/bin/clang")
  set(CMAKE_C_COMPILER "/opt/homebrew/opt/llvm/bin/clang")
  set(CMAKE_CXX_COMPILER "/opt/homebrew/opt/llvm/bin/clang++")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)

# ------------------------------------------------------------
# 1. OpenMP setup (portable for macOS + Linux)
# ------------------------------------------------------------
find_package(OpenMP)

# if (OpenMP_CXX_FOUND)
#   message(STATUS "Found OpenMP: ${OpenMP_CXX_FLAGS}")
#   add_compile_options(${OpenMP_CXX_FLAGS})
#   add_link_options(${OpenMP_EXE_LINKER_FLAGS})
# else()
#   if (APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
#     message(STATUS "Using manual OpenMP flags for macOS Clang")
#     add_compile_options(-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include)
#     add_link_options(-L/opt/homebrew/opt/libomp/lib -lomp)
#   endif()
# endif()


# ------------------------------------------------------------
# 2. gRPC / Protobuf setup (Homebrew-friendly)
# ------------------------------------------------------------
list(APPEND CMAKE_PREFIX_PATH
  "/usr/local/opt/grpc"
  "/usr/local/opt/protobuf"
  "/opt/homebrew/opt/grpc"
  "/opt/homebrew/opt/protobuf"
)

# Find gRPC first — it brings its own protobuf dependency
find_package(gRPC REQUIRED)

find_package(Protobuf REQUIRED)

# Abseil headers are required by newer protobuf (e.g., 33.x)
find_path(ABSL_INCLUDE_DIR
  NAMES absl/base/optimization.h
  HINTS
    /usr/local/opt/abseil/include
    /opt/homebrew/opt/abseil/include
    /opt/local/include
)
if(NOT ABSL_INCLUDE_DIR)
  message(FATAL_ERROR "Could not find Abseil headers (absl). Install via Homebrew: brew install abseil")
endif()
# ------------------------------------------------------------
# 3. Generate protobuf & gRPC sources
# ------------------------------------------------------------
set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(PROTO_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GENERATED_DIR})

set(PROTO_FILES ${PROTO_SRC_DIR}/route.proto)

add_custom_command(
  OUTPUT
    ${PROTO_GENERATED_DIR}/route.pb.cc
    ${PROTO_GENERATED_DIR}/route.pb.h
    ${PROTO_GENERATED_DIR}/route.grpc.pb.cc
    ${PROTO_GENERATED_DIR}/route.grpc.pb.h
  COMMAND protobuf::protoc
  ARGS --cpp_out=${PROTO_GENERATED_DIR}
       --grpc_out=${PROTO_GENERATED_DIR}
       --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
       -I${PROTO_SRC_DIR}
       ${PROTO_FILES}
  DEPENDS ${PROTO_FILES}
  COMMENT "Generating C++ gRPC sources from proto"
)

add_library(loop_lib
  ${PROTO_GENERATED_DIR}/route.pb.cc
  ${PROTO_GENERATED_DIR}/route.grpc.pb.cc
)
target_include_directories(loop_lib PUBLIC
  ${PROTO_GENERATED_DIR}
  ${Protobuf_INCLUDE_DIRS}
  ${ABSL_INCLUDE_DIR}
)
target_link_libraries(loop_lib PUBLIC
  gRPC::grpc++
  protobuf::libprotobuf
)

# ------------------------------------------------------------
# 4. Build executables
# ------------------------------------------------------------
# add_executable(loop-server src/loopImpl.cc)
# add_executable(loop-nt-server src/loopImpl.cc)
# app targets
include_directories(include)

add_executable(loop-server src/main_server.cc src/ipcLoop.cc src/jobs.cc src/WorldDataParser.cc)
target_compile_definitions(loop-server PRIVATE USE_OPENMP)

# ---- OpenMP setup per platform ----
if (APPLE)
  # AppleClang does not ship OpenMP runtime; discover libomp from package managers.
  find_path(LIBOMP_INCLUDE_DIR
    NAMES omp.h
    HINTS
      /usr/local/opt/libomp/include
      /opt/homebrew/opt/libomp/include
      /opt/local/include
  )
  find_library(LIBOMP_LIBRARY
    NAMES omp
    HINTS
      /usr/local/opt/libomp/lib
      /opt/homebrew/opt/libomp/lib
      /opt/local/lib
  )
  if (LIBOMP_INCLUDE_DIR AND LIBOMP_LIBRARY)
    message(STATUS "OpenMP: using ${LIBOMP_LIBRARY}")
    get_filename_component(LIBOMP_LIBDIR "${LIBOMP_LIBRARY}" DIRECTORY)
    # Enable OpenMP front-end and wire in libomp
    target_compile_options(loop-server PRIVATE -Xpreprocessor -fopenmp)
    target_include_directories(loop-server PRIVATE "${LIBOMP_INCLUDE_DIR}")
    target_link_libraries(loop-server PRIVATE "${LIBOMP_LIBRARY}")
    target_link_options(loop-server PRIVATE "-L${LIBOMP_LIBDIR}" "-Wl,-rpath,${LIBOMP_LIBDIR}")
  else()
    message(FATAL_ERROR "libomp not found. Install with: brew install libomp (or MacPorts: sudo port install libomp)")
  endif()
elseif (OpenMP_CXX_FOUND)
  # Non-Apple platforms with a working OpenMP toolchain
  target_link_libraries(loop-server PRIVATE OpenMP::OpenMP_CXX)
endif()

add_executable(loop-nt-server src/main_server.cc src/ipcLoop.cc)


add_executable(loop-client src/main_client.cc)
add_executable(loop-nt-client src/main_client.cc)



target_include_directories(loop-server PRIVATE ${PROTO_GENERATED_DIR})
target_include_directories(loop-nt-server PRIVATE ${PROTO_GENERATED_DIR})
target_include_directories(loop-nt-client PRIVATE ${PROTO_GENERATED_DIR})
target_include_directories(loop-client PRIVATE ${PROTO_GENERATED_DIR})

target_link_libraries(loop-server PRIVATE loop_lib)
target_link_libraries(loop-nt-server PRIVATE loop_lib)
target_link_libraries(loop-client PRIVATE loop_lib)
target_link_libraries(loop-nt-client PRIVATE loop_lib)

# ------------------------------------------------------------
# 5. macOS linker tweak
# ------------------------------------------------------------
if(APPLE)
  foreach(target loop-server loop-nt-server)
    set_target_properties(${target} PROPERTIES
      LINK_FLAGS "-Wl,-undefined,dynamic_lookup"
    )
  endforeach()
endif()
