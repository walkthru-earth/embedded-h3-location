cmake_minimum_required(VERSION 3.19)

# Verify every vendored official Uber H3 v4.5.0 file against its SHA-256 manifest
# before configuring the upstream public header; no host code generators required.
set(h3_root "${CMAKE_CURRENT_LIST_DIR}/vendor/h3")
file(READ "${h3_root}/manifest.json" h3_manifest)
string(JSON h3_count LENGTH "${h3_manifest}" files)
math(EXPR h3_last "${h3_count} - 1")
foreach(index RANGE ${h3_last})
    string(JSON name MEMBER "${h3_manifest}" files ${index})
    string(JSON expected GET "${h3_manifest}" files "${name}")
    file(SHA256 "${h3_root}/${name}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Vendored H3 hash mismatch: ${name}")
    endif()
endforeach()

set(H3_VERSION_MAJOR 4)
set(H3_VERSION_MINOR 5)
set(H3_VERSION_PATCH 0)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file("${h3_root}/h3lib/include/h3api.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/include/h3api.h" @ONLY)
file(GLOB h3_sources "${h3_root}/h3lib/lib/*.c")

if(ESP_PLATFORM)
    idf_component_register(
        SRCS "src/aq_location.cpp" ${h3_sources}
        INCLUDE_DIRS "src"
        PRIV_INCLUDE_DIRS "${h3_root}/h3lib/include" "${CMAKE_CURRENT_BINARY_DIR}/include"
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
    # ESP-IDF's section GC drops unused polygon/traversal/export functions.
    target_compile_options(${COMPONENT_LIB} PRIVATE -ffunction-sections -fdata-sections)
else()
    project(embedded_h3_location VERSION 1.0.0 LANGUAGES C CXX)

    option(ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

    add_library(embedded_h3_location STATIC
        "src/aq_location.cpp"
        ${h3_sources}
    )
    add_library(walkthru_earth::embedded_h3_location ALIAS embedded_h3_location)

    target_include_directories(embedded_h3_location
        PUBLIC
            "${CMAKE_CURRENT_LIST_DIR}/src"
        PRIVATE
            "${h3_root}/h3lib/include"
            "${CMAKE_CURRENT_BINARY_DIR}/include"
    )
    target_compile_features(embedded_h3_location PUBLIC cxx_std_17)
    target_compile_options(embedded_h3_location PRIVATE -ffunction-sections -fdata-sections)
    if(ENABLE_SANITIZERS)
        target_compile_options(embedded_h3_location PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(embedded_h3_location PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(embedded_h3_location PRIVATE m)
    endif()

    if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
        include(CTest)
        if(BUILD_TESTING)
            enable_testing()
            add_executable(test_h3_location "tests/test_h3_location.cpp")
            target_link_libraries(test_h3_location PRIVATE embedded_h3_location)
            target_compile_options(test_h3_location PRIVATE -Wall -Wextra -Werror)
            add_test(NAME test_h3_location COMMAND test_h3_location)
        endif()
    endif()
endif()
