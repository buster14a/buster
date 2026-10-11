cmake_minimum_required(VERSION 3.20)
project(buster_embed_example C)

find_package(BusterCompiler CONFIG REQUIRED)
add_executable(buster_embed_example example.c)
target_link_libraries(buster_embed_example PRIVATE Buster::buster_compiler)
target_compile_features(buster_embed_example PRIVATE c_std_99)
