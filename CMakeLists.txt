cmake_minimum_required(VERSION 3.16)
project(StoryFlow LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk+-3.0)
find_package(LibXml2 REQUIRED)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS data/word-frequencies.tsv)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/word-frequencies.tsv" STORYFLOW_WORD_FREQUENCIES)
configure_file(src/word_data.hpp.in generated/word_data.hpp @ONLY)

add_library(storyflow_completion src/completion.cpp)
target_compile_features(storyflow_completion PUBLIC cxx_std_17)
target_include_directories(storyflow_completion PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(storyflow_completion PUBLIC PkgConfig::GTK)
target_compile_options(storyflow_completion PRIVATE -Wall -Wextra -Wpedantic)

add_executable(storyflow src/main.cpp src/document.cpp)
target_compile_features(storyflow PRIVATE cxx_std_17)
target_link_libraries(storyflow PRIVATE storyflow_completion LibXml2::LibXml2)
target_compile_options(storyflow PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    add_executable(document_test tests/document_test.cpp src/document.cpp)
    target_compile_features(document_test PRIVATE cxx_std_17)
    target_include_directories(document_test PRIVATE src)
    target_link_libraries(document_test PRIVATE PkgConfig::GTK LibXml2::LibXml2)
    add_test(NAME document_roundtrip COMMAND document_test)

    add_executable(completion_test tests/completion_test.cpp src/document.cpp)
    target_link_libraries(completion_test PRIVATE storyflow_completion LibXml2::LibXml2)
    add_test(NAME word_completion COMMAND completion_test)
endif()
