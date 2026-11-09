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

set(embedded_h3_location_sources "src/location.cpp" "src/multipolygon.cpp"
    ${h3_sources})

# H3 allocates only in cells_to_multipolygon, through H3_MEMORY (vendor H3
# alloc.h). With H3_ALLOC_PREFIX unset that is plain malloc, calloc, realloc
# and free. A prefix such as my_h3_ makes H3 call my_h3_malloc, my_h3_calloc,
# my_h3_realloc and my_h3_free, which the consumer defines with C linkage and
# libc semantics. alloc.h joins the names with TJOIN, which h3api.h defines
# only under H3_PREFIX, so H3_PREFIX is set empty, as the upstream H3 v4.5.0
# CMakeLists.txt always does. Exported names stay unprefixed. Compile
# definitions only, no vendored byte changes. ESP-IDF builds read the prefix
# from Kconfig, host builds from the EMBEDDED_H3_LOCATION_ALLOC_PREFIX cache
# variable.
function(embedded_h3_location_alloc_prefix target prefix)
    if(prefix STREQUAL "")
        return()
    endif()
    if(NOT prefix MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "H3 allocator prefix is not a C identifier prefix: '${prefix}'")
    endif()
    target_compile_definitions(${target} PRIVATE H3_PREFIX= H3_ALLOC_PREFIX=${prefix})
endfunction()

# Settings both the host and the ESP-IDF library take.
function(embedded_h3_location_common target)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON)
    # Section GC drops the H3 functions this library does not call.
    target_compile_options(${target}
        PUBLIC ${embedded_h3_location_public_path_options}
        PRIVATE -ffunction-sections -fdata-sections ${embedded_h3_location_path_options})
endfunction()

# A host static library of this component. The tests build a second one with
# their own allocator prefix.
function(embedded_h3_location_host_library target prefix)
    add_library(${target} STATIC ${embedded_h3_location_sources})
    target_include_directories(${target}
        PUBLIC
            "${CMAKE_CURRENT_LIST_DIR}/include"
        PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src"
            "${h3_root}/h3lib/include"
            "${CMAKE_CURRENT_BINARY_DIR}/include"
    )
    # The public header needs std::expected, so consumers need C++23 too.
    target_compile_features(${target} PUBLIC cxx_std_23)
    # Prove the library builds the way it runs on device.
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-Wall;-Wextra;-Wpedantic>")
    if(ENABLE_SANITIZERS)
        target_compile_options(${target} PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(${target} PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(${target} PRIVATE m)
    endif()
    embedded_h3_location_common(${target})
    embedded_h3_location_alloc_prefix(${target} "${prefix}")
endfunction()

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${embedded_h3_location_sources}
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS "src" "${h3_root}/h3lib/include" "${CMAKE_CURRENT_BINARY_DIR}/include"
    )
    # The prefix comes from this component's Kconfig. CONFIG_ values exist
    # here, after idf_component_register, but not during early expansion.
    embedded_h3_location_alloc_prefix(${COMPONENT_LIB}
        "${CONFIG_EMBEDDED_H3_LOCATION_ALLOC_PREFIX}")
    embedded_h3_location_common(${COMPONENT_LIB})
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

    set(EMBEDDED_H3_LOCATION_ALLOC_PREFIX "" CACHE STRING
        "Prefix for the malloc, calloc, realloc and free that H3 calls. Empty means plain malloc")
    embedded_h3_location_host_library(embedded_h3_location
        "${EMBEDDED_H3_LOCATION_ALLOC_PREFIX}")
    add_library(walkthru_earth::embedded_h3_location ALIAS embedded_h3_location)
endif()

if(NOT ESP_PLATFORM AND CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
    if(BUILD_TESTING)
        # test_location and test_multipolygon_malloc need a library that calls
        # plain malloc. With a consumer prefix set, the public library keeps
        # that prefix and the tests get their own unprefixed copy.
        if(EMBEDDED_H3_LOCATION_ALLOC_PREFIX STREQUAL "")
            set(eh3l_malloc_library embedded_h3_location)
        else()
            set(eh3l_malloc_library embedded_h3_location_malloc)
            embedded_h3_location_host_library(${eh3l_malloc_library} "")
        endif()
        # The same sources with the prefix eh3l_test_, so the counting
        # allocator in tests/test_multipolygon.cpp sees every H3 allocation.
        embedded_h3_location_host_library(embedded_h3_location_counted eh3l_test_)
        set(eh3l_libraries embedded_h3_location ${eh3l_malloc_library}
            embedded_h3_location_counted)
        list(REMOVE_DUPLICATES eh3l_libraries)
        foreach(library IN LISTS eh3l_libraries)
            target_compile_options(${library} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Werror>")
        endforeach()
        function(embedded_h3_location_test name source library)
            add_executable(${name} "${source}")
            target_link_libraries(${name} PRIVATE ${library})
            # The tests reach private helpers and H3's own header.
            target_include_directories(${name} PRIVATE
                "${CMAKE_CURRENT_LIST_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}/include")
            set_target_properties(${name} PROPERTIES
                CXX_STANDARD 23
                CXX_STANDARD_REQUIRED ON)
            target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
            add_test(NAME ${name} COMMAND ${name})
        endfunction()
        embedded_h3_location_test(test_location "tests/test_location.cpp"
            ${eh3l_malloc_library})
        embedded_h3_location_test(test_multipolygon "tests/test_multipolygon.cpp"
            embedded_h3_location_counted)
        target_compile_definitions(test_multipolygon PRIVATE
            EMBEDDED_H3_LOCATION_TEST_COUNTING=1)
        embedded_h3_location_test(test_multipolygon_malloc
            "tests/test_multipolygon.cpp" ${eh3l_malloc_library})
    endif()
endif()
