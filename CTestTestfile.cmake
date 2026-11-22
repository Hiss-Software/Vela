# CMake generated Testfile for 
# Source directory: C:/Users/toxic/Desktop/Vela
# Build directory: C:/Users/toxic/Desktop/Vela
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("fib" "C:/Users/toxic/Desktop/Vela/vela.exe" "check" "C:/Users/toxic/Desktop/Vela/tests/fib.vela")
set_tests_properties("fib" PROPERTIES  _CMAKE_TEST_BUILD_DEPENDS "C:/Users/toxic/Desktop/Vela/vela.exe" _BACKTRACE_TRIPLES "C:/Users/toxic/Desktop/Vela/CMakeLists.txt;35;add_test;C:/Users/toxic/Desktop/Vela/CMakeLists.txt;0;")
add_test("reject_bad_type" "C:/Users/toxic/Desktop/Vela/vela.exe" "check" "C:/Users/toxic/Desktop/Vela/tests/bad_type.vela")
set_tests_properties("reject_bad_type" PROPERTIES  WILL_FAIL "TRUE" _CMAKE_TEST_BUILD_DEPENDS "C:/Users/toxic/Desktop/Vela/vela.exe" _BACKTRACE_TRIPLES "C:/Users/toxic/Desktop/Vela/CMakeLists.txt;37;add_test;C:/Users/toxic/Desktop/Vela/CMakeLists.txt;0;")
add_test("reject_shadowing" "C:/Users/toxic/Desktop/Vela/vela.exe" "check" "C:/Users/toxic/Desktop/Vela/tests/shadowing.vela")
set_tests_properties("reject_shadowing" PROPERTIES  WILL_FAIL "TRUE" _CMAKE_TEST_BUILD_DEPENDS "C:/Users/toxic/Desktop/Vela/vela.exe" _BACKTRACE_TRIPLES "C:/Users/toxic/Desktop/Vela/CMakeLists.txt;39;add_test;C:/Users/toxic/Desktop/Vela/CMakeLists.txt;0;")
