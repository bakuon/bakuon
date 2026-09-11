set(TARGET_NAME "sandbox_example")

find_package(
    Qt6
    COMPONENTS Core RemoteObjects
    REQUIRED)

add_executable(${TARGET_NAME} main.cpp)
target_link_libraries(${TARGET_NAME} PRIVATE bakuon::sandbox Qt6::Core Qt6::RemoteObjects)
target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
bakuon_apply_warnings(${TARGET_NAME})
