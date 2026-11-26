# libwillow: Willow's frontend-independent logic (the MCP bridge protocol,
# AI gameplay, Emerald adapters, phone video). It must not depend on Qt so
# that any frontend can link it.
set(WILLOW_SOURCES
	Bridge.cpp
	EmeraldGameState.cpp
	EmeraldMusic.cpp
	MobileVideo.cpp)

add_library(willow STATIC ${WILLOW_SOURCES})
target_compile_features(willow PUBLIC cxx_std_17)
target_include_directories(willow PUBLIC
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/third-party
	${PROJECT_SOURCE_DIR}/include
	${CMAKE_BINARY_DIR}/include)
target_link_libraries(willow PUBLIC ${BINARY_NAME})
target_compile_definitions(willow PUBLIC ${FEATURE_DEFINES} ${FUNCTION_DEFINES} ${OS_DEFINES})

if(BUILD_SUITE OR BUILD_AI_TESTS)
	enable_testing()
	foreach(TEST_NAME aigameplay bridge emeraldgamestate emeraldmusic mobilevideo)
		add_executable(test-willow-${TEST_NAME} test/${TEST_NAME}.cpp)
		target_link_libraries(test-willow-${TEST_NAME} willow)
		add_test(willow-${TEST_NAME} test-willow-${TEST_NAME})
	endforeach()
endif()
