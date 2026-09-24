# /// script
# requires-python = ">=3.11"
# dependencies = ["pcons>=0.29"]
# ///
# Copyright OpenFX and contributors to the OpenFX project.
# SPDX-License-Identifier: BSD-3-Clause
"""pcons build for OpenFX: the OfxHost and OfxSupport libraries, the
ofxtesthost test host, and optionally every example and Support plugin as an
installable .ofx.bundle, with tests that run the host against them.

This is a developer convenience alongside the canonical CMake build
(see install.md). Output goes under build/pcons/<variant>/, so it never
collides with CMake's build/<Variant>/ tree.

Usage:
    uvx pcons                        # OfxHost + OfxSupport + ofxtesthost (release)
    uvx pcons --variant=debug        # debug build
    uvx pcons BUILD_PLUGINS=1        # also build all plugin bundles
    uvx pcons -B build/pcons/release test   # run the test host against them
    uvx pcons BUILD_PLUGINS=1 install   # copy bundles to the system plugin dir
    uvx pcons run gen-props          # regenerate the property metadata headers/docs
    uvx pcons explain                # show every target and where its flags came from

`pcons test` looks for its manifest in the CLI's build directory, so point it
at the variant's directory with -B (which is also fine for building).

Variables (VAR=value on the command line; each persists per build dir):
    BUILD_PLUGINS     build every plugin bundle (default: false)
    BUILD_UNIVERSAL   macOS x86_64 + arm64 binaries (default: false)
    BUILD_OPENCL      OpenCL support in GPUGain (default: false)
    BUILD_CUDA        CUDA support in GPUGain; needs nvcc (default: false)
    PLUGIN_INSTALLDIR where `install` puts the bundles
                      (default: the OS plugin directory, as CMake uses)
    SANITIZE          build everything with AddressSanitizer and
                      UndefinedBehaviorSanitizer (default: false)
    CLANG_TIDY        run clang-tidy alongside every compile, with the checks
                      in .clang-tidy; findings are reported, not fatal
                      (default: false)

Use a build directory of its own for the last two, e.g.
`uvx pcons -B build/pcons/tidy CLANG_TIDY=1`.

Bundles are laid out exactly as the CMake build lays them out (same
target names, arch directory and Info.plist), so the two builds install
interchangeable plugin sets. Built bundles live in <build>/plugins/, which
can be used directly as an OFX_PLUGIN_PATH entry for testing.
"""

import os
import subprocess
from pathlib import Path
from string import Template

from pcons import Project, get_platform, get_var, get_variant, write_file
from pcons.contrib.bundle import create_macos_bundle
from pcons.packages.finders import ConanFinder

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

VARIANT = get_variant("release")
BUILD_PLUGINS = get_var("BUILD_PLUGINS", False)
BUILD_UNIVERSAL = get_var("BUILD_UNIVERSAL", False)
BUILD_OPENCL = get_var("BUILD_OPENCL", False)
BUILD_CUDA = get_var("BUILD_CUDA", False)
PLUGIN_INSTALLDIR = get_var("PLUGIN_INSTALLDIR", "")
SANITIZE = get_var("SANITIZE", False)
CLANG_TIDY = get_var("CLANG_TIDY", False)

platform = get_platform()

# Same names the CMake build uses (cmake/OpenFX.cmake).
if platform.is_macos:
    ARCH_DIR = "MacOS"
    DEFAULT_INSTALLDIR = "/Library/OFX/Plugins/OpenFX Examples"
elif platform.is_windows:
    ARCH_DIR = "Win64"
    DEFAULT_INSTALLDIR = "C:/Program Files/Common Files/OFX/Plugins/OpenFX Examples"
else:
    ARCH_DIR = "Linux-x86-64"
    DEFAULT_INSTALLDIR = "/usr/OFX/Plugins/OpenFX Examples"

# ---------------------------------------------------------------------------
# Project and environment
# ---------------------------------------------------------------------------

# The CLI defaults the build dir to build/, which CMake also uses, so by default
# the pcons tree goes in its own subdirectory, one per variant. An explicit
# `-B DIR` is used as given.
script_dir = Path(__file__).parent
build_dir = Path(os.environ.get("PCONS_BUILD_DIR", script_dir / "build"))
if build_dir.resolve() == (script_dir / "build").resolve():
    build_dir = build_dir / "pcons" / VARIANT
project = Project("openfx", build_dir=build_dir)
root = project.root_dir

env = project.Environment(toolchain="c")
env.set_variant(VARIANT)
env.cxx.set_standard("c++17")
env.cxx.defines.append("_HAS_AUTO_PTR_ETC")
env.cxx.defines.append("OFX_SUPPORTS_OPENGLRENDER")
if VARIANT == "debug":
    env.cxx.defines.append("DEBUG")
if SANITIZE:
    env.apply_preset("sanitize")
if CLANG_TIDY:
    env.use_clang_tidy()
if not platform.is_windows:
    env.cxx.flags.extend(
        ["-Wall", "-Wextra", "-Wno-deprecated", "-Wno-deprecated-declarations", "-fPIC"]
    )
if platform.is_macos and BUILD_UNIVERSAL:
    env.cxx.flags.extend(["-arch", "x86_64", "-arch", "arm64"])
    env.link.flags.extend(["-arch", "x86_64", "-arch", "arm64"])

env.cxx.includes.extend(
    [
        root / "include",
        root / "Support" / "include",
        root / "HostSupport" / "include",
        root / "openfx-cpp" / "include",
    ]
)

# ---------------------------------------------------------------------------
# Conan dependencies (conanfile.py): expat for OfxHost; with BUILD_PLUGINS,
# also the example-only packages behind the conanfile's build_examples option.
# ---------------------------------------------------------------------------

conan = ConanFinder(
    conanfile=root / "conanfile.py", output_folder=project.build_dir / "conan"
)
profile = conan.sync_profile(env.toolchain, env=env, cppstd="17")
if BUILD_PLUGINS:
    # ConanFinder has no option hook; a profile [options] section does the job.
    profile.write_text(profile.read_text() + "\n[options]\n&:build_examples=True\n")
conan.install()
project.add_package_finder(conan)

expat = project.find_package("expat")


def optional_package(name: str):
    return project.find_package(name, required=False)


# ---------------------------------------------------------------------------
# Libraries
# ---------------------------------------------------------------------------

ofxhost = project.StaticLibrary(
    "OfxHost", env, sources=sorted((root / "HostSupport" / "src").glob("*.cpp"))
)
ofxhost.public.include_dirs.extend([root / "include", root / "HostSupport" / "include"])
ofxhost.link(expat)

ofxsupport = project.StaticLibrary(
    "OfxSupport", env, sources=sorted((root / "Support" / "Library").glob("*.cpp"))
)
ofxsupport.public.include_dirs.extend([root / "include", root / "Support" / "include"])

# ---------------------------------------------------------------------------
# Test host (TestHost/): C++20, for std::span in the openfx-cpp headers.
# ---------------------------------------------------------------------------

host_env = env.clone()
host_env.cxx.set_standard("c++20")
testhost = project.Program("ofxtesthost", host_env, sources=sorted((root / "TestHost" / "src").glob("*.cpp")))

# ---------------------------------------------------------------------------
# Unit tests for the openfx-cpp bindings (openfx-cpp/tests/): the plugin-side
# wrappers run against the host-side objects in process, so these need no
# plugin bundles and build whenever the host does. One `pcons test` entry per
# source file; the program takes the file name as a filter.
# ---------------------------------------------------------------------------

tests_dir = root / "openfx-cpp" / "tests"
tests_env = host_env.clone()
tests_env.cxx.includes.append(tests_dir)
cpptests = project.Program(
    "openfx-cpp-tests", tests_env, sources=sorted(tests_dir.glob("*.cpp"))
)
for source in sorted(tests_dir.glob("test_*.cpp")):
    project.Test(
        f"openfx-cpp.{source.stem.removeprefix('test_')}",
        cpptests,
        args=[source.name],
        labels=["openfx-cpp"],
    )

# The same tests at C++17, the standard the bindings promise, where ofxSpan.h
# stands in tcb-span for std::span.
tests17_env = env.clone()
tests17_env.cxx.set_standard("c++17")
tests17_env.cxx.includes.append(tests_dir)
cpptests17 = project.Program(
    "openfx-cpp-tests-cxx17", tests17_env, sources=sorted(tests_dir.glob("*.cpp"))
)
cpptests17.link(project.find_package("tcb-span"))
project.Test("openfx-cpp.cxx17", cpptests17, labels=["openfx-cpp"])

# ---------------------------------------------------------------------------
# Plugins
# ---------------------------------------------------------------------------

PLIST_TEMPLATE = root / "Examples" / "Info.plist.in"
project.add_configure_dependency(PLIST_TEMPLATE)


def ofx_plugin(target: str, source_dir: Path, *, env, link=(), sources=None):
    """Build one plugin as <build>/plugins/<target>.ofx.bundle.

    Returns (plugin, bundle, plist): the bundle target stands for the whole
    bundle; the plist is reused by the system install."""
    plugin = project.SharedLibrary(
        target, env, sources=sources or sorted(source_dir.glob("*.cpp"))
    )
    plugin.output_prefix = ""
    plugin.output_suffix = ".ofx"
    for lib in link:
        plugin.link(lib)

    plist = write_file(
        project.build_dir / "plists" / target / "Info.plist",
        Template(PLIST_TEMPLATE.read_text()).substitute(
            PLUGIN_NAME=target,
            PLUGIN_EXE=f"{target}.ofx",
            BUNDLE_IDENTIFIER=f"org.openeffects.plugins.{target}",
            BUNDLE_SIGNATURE="????",
            PLUGIN_VERSION="1.0.0",
        ),
    )
    bundle = create_macos_bundle(
        project,
        env,
        plugin,
        bundle_dir=Path("plugins") / f"{target}.ofx.bundle",
        info_plist=plist,
        arch_subdir=ARCH_DIR,
    )
    bundle.build_tier = "default"
    return plugin, bundle, plist


def plugin_env(base, *, opengl=False, opencl=False):
    e = base.clone()
    if not platform.is_windows:
        e.cxx.flags.append("-fvisibility=hidden")
    if opengl:
        if platform.is_macos:
            e.Framework("OpenGL")
        else:
            e.link.libs.append("opengl32" if platform.is_windows else "GL")
    if opencl:
        if platform.is_macos:
            e.Framework("OpenCL")
        else:
            e.link.libs.append("OpenCL")
    return e


plugins = []

if BUILD_PLUGINS:
    examples_dir = root / "Examples"
    support_dir = root / "Support"
    plugins_dir = support_dir / "Plugins"

    ex_env = plugin_env(env)
    ex_env.cxx.includes.append(examples_dir / "include")
    ex_gl_env = plugin_env(env, opengl=True)
    ex_gl_env.cxx.includes.append(examples_dir / "include")

    # Examples: the same list as Examples/CMakeLists.txt.
    for name in [
        "Basic",
        "ChoiceParams",
        "DepthConverter",
        "DrawSuite",
        "Invert",
        "Rectangle",
        "Test",
    ]:
        plugins.append(ofx_plugin(f"example-{name}", examples_dir / name, env=ex_env))
    for name in ["Custom", "OpenGL"]:
        plugins.append(
            ofx_plugin(f"example-{name}", examples_dir / name, env=ex_gl_env)
        )

    # TestProps: a property-compliance test plugin on openfx-cpp (C++20 for
    # std::span), which also compiles the host-specific-props example header.
    tp_env = plugin_env(env)
    tp_env.cxx.set_standard("c++20")
    tp_env.cxx.includes.append(root / "openfx-cpp" / "examples" / "host-specific-props")
    plugins.append(ofx_plugin("example-TestProps", examples_dir / "TestProps", env=tp_env))

    # CppGain: a gain/offset filter written entirely on the openfx-cpp plugin
    # bindings (C++20 for std::span, like TestProps).
    cg_env = plugin_env(env)
    cg_env.cxx.set_standard("c++20")
    plugins.append(ofx_plugin("example-CppGain", examples_dir / "CppGain", env=cg_env))

    cimg, spdlog = optional_package("CImg"), optional_package("spdlog")
    have_colourspace = bool(cimg and spdlog)
    if cimg and spdlog:
        plugins.append(
            ofx_plugin(
                "example-ColourSpace",
                examples_dir / "ColourSpace",
                env=ex_env,
                link=[cimg, spdlog],
            )
        )
    else:
        print("Skipping example-ColourSpace: CImg and spdlog not available from Conan")

    # Support plugins: the same list as Support/Plugins/CMakeLists.txt.
    sp_env = plugin_env(env, opengl=True)
    sp_env.cxx.includes.append(plugins_dir / "include")
    for name in [
        "Basic",
        "ChoiceParams",
        "Field",
        "Generator",
        "Invert",
        "MultiBundle",
        "Retimer",
        "Tester",
        "Transition",
    ]:
        plugins.append(
            ofx_plugin(
                f"support-{name}", plugins_dir / name, env=sp_env, link=[ofxsupport]
            )
        )

    # GPUGain: Metal on macOS; OpenCL and CUDA opt-in.
    gpu_dir = plugins_dir / "GPUGain"
    gpu_env = plugin_env(env, opengl=True, opencl=BUILD_OPENCL)
    gpu_env.cxx.includes.append(plugins_dir / "include")
    gpu_sources = sorted(gpu_dir.glob("*.cpp"))
    if BUILD_OPENCL:
        gpu_env.cxx.defines.append("OFX_SUPPORTS_OPENCLRENDER")
    else:
        gpu_sources = [s for s in gpu_sources if "OpenCLKernel" not in s.name]
    if platform.is_macos:
        gpu_sources += sorted(gpu_dir.glob("*.mm"))
        for fw in ["Metal", "Foundation", "QuartzCore"]:
            gpu_env.Framework(fw)
    elif BUILD_CUDA:
        gpu_env.add_toolchain("cuda")
        gpu_env.cxx.defines.append("OFX_SUPPORTS_CUDARENDER")
        gpu_sources += sorted(gpu_dir.glob("*.cu"))
    plugins.append(
        ofx_plugin(
            "support-GPUGain",
            gpu_dir,
            env=gpu_env,
            link=[ofxsupport],
            sources=gpu_sources,
        )
    )

    plugins.append(
        ofx_plugin(
            "example-PropTester",
            support_dir / "PropTester",
            env=sp_env,
            link=[ofxsupport],
        )
    )

    # Tests: the test host driving the plugins just built (`pcons test`).
    # Tests run in the build directory, so the bundle paths are relative to it.
    bundle_dir = Path("plugins")

    def host_test(name, *args):
        project.Test(f"host.{name}", testhost, args=[str(a) for a in args], labels=["host"])

    def bundle(target):
        return bundle_dir / f"{target}.ofx.bundle"

    host_test("list", bundle_dir, "--list")
    host_test("invert", bundle("example-Invert"), "--fill", "0.25,0.5,0.75,1", "--expect", "3,3,0.75,0.5,0.25,0")
    host_test("gain-param", bundle("example-Basic"), "--param", "scale=2", "--fill", "0.25,0.25,0.25,1",
              "--expect", "5,5,0.5,0.5,0.5,2")
    host_test("support-invert", bundle("support-Invert"), "--fill", "0.25,0.5,0.75,1", "--expect", "3,3,0.75,0.5,0.25,0")
    host_test("chain", bundle_dir, "--plugin", "net.sf.openfx.basicPlugin", "--param", "scale=2",
              "--plugin", "net.sf.openfx.invertPlugin", "--fill", "0.25,0.25,0.25,1", "--expect", "10,10,0.5,0.5,0.5,0")
    host_test("chain-roundtrip", bundle_dir, "--plugin", "uk.co.thefoundry.OfxInvertExample",
              "--plugin", "uk.co.thefoundry.OfxInvertExample", "--fill", "0.25,0.5,0.75,1", "--expect", "3,3,0.25,0.5,0.75,1")
    host_test("generator", bundle("example-Rectangle"), "--context", "OfxImageEffectContextGenerator",
              "--param", "colour=1,0,0,1", "--size", "32x32", "--expect", "16,16,1,0,0,1")
    host_test("generator-infinite-rod", bundle("support-Generator"), "--size", "32x32")
    host_test("rectangle-over-input", bundle("example-Rectangle"), "--param", "colour=0,1,0,1", "--param", "corner1=8,8",
              "--param", "corner2=24,24", "--size", "32x32", "--expect", "16,16,0,1,0,1", "--expect", "2,2,0.0645,0.0645,0.5,1")
    host_test("choice-params", bundle("support-ChoiceParams"), "--param", "red_choice=1", "--param", "green_choice=1",
              "--param", "blue_choice=blue_0.5", "--fill", "0.5,0.5,0.5,1", "--expect", "4,4,0.25,0.5,0.25,1")
    host_test("strchoice-unknown-uses-default", bundle("support-ChoiceParams"), "--param", "blue_choice=not-an-option",
              "--fill", "0.5,0.5,0.5,1", "--expect", "4,4,0,0,0,1")
    host_test("clip-prefs-identity", bundle("example-DepthConverter"), "--param", "depth=2", "--fill", "0.25,0.5,0.75,1",
              "--expect", "4,4,0.25,0.5,0.75,1")
    host_test("many-param-types", bundle("support-Tester"), "--describe", "--fill", "0.5,0.5,0.5,1")
    host_test("props-compliance", bundle("example-TestProps"), "--param", "scale=2", "--fill", "0.5,0.5,0.5,1")
    host_test("cppgain", bundle("example-CppGain"), "--param", "gain=2,2,2,1", "--fill", "0.25,0.25,0.25,1",
              "--expect", "5,5,0.5,0.5,0.5,1")
    host_test("cppgain-byte-rgb", bundle("example-CppGain"), "--depth", "Byte", "--components", "RGB",
              "--param", "gain=2,2,2,1", "--param", "offset=0.25", "--fill", "0.25,0.25,0.25,1",
              "--expect", "5,5,0.7529,0.7529,0.7529,1")
    host_test("mask-clip", bundle("support-Basic"), "--context", "OfxImageEffectContextGeneral", "--param", "scale=2",
              "--clip", "Mask=fill:0,0,0,0.5", "--fill", "0.25,0.25,0.25,1", "--expect", "5,5,0.375,0.375,0.375,1")

    # The actions the host asks about a frame before rendering it, and the ones
    # that bracket an edit. The Support library answers RoI and FramesNeeded
    # from its base class, so these plugins exercise both sides.
    host_test("roi-frames-needed", bundle("support-Basic"), "--context", "OfxImageEffectContextGeneral",
              "--param", "scale=2", "--verbose", "--fill", "0.25,0.25,0.25,1", "--expect", "5,5,0.5,0.5,0.5,1")
    host_test("roi-frames-needed-filter", bundle("support-Invert"), "--verbose",
              "--fill", "0.25,0.5,0.75,1", "--expect", "3,3,0.75,0.5,0.25,0")
    host_test("time-domain", bundle("support-Generator"), "--context", "OfxImageEffectContextGenerator",
              "--size", "32x32", "--verbose")

    # Colour management (OFX 1.5). CppGain declares the basic style and answers
    # GetOutputColourspace; gain is otherwise unaffected by it.
    host_test("cppgain-colour-basic", bundle("example-CppGain"), "--colour-management", "basic", "--verbose",
              "--param", "gain=2,2,2,1", "--fill", "0.25,0.25,0.25,1", "--expect", "5,5,0.5,0.5,0.5,1")
    # A basic-style plugin under a core-style host: the negotiation falls to
    # basic, so the host names the basic colourspace standing for ACEScct.
    host_test("cppgain-colour-core-host", bundle("example-CppGain"), "--colour-management", "core",
              "--colourspace", "ACEScct", "--verbose",
              "--param", "gain=2,2,2,1", "--fill", "0.25,0.25,0.25,1", "--expect", "5,5,0.5,0.5,0.5,1")
    if have_colourspace:
        # The ColourSpace examples copy the source and label it, and the label
        # is drawn from x=100 rightwards, so a narrow frame is a plain copy.
        for style, plugin in [("basic", "Basic"), ("core", "Core"), ("core", "Full")]:
            host_test(
                f"colour-{style}-{plugin.lower()}",
                bundle("example-ColourSpace"),
                "--plugin", f"io.aswf.openfx.example.ColourspacePlugin{plugin}",
                "--colour-management", style, "--verbose", "--size", "64x200",
                "--fill", "0.25,0.5,0.75,1", "--expect", "5,5,0.25,0.5,0.75,1",
            )
        # A core-style plugin under a basic-style host falls back to basic.
        host_test("colour-core-plugin-basic-host", bundle("example-ColourSpace"),
                  "--plugin", "io.aswf.openfx.example.ColourspacePluginCore",
                  "--colour-management", "basic", "--verbose", "--size", "64x200",
                  "--fill", "0.25,0.5,0.75,1", "--expect", "5,5,0.25,0.5,0.75,1")
        # The host names the input colourspace and the plugin answers with one
        # of its own, which must still be a colourspace the core style offers.
        host_test("colour-core-colourspace", bundle("example-ColourSpace"),
                  "--plugin", "io.aswf.openfx.example.ColourspacePluginCore",
                  "--colour-management", "core", "--colourspace", "ACEScct", "--verbose",
                  "--param", "output_colourspace=ACEScg", "--size", "64x200",
                  "--fill", "0.25,0.5,0.75,1", "--expect", "5,5,0.25,0.5,0.75,1")

    # Tiles: several Render actions per frame, each seeing only its own window.
    # --check-tiles also renders the frame whole and warns if the two differ.
    host_test("tiles-gain", bundle("example-Basic"), "--tiles", "3", "--check-tiles", "--param", "scale=2",
              "--fill", "0.25,0.25,0.25,1", "--expect", "5,5,0.5,0.5,0.5,2")
    host_test("tiles-invert", bundle("support-Invert"), "--tiles", "3", "--check-tiles",
              "--fill", "0.25,0.5,0.75,1", "--expect", "3,3,0.75,0.5,0.25,0")
    host_test("tiles-fixed-size", bundle("example-Rectangle"), "--tile", "7x5", "--check-tiles",
              "--param", "colour=0,1,0,1", "--param", "corner1=8,8", "--param", "corner2=24,24", "--size", "32x32",
              "--expect", "16,16,0,1,0,1", "--expect", "2,2,0.0645,0.0645,0.5,1")
    # Render scale: the rectangle's canonical corners land at half the pixel coordinates.
    host_test("render-scale-generator", bundle("example-Rectangle"), "--context", "OfxImageEffectContextGenerator",
              "--render-scale", "0.5", "--param", "colour=1,0,0,1", "--param", "corner1=8,8", "--param", "corner2=24,24",
              "--size", "32x32", "--expect", "4,4,1,0,0,1", "--expect", "6,6,1,0,0,1")
    host_test("render-scale-filter", bundle("example-Rectangle"), "--render-scale", "0.5",
              "--param", "colour=0,1,0,1", "--param", "corner1=8,8", "--param", "corner2=24,24", "--size", "32x32",
              "--fill", "0.25,0.25,0.25,1", "--expect", "6,6,0,1,0,1", "--expect", "1,1,0.25,0.25,0.25,1")
    host_test("render-scale-tiled", bundle("example-Rectangle"), "--render-scale", "0.5", "--tiles", "3",
              "--check-tiles", "--param", "colour=0,1,0,1", "--param", "corner1=8,8", "--param", "corner2=24,24",
              "--size", "32x32", "--fill", "0.25,0.25,0.25,1", "--expect", "6,6,0,1,0,1")

    # Animation: a keyed double interpolates between its keys, so each frame differs.
    host_test("animated-param", bundle("example-Basic"), "--param", "scale@0=1", "--param", "scale@4=3",
              "--frames", "0-4", "--fill", "0.25,0.25,0.25,1",
              "--expect", "0:5,5,0.25,0.25,0.25,1", "--expect", "4:5,5,0.75,0.75,0.75,3")
    host_test("sequence", bundle("support-Basic"), "--frames", "0-2", "--param", "scale@0=1", "--param", "scale@2=3",
              "--fill", "0.25,0.25,0.25,1", "--expect", "0:4,4,0.25,0.25,0.25,1",
              "--expect", "1:4,4,0.5,0.5,0.5,1", "--expect", "2:4,4,0.75,0.75,0.75,1")
    # A parameter type that does not animate takes the keyframe as its value.
    host_test("no-animation-ignores-key", bundle("support-ChoiceParams"), "--param", "red_choice@2=1",
              "--frames", "0-2", "--fill", "0.5,0.5,0.5,1", "--expect", "0:4,4,0.25,0,0,1",
              "--expect", "2:4,4,0.25,0,0,1")
    # Overlay interacts (OFX 1.5 Draw suite). The DrawSuite example draws a
    # crosshair for its "point" parameter and drags it with the pen; CppGain's
    # overlay is the same idea written on the openfx-cpp plugin bindings.
    host_test("interact-draw", bundle("example-DrawSuite"), "--size", "64x64",
              "--interact", "--draw", "--expect-draws", ">0")
    host_test("interact-pen-drag", bundle("example-DrawSuite"), "--size", "64x64",
              "--interact", "--pen", "down", "32,32", "--pen", "move", "40,44",
              "--pen", "up", "40,44", "--draw", "--expect-draws", ">0",
              "--expect-param", "point=40,44")
    host_test("interact-keys-focus", bundle("example-DrawSuite"), "--size", "32x32",
              "--interact", "--focus", "in", "--key", "down", "Escape",
              "--key", "up", "Escape", "--focus", "out", "--draw", "--expect-draws", ">0")
    host_test("interact-cppgain", bundle("example-CppGain"), "--size", "32x32",
              "--interact", "--pen", "down", "16,16", "--pen", "move", "20,24",
              "--pen", "up", "20,24", "--draw", "--expect-draws", "3",
              "--expect-param", "centre=20,24", "--param", "gain=2,2,2,1",
              "--fill", "0.25,0.25,0.25,1", "--expect", "5,5,0.5,0.5,0.5,1")
    # A change to a slaved parameter makes the host redraw the overlay.
    host_test("interact-slaved-param", bundle("example-CppGain"), "--size", "32x32",
              "--interact", "--param", "centre=8,8", "--expect-draws", "3")

    # `pcons test` builds the programs under test first; make that pull in the bundles.
    testhost.depends(*[b for _, b, _ in plugins], on_change=False)

    # `install`: the same bundles again, into the system plugin directory.
    install_dir = Path(PLUGIN_INSTALLDIR or DEFAULT_INSTALLDIR)
    installed = [
        create_macos_bundle(
            project,
            env,
            plugin,
            bundle_dir=install_dir / f"{plugin.name}.ofx.bundle",
            info_plist=plist,
            arch_subdir=ARCH_DIR,
        )
        for plugin, _, plist in plugins
    ]
    project.Alias("install", *installed)

# ---------------------------------------------------------------------------
# Property metadata generation: `pcons run gen-props`. The generated headers
# and docs are committed, so this is a deliberate action, not a build step.
# ---------------------------------------------------------------------------


@project.cli_command()
def gen_props() -> None:
    """Regenerate openfx-cpp property headers and the property reference docs from include/*.h."""
    for script in ["gen-props.py", "gen-props-doc.py"]:
        subprocess.run(
            ["uv", "run", str(root / "scripts" / script), "-v"], cwd=root, check=True
        )


# ---------------------------------------------------------------------------
# Formatting: `pcons run format`. Reformats only the hand-written sources of
# the openfx-cpp bindings and their test host/examples.
# ---------------------------------------------------------------------------

GENERATED_HEADERS = {"ofxPropSetAccessors.h", "ofxPropsMetadata.h", "ofxPropsBySet.h"}


@project.cli_command()
def format() -> None:
    """Run clang-format over the openfx-cpp bindings, the test host and their
    example plugins. The rest of the repository is not covered."""
    openfx_cpp = root / "openfx-cpp" / "include" / "openfx"
    sources = [
        p
        for pattern in ["*.h", "plugin/*.h", "host/*.h"]
        for p in sorted(openfx_cpp.glob(pattern))
        if p.name not in GENERATED_HEADERS
    ]
    for pattern in ["TestHost/src/*.h", "TestHost/src/*.cpp", "openfx-cpp/tests/*.h",
                    "openfx-cpp/tests/*.cpp", "Examples/CppGain/*.cpp",
                    "Examples/TestProps/*.cpp"]:
        sources.extend(sorted(root.glob(pattern)))
    subprocess.run(["clang-format", "-i", *[str(p) for p in sources]], cwd=root, check=True)
