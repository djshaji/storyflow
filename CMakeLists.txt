cmake_minimum_required(VERSION 3.16)
project(StoryFlow LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(SENTENCE REQUIRED IMPORTED_TARGET libsoup-3.0 json-glib-1.0)
find_package(LibXml2 REQUIRED)
find_package(SQLite3 REQUIRED)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS data/word-frequencies.tsv data/phrases.tsv)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/word-frequencies.tsv" STORYFLOW_WORD_FREQUENCIES)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/phrases.tsv" STORYFLOW_PHRASES)
configure_file(src/word_data.hpp.in generated/word_data.hpp @ONLY)

add_library(storyflow_completion src/completion.cpp src/sentence.cpp src/profiles.cpp)
target_compile_features(storyflow_completion PUBLIC cxx_std_17)
target_include_directories(storyflow_completion PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(storyflow_completion PUBLIC PkgConfig::GTK PkgConfig::SENTENCE SQLite::SQLite3)
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

    add_executable(profile_test tests/profile_test.cpp)
    target_link_libraries(profile_test PRIVATE storyflow_completion)
    add_test(NAME writing_profiles COMMAND profile_test)
    set_tests_properties(word_completion PROPERTIES
        ENVIRONMENT "XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-data")
endif()
