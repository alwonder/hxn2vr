# hxvr: C++20 VR module (static lib), its unit tests, and the OpenXR static loader.
# The module never includes engine headers; the engine only sees engine/vr/include/vr_api.h.

set(HXVR_DIR "${CMAKE_SOURCE_DIR}/engine/vr")
set(HXVR_EXTERNAL "${HXVR_DIR}/external")

# CRT must match the engine target: /MDd in Debug, /MT in Release (see cmake/glhexen2.cmake).
set(HXVR_MSVC_RUNTIME "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>")

# --- OpenXR static loader (same setup as spike/openxr) ---
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_SYSTEM_JSONCPP OFF CACHE BOOL "" FORCE)
add_subdirectory(${HXVR_EXTERNAL}/openxr-sdk ${CMAKE_BINARY_DIR}/openxr-sdk EXCLUDE_FROM_ALL)
# The loader forces the DLL CRT for static builds; override to match the engine.
set_target_properties(openxr_loader PROPERTIES MSVC_RUNTIME_LIBRARY "${HXVR_MSVC_RUNTIME}")

# --- module ---
add_library(hxvr STATIC
    ${HXVR_DIR}/core/vr_module.cpp
    ${HXVR_DIR}/core/vr_math.cpp
)
target_compile_features(hxvr PUBLIC cxx_std_20)
set_target_properties(hxvr PROPERTIES CXX_EXTENSIONS OFF MSVC_RUNTIME_LIBRARY "${HXVR_MSVC_RUNTIME}")
target_include_directories(hxvr
    PUBLIC  ${HXVR_DIR}/include
    PRIVATE ${HXVR_DIR}/core ${HXVR_EXTERNAL}/glm)
target_compile_definitions(hxvr PRIVATE GLM_FORCE_RADIANS _CRT_SECURE_NO_WARNINGS)
target_link_libraries(hxvr PRIVATE openxr_loader)
if(MSVC)
    target_compile_options(hxvr PRIVATE /W4 /permissive-)
endif()

# --- tests ---
enable_testing()
add_executable(vr_tests
    ${CMAKE_SOURCE_DIR}/tests/vr/main.cpp
    ${CMAKE_SOURCE_DIR}/tests/vr/test_smoke.cpp
    ${CMAKE_SOURCE_DIR}/tests/vr/test_tracking.cpp
    ${CMAKE_SOURCE_DIR}/tests/vr/test_projection.cpp
)
target_compile_features(vr_tests PRIVATE cxx_std_20)
set_target_properties(vr_tests PROPERTIES CXX_EXTENSIONS OFF MSVC_RUNTIME_LIBRARY "${HXVR_MSVC_RUNTIME}")
target_include_directories(vr_tests PRIVATE ${HXVR_EXTERNAL}/doctest ${HXVR_EXTERNAL}/glm)
target_link_libraries(vr_tests PRIVATE hxvr)
if(MSVC)
    target_compile_options(vr_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME vr_tests COMMAND vr_tests)
