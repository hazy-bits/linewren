add_library(classic-test STATIC classic-test.c)
target_include_directories(classic-test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(classic-test PUBLIC c_std_11)
