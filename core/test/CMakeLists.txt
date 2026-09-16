add_executable(devoursphere_tests sim_test.cpp)
target_link_libraries(devoursphere_tests PRIVATE devoursphere::sim devoursphere::render)
target_compile_options(devoursphere_tests PRIVATE -Wall -Wextra)
if(DEVOURSPHERE_SANITIZE)
    target_compile_options(devoursphere_tests PRIVATE -fsanitize=address,undefined -g)
    target_link_options(devoursphere_tests PRIVATE -fsanitize=address,undefined)
    target_compile_options(devoursphere_sim PRIVATE -fsanitize=address,undefined -g)
    target_compile_options(devoursphere_render PRIVATE -fsanitize=address,undefined -g)
endif()
add_test(NAME devoursphere_tests COMMAND devoursphere_tests)

# The Xiamocon front end builds the core at 30 Hz, so the suite runs at that
# rate too: the constants in sim/config.hpp are derived from per-second
# values, and it is easy to write a test that silently assumes 60. The sources
# are compiled again rather than relinked, because TICK_RATE is a constexpr in
# a header.
add_executable(devoursphere_tests_30hz sim_test.cpp ${DEVOURSPHERE_CORE_SOURCES})
target_link_libraries(devoursphere_tests_30hz PRIVATE shapoco::gfx)
target_include_directories(devoursphere_tests_30hz
    PRIVATE ${CMAKE_SOURCE_DIR}/core/include)
target_compile_definitions(devoursphere_tests_30hz PRIVATE DEVOURSPHERE_TICK_RATE=30)
target_compile_features(devoursphere_tests_30hz PRIVATE cxx_std_17)
target_compile_options(devoursphere_tests_30hz PRIVATE -Wall -Wextra)
add_test(NAME devoursphere_tests_30hz COMMAND devoursphere_tests_30hz)

add_executable(devoursphere_sim_bench sim_bench.cpp)
target_link_libraries(devoursphere_sim_bench PRIVATE devoursphere::sim)
