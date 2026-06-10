# CMake generated Testfile for 
# Source directory: D:/Ray Tracing Optimization NEW/optimization-based-on-pbrt-v3
# Build directory: D:/Ray Tracing Optimization NEW/optimization-based-on-pbrt-v3
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(pbrt_unit_test "pbrt_test")
set_tests_properties(pbrt_unit_test PROPERTIES  _BACKTRACE_TRIPLES "D:/Ray Tracing Optimization NEW/optimization-based-on-pbrt-v3/CMakeLists.txt;568;ADD_TEST;D:/Ray Tracing Optimization NEW/optimization-based-on-pbrt-v3/CMakeLists.txt;0;")
subdirs("src/ext/zlib")
subdirs("src/ext/openexr")
subdirs("src/ext/glog")
subdirs("src/ext/ptex/src")
