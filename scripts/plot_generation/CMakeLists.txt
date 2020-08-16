cmake_minimum_required(VERSION 3.10)
project(plot_generation)
add_executable(append_true_fct append_true_fct.cpp)
target_compile_features(append_true_fct PRIVATE cxx_std_17)
