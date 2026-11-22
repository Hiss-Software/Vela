cmake_minimum_required(VERSION 3.24)
project(Vela VERSION 0.2.0 LANGUAGES CXX)

# Vela собирается на C++23. На MSYS2 UCRT64 это просто работает:
#   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake
#   cmake -S . -B build -G Ninja && cmake --build build

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(vela
    src/diag.cpp
    src/lexer.cpp
    src/parser.cpp
    src/checker.cpp
    src/codegen.cpp
    src/driver.cpp
    src/main.cpp
)

target_include_directories(vela PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(vela PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(vela PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS vela RUNTIME DESTINATION bin)

# Строгие смоук-тесты: рабочая программа обязана проходить проверку,
# сломанная — обязана падать. Иначе это не тесты, а украшение.
enable_testing()
add_test(NAME fib
         COMMAND vela check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fib.vela)
add_test(NAME reject_bad_type
         COMMAND vela check ${CMAKE_CURRENT_SOURCE_DIR}/tests/bad_type.vela)
add_test(NAME reject_shadowing
         COMMAND vela check ${CMAKE_CURRENT_SOURCE_DIR}/tests/shadowing.vela)
set_tests_properties(reject_bad_type reject_shadowing PROPERTIES WILL_FAIL TRUE)
