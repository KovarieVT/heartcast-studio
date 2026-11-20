# Unit tests (no GPU/display needed)
add_executable(kovalive_unit_tests test_unit.cpp)
target_link_libraries(kovalive_unit_tests PRIVATE kovalive_core Qt6::Test)
add_test(NAME unit COMMAND kovalive_unit_tests -platform offscreen)

# Engine integration test: real libobs startup, scene loading, recording,
# replay buffer and MULTISTREAM to two local RTMP servers.
# Needs an X display + RTMP listeners — run via tests/run_engine_test.sh.
add_executable(kovalive_engine_test test_engine.cpp)
target_link_libraries(kovalive_engine_test PRIVATE kovalive_core Qt6::Test)
