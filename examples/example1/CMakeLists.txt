add_executable(example1 main.cpp)

# target_link_libraries(example1 PRIVATE bakuon::bakuon)
target_compile_features(example1 PRIVATE cxx_std_20)
bakuon_apply_warnings(example1)
