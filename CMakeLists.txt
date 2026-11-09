# CXX_STANDARD 23 needs CMake 3.20.
cmake_minimum_required(VERSION 3.20)

# Verify every vendored official Uber H3 file against its SHA-256 manifest
# before configuring the upstream public header. No host code generators.
# vendor/h3 must hold exactly the listed files plus manifest.json, so an
# unlisted header cannot be picked up from the include path.
set(h3_root "${CMAKE_CURRENT_LIST_DIR}/vendor/h3")
file(READ "${h3_root}/manifest.json" h3_manifest)
string(JSON h3_count LENGTH "${h3_manifest}" files)
math(EXPR h3_last "${h3_count} - 1")
file(GLOB_RECURSE h3_present LIST_DIRECTORIES false RELATIVE "${h3_root}"
     "${h3_root}/*")
list(REMOVE_ITEM h3_present "manifest.json")
# Finder writes .DS_Store when a folder is opened on macOS. It is never built.
list(FILTER h3_present EXCLUDE REGEX "(^|/)\\.DS_Store$")
set(h3_sources "")
foreach(index RANGE ${h3_last})
    string(JSON name MEMBER "${h3_manifest}" files ${index})
    list(REMOVE_ITEM h3_present "${name}")
    string(JSON expected GET "${h3_manifest}" files "${name}")
    file(SHA256 "${h3_root}/${name}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Vendored H3 hash mismatch: ${name}")
    endif()
    # Compile only the C files the manifest lists, so an unverified file
    # dropped into vendor/h3 never reaches the build.
    if(name MATCHES "^h3lib/lib/[^/]+\\.c$")
        list(APPEND h3_sources "${h3_root}/${name}")
    endif()
endforeach()
if(h3_present)
    list(JOIN h3_present ", " h3_unlisted)
    message(FATAL_ERROR "Vendored H3 file not in manifest.json: ${h3_unlisted}")
endif()

# The header version comes from the verified upstream VERSION file.
file(STRINGS "${h3_root}/VERSION" h3_version LIMIT_COUNT 1)
if(NOT h3_version MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "vendor/h3/VERSION is not X.Y.Z: ${h3_version}")
endif()
set(H3_VERSION_MAJOR ${CMAKE_MATCH_1})
set(H3_VERSION_MINOR ${CMAKE_MATCH_2})
set(H3_VERSION_PATCH ${CMAKE_MATCH_3})
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file("${h3_root}/h3lib/include/h3api.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/include/h3api.h" @ONLY)

# Reproducible images. H3 uses assert(), which expands __FILE__, and the
# compiler writes source paths into debug info. Map this checkout and its build
# directory to fixed names so no image or ELF holds a checkout path. Consumers
# that map paths themselves can turn this off.
option(EMBEDDED_H3_LOCATION_MAP_PATHS
       "Map this checkout's paths to fixed names in macros and debug info" ON)
# The checkout map is also public, because a consumer's debug info names the
# public header's directory. It maps only this checkout. When the build
# directory sits inside the checkout, GCC applies the last matching map, so
# the build map comes last.
set(embedded_h3_location_public_path_options "")
set(embedded_h3_location_path_options "")
if(EMBEDDED_H3_LOCATION_MAP_PATHS)
    get_filename_component(eh3l_source_real "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
    get_filename_component(eh3l_binary_real "${CMAKE_CURRENT_BINARY_DIR}" REALPATH)
    foreach(eh3l_dir IN ITEMS "${CMAKE_CURRENT_LIST_DIR}" "${eh3l_source_real}")
        list(APPEND embedded_h3_location_public_path_options
             "-ffile-prefix-map=${eh3l_dir}=embedded-h3-location")
    endforeach()
    foreach(eh3l_dir IN ITEMS "${CMAKE_CURRENT_BINARY_DIR}" "${eh3l_binary_real}")
        list(APPEND embedded_h3_location_path_options
             "-ffile-prefix-map=${eh3l_dir}=embedded-h3-location-build")
    endforeach()
    list(REMOVE_DUPLICATES embedded_h3_location_public_path_options)
    list(REMOVE_DUPLICATES embedded_h3_location_path_options)
endif()

set(embedded_h3_location_sources "src/location.cpp" ${h3_sources})

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${embedded_h3_location_sources}
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS "src" "${h3_root}/h3lib/include" "${CMAKE_CURRENT_BINARY_DIR}/include"
    )
    set(embedded_h3_location_target ${COMPONENT_LIB})
    # ESP-IDF adds its own -std=gnu++26 (v6.1) as a build wide compile option,
    # which lands after the flag that CXX_STANDARD writes. This option comes
    # last, so an SDK default change cannot move the dialect. It applies to
    # this component's C++ only. H3 keeps the SDK C dialect.
    target_compile_options(${COMPONENT_LIB} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++23>")
else()
    project(embedded_h3_location VERSION 2.0.0 LANGUAGES C CXX)

    # A top level host build writes compile_commands.json for clangd (.clangd).
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    endif()

    option(ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

    add_library(embedded_h3_location STATIC ${embedded_h3_location_sources})
    add_library(walkthru_earth::embedded_h3_location ALIAS embedded_h3_location)
    set(embedded_h3_location_target embedded_h3_location)

    target_include_directories(embedded_h3_location
        PUBLIC
            "${CMAKE_CURRENT_LIST_DIR}/include"
        PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src"
            "${h3_root}/h3lib/include"
            "${CMAKE_CURRENT_BINARY_DIR}/include"
    )
    # The public header needs std::expected, so consumers need C++23 too.
    target_compile_features(embedded_h3_location PUBLIC cxx_std_23)
    # Prove the library builds the way it runs on device.
    target_compile_options(embedded_h3_location PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-Wall;-Wextra;-Wpedantic>")
    if(ENABLE_SANITIZERS)
        target_compile_options(embedded_h3_location PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(embedded_h3_location PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(embedded_h3_location PRIVATE m)
    endif()
endif()

set_target_properties(${embedded_h3_location_target} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
# Section GC drops the H3 functions this library does not call.
target_compile_options(${embedded_h3_location_target}
    PUBLIC ${embedded_h3_location_public_path_options}
    PRIVATE -ffunction-sections -fdata-sections ${embedded_h3_location_path_options})

if(NOT ESP_PLATFORM AND CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
    if(BUILD_TESTING)
        target_compile_options(embedded_h3_location PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Werror>")
        add_executable(test_location "tests/test_location.cpp")
        target_link_libraries(test_location PRIVATE embedded_h3_location)
        # The tests reach the private error mapping and H3's own header.
        target_include_directories(test_location PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}/include")
        set_target_properties(test_location PROPERTIES
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON)
        target_compile_options(test_location PRIVATE -Wall -Wextra -Wpedantic -Werror)
        add_test(NAME test_location COMMAND test_location)
    endif()
endif()
