# SPDX-License-Identifier: BSD-3-Clause

# ofxtesthost: a command-line host for exercising plugins, built on the
# openfx-cpp host headers at C++20. See README.md.

if(NOT cxx_std_20 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
  message(STATUS "TestHost: the compiler has no C++20, skipping ofxtesthost")
  return()
endif()

find_package(Threads REQUIRED)

file(GLOB OFX_TESTHOST_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_executable(ofxtesthost ${OFX_TESTHOST_SOURCES})
target_compile_features(ofxtesthost PRIVATE cxx_std_20)
target_link_libraries(ofxtesthost PRIVATE OpenFX::openfx-cpp Threads::Threads
                                          ${CMAKE_DL_LIBS})

if(NOT BUILD_EXAMPLE_PLUGINS)
  return()
endif()

# host.*: ofxtesthost run against the example and Support plugins, the same
# tests pcons-build.py defines. Each test gives the host a plugin's binary,
# $<TARGET_FILE:...>, inside its bundle, and the host loads that bundle;
# OFX_PLUGIN_BUILD_DIR is the directory holding every bundle.
function(ofx_host_test NAME)
  add_test(NAME host.${NAME} COMMAND ofxtesthost ${ARGN})
  set_tests_properties(host.${NAME} PROPERTIES LABELS host)
endfunction()

ofx_host_test(list "${OFX_PLUGIN_BUILD_DIR}" --list)
ofx_host_test(invert $<TARGET_FILE:example-Invert>
  --fill 0.25,0.5,0.75,1 --expect 3,3,0.75,0.5,0.25,0)
ofx_host_test(gain-param $<TARGET_FILE:example-Basic>
  --param scale=2 --fill 0.25,0.25,0.25,1 --expect 5,5,0.5,0.5,0.5,2)
ofx_host_test(support-invert $<TARGET_FILE:example-Invert-support>
  --fill 0.25,0.5,0.75,1 --expect 3,3,0.75,0.5,0.25,0)
ofx_host_test(chain "${OFX_PLUGIN_BUILD_DIR}"
  --plugin net.sf.openfx.basicPlugin --param scale=2
  --plugin net.sf.openfx.invertPlugin --fill 0.25,0.25,0.25,1
  --expect 10,10,0.5,0.5,0.5,0)
ofx_host_test(chain-roundtrip "${OFX_PLUGIN_BUILD_DIR}"
  --plugin uk.co.thefoundry.OfxInvertExample
  --plugin uk.co.thefoundry.OfxInvertExample --fill 0.25,0.5,0.75,1
  --expect 3,3,0.25,0.5,0.75,1)
ofx_host_test(generator $<TARGET_FILE:example-Rectangle>
  --context OfxImageEffectContextGenerator --param colour=1,0,0,1 --size 32x32
  --expect 16,16,1,0,0,1)
ofx_host_test(generator-infinite-rod $<TARGET_FILE:example-Generator-support>
  --size 32x32)
ofx_host_test(rectangle-over-input $<TARGET_FILE:example-Rectangle>
  --param colour=0,1,0,1 --param corner1=8,8 --param corner2=24,24 --size 32x32
  --expect 16,16,0,1,0,1 --expect 2,2,0.0645,0.0645,0.5,1)
ofx_host_test(choice-params $<TARGET_FILE:example-ChoiceParams-support>
  --param red_choice=1 --param green_choice=1 --param blue_choice=blue_0.5
  --fill 0.5,0.5,0.5,1 --expect 4,4,0.25,0.5,0.25,1)
ofx_host_test(strchoice-unknown-uses-default $<TARGET_FILE:example-ChoiceParams-support>
  --param blue_choice=not-an-option --fill 0.5,0.5,0.5,1 --expect 4,4,0,0,0,1)
ofx_host_test(clip-prefs-identity $<TARGET_FILE:example-DepthConverter>
  --param depth=2 --fill 0.25,0.5,0.75,1 --expect 4,4,0.25,0.5,0.75,1)
ofx_host_test(many-param-types $<TARGET_FILE:example-Tester-support>
  --describe --fill 0.5,0.5,0.5,1)
ofx_host_test(props-compliance $<TARGET_FILE:example-TestProps>
  --param scale=2 --fill 0.5,0.5,0.5,1)
ofx_host_test(cppgain $<TARGET_FILE:example-CppGain>
  --param gain=2,2,2,1 --fill 0.25,0.25,0.25,1 --expect 5,5,0.5,0.5,0.5,1)
ofx_host_test(cppgain-byte-rgb $<TARGET_FILE:example-CppGain>
  --depth Byte --components RGB --param gain=2,2,2,1 --param offset=0.25
  --fill 0.25,0.25,0.25,1 --expect 5,5,0.7529,0.7529,0.7529,1)
ofx_host_test(mask-clip $<TARGET_FILE:example-Basic-support>
  --context OfxImageEffectContextGeneral --param scale=2
  --clip Mask=fill:0,0,0,0.5 --fill 0.25,0.25,0.25,1
  --expect 5,5,0.375,0.375,0.375,1)

# The actions the host asks about a frame before rendering it, and the ones
# that bracket an edit.
ofx_host_test(roi-frames-needed $<TARGET_FILE:example-Basic-support>
  --context OfxImageEffectContextGeneral --param scale=2 --verbose
  --fill 0.25,0.25,0.25,1 --expect 5,5,0.5,0.5,0.5,1)
ofx_host_test(roi-frames-needed-filter $<TARGET_FILE:example-Invert-support>
  --verbose --fill 0.25,0.5,0.75,1 --expect 3,3,0.75,0.5,0.25,0)
ofx_host_test(time-domain $<TARGET_FILE:example-Generator-support>
  --context OfxImageEffectContextGenerator --size 32x32 --verbose)

# Colour management (OFX 1.5). A basic-style plugin under a core-style host,
# and a core-style one under a basic-style host, fall back to basic.
ofx_host_test(cppgain-colour-basic $<TARGET_FILE:example-CppGain>
  --colour-management basic --verbose --param gain=2,2,2,1
  --fill 0.25,0.25,0.25,1 --expect 5,5,0.5,0.5,0.5,1)
ofx_host_test(cppgain-colour-core-host $<TARGET_FILE:example-CppGain>
  --colour-management core --colourspace ACEScct --verbose --param gain=2,2,2,1
  --fill 0.25,0.25,0.25,1 --expect 5,5,0.5,0.5,0.5,1)
# The ColourSpace examples label a copy of the source from x=100 rightwards,
# so a narrow frame is a plain copy.
ofx_host_test(colour-basic-basic $<TARGET_FILE:example-ColourSpace>
  --plugin io.aswf.openfx.example.ColourspacePluginBasic
  --colour-management basic --verbose --size 64x200 --fill 0.25,0.5,0.75,1
  --expect 5,5,0.25,0.5,0.75,1)
ofx_host_test(colour-core-core $<TARGET_FILE:example-ColourSpace>
  --plugin io.aswf.openfx.example.ColourspacePluginCore --colour-management core
  --verbose --size 64x200 --fill 0.25,0.5,0.75,1 --expect 5,5,0.25,0.5,0.75,1)
ofx_host_test(colour-core-full $<TARGET_FILE:example-ColourSpace>
  --plugin io.aswf.openfx.example.ColourspacePluginFull --colour-management core
  --verbose --size 64x200 --fill 0.25,0.5,0.75,1 --expect 5,5,0.25,0.5,0.75,1)
ofx_host_test(colour-core-plugin-basic-host $<TARGET_FILE:example-ColourSpace>
  --plugin io.aswf.openfx.example.ColourspacePluginCore
  --colour-management basic --verbose --size 64x200 --fill 0.25,0.5,0.75,1
  --expect 5,5,0.25,0.5,0.75,1)
ofx_host_test(colour-core-colourspace $<TARGET_FILE:example-ColourSpace>
  --plugin io.aswf.openfx.example.ColourspacePluginCore --colour-management core
  --colourspace ACEScct --verbose --param output_colourspace=ACEScg
  --size 64x200 --fill 0.25,0.5,0.75,1 --expect 5,5,0.25,0.5,0.75,1)

# Tiles and render scale. --check-tiles also renders the frame whole and warns
# if the two differ.
ofx_host_test(tiles-gain $<TARGET_FILE:example-Basic>
  --tiles 3 --check-tiles --param scale=2 --fill 0.25,0.25,0.25,1
  --expect 5,5,0.5,0.5,0.5,2)
ofx_host_test(tiles-invert $<TARGET_FILE:example-Invert-support>
  --tiles 3 --check-tiles --fill 0.25,0.5,0.75,1 --expect 3,3,0.75,0.5,0.25,0)
ofx_host_test(tiles-fixed-size $<TARGET_FILE:example-Rectangle>
  --tile 7x5 --check-tiles --param colour=0,1,0,1 --param corner1=8,8
  --param corner2=24,24 --size 32x32 --expect 16,16,0,1,0,1
  --expect 2,2,0.0645,0.0645,0.5,1)
ofx_host_test(render-scale-generator $<TARGET_FILE:example-Rectangle>
  --context OfxImageEffectContextGenerator --render-scale 0.5
  --param colour=1,0,0,1 --param corner1=8,8 --param corner2=24,24 --size 32x32
  --expect 4,4,1,0,0,1 --expect 6,6,1,0,0,1)
ofx_host_test(render-scale-filter $<TARGET_FILE:example-Rectangle>
  --render-scale 0.5 --param colour=0,1,0,1 --param corner1=8,8
  --param corner2=24,24 --size 32x32 --fill 0.25,0.25,0.25,1
  --expect 6,6,0,1,0,1 --expect 1,1,0.25,0.25,0.25,1)
ofx_host_test(render-scale-tiled $<TARGET_FILE:example-Rectangle>
  --render-scale 0.5 --tiles 3 --check-tiles --param colour=0,1,0,1
  --param corner1=8,8 --param corner2=24,24 --size 32x32 --fill 0.25,0.25,0.25,1
  --expect 6,6,0,1,0,1)

# Animation. A parameter type that does not animate takes a keyframe as its
# value.
ofx_host_test(animated-param $<TARGET_FILE:example-Basic>
  --param scale@0=1 --param scale@4=3 --frames 0-4 --fill 0.25,0.25,0.25,1
  --expect 0:5,5,0.25,0.25,0.25,1 --expect 4:5,5,0.75,0.75,0.75,3)
ofx_host_test(sequence $<TARGET_FILE:example-Basic-support>
  --frames 0-2 --param scale@0=1 --param scale@2=3 --fill 0.25,0.25,0.25,1
  --expect 0:4,4,0.25,0.25,0.25,1 --expect 1:4,4,0.5,0.5,0.5,1
  --expect 2:4,4,0.75,0.75,0.75,1)
ofx_host_test(no-animation-ignores-key $<TARGET_FILE:example-ChoiceParams-support>
  --param red_choice@2=1 --frames 0-2 --fill 0.5,0.5,0.5,1
  --expect 0:4,4,0.25,0,0,1 --expect 2:4,4,0.25,0,0,1)

# Overlay interacts (OFX 1.5 Draw suite): the DrawSuite example's crosshair,
# and CppGain's, written on the openfx-cpp plugin bindings. A change to a
# slaved parameter makes the host redraw the overlay.
ofx_host_test(interact-draw $<TARGET_FILE:example-DrawSuite>
  --size 64x64 --interact --draw --expect-draws >0)
ofx_host_test(interact-pen-drag $<TARGET_FILE:example-DrawSuite>
  --size 64x64 --interact --pen down 32,32 --pen move 40,44 --pen up 40,44
  --draw --expect-draws >0 --expect-param point=40,44)
ofx_host_test(interact-keys-focus $<TARGET_FILE:example-DrawSuite>
  --size 32x32 --interact --focus in --key down Escape --key up Escape
  --focus out --draw --expect-draws >0)
ofx_host_test(interact-cppgain $<TARGET_FILE:example-CppGain>
  --size 32x32 --interact --pen down 16,16 --pen move 20,24 --pen up 20,24
  --draw --expect-draws 3 --expect-param centre=20,24 --param gain=2,2,2,1
  --fill 0.25,0.25,0.25,1 --expect 5,5,0.5,0.5,0.5,1)
ofx_host_test(interact-slaved-param $<TARGET_FILE:example-CppGain>
  --size 32x32 --interact --param centre=8,8 --expect-draws 3)
