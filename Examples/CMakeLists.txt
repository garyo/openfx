set(OFX_SUPPORT_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Support/include")

set(PLUGINS
	Basic
	ChoiceParams
	ColourSpace
	Custom
	DepthConverter
	Invert
	OpenGL
	Rectangle
	Test
        DrawSuite
	)

foreach(PLUGIN IN LISTS PLUGINS)
	file(GLOB_RECURSE PLUGIN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN}/*.cpp")

        set(TGT example-${PLUGIN})
	add_ofx_plugin(${TGT} ${PLUGIN})
	target_sources(${TGT} PUBLIC ${PLUGIN_SOURCES})
	target_link_libraries(${TGT} ${CONAN_LIBS})
	target_include_directories(${TGT} PUBLIC ${OFX_HEADER_DIR} ${OFX_SUPPORT_HEADER_DIR})
        if (OFX_SUPPORTS_OPENCLRENDER)
          target_link_libraries(${TGT} PRIVATE OpenCL::Headers OpenCL::OpenCL)
        endif()
endforeach()

target_link_libraries(example-OpenGL PRIVATE opengl::opengl)
target_link_libraries(example-Custom PRIVATE opengl::opengl)
target_link_libraries(example-ColourSpace PRIVATE cimg::cimg)
target_link_libraries(example-ColourSpace PRIVATE spdlog::spdlog_header_only)

# Plugins written on the openfx-cpp bindings, at C++20 for std::span.
if(cxx_std_20 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
	foreach(PLUGIN IN ITEMS CppGain TestProps)
		file(GLOB_RECURSE PLUGIN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN}/*.cpp")
		set(TGT example-${PLUGIN})
		add_ofx_plugin(${TGT} ${PLUGIN})
		target_sources(${TGT} PRIVATE ${PLUGIN_SOURCES})
		target_link_libraries(${TGT} PRIVATE OpenFX::openfx-cpp)
		target_compile_features(${TGT} PRIVATE cxx_std_20)
	endforeach()
	# TestProps also compiles the host-specific-props example header.
	target_include_directories(example-TestProps PRIVATE
		"${PROJECT_SOURCE_DIR}/openfx-cpp/examples/host-specific-props")
else()
	message(STATUS "Examples: the compiler has no C++20, skipping CppGain and TestProps")
endif()
