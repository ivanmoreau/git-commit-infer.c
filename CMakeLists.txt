cmake_minimum_required(VERSION 3.30)
project(git-commit-infer C)

set(CMAKE_C_STANDARD 23)

# Find CURL library
find_package(CURL REQUIRED)
find_package(json-c CONFIG REQUIRED)

# Include CURL headers
include_directories(${CURL_INCLUDE_DIRS})

add_executable(git-commit-infer src/main.c)

# Link CURL library with your target
target_link_libraries(git-commit-infer ${CURL_LIBRARIES} json-c::json-c)
