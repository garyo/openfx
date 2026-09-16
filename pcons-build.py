#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["pcons>=0.29"]
# ///
# Copyright OpenFX and contributors to the OpenFX project.
# SPDX-License-Identifier: BSD-3-Clause
"""pcons build for OpenFX: the OfxHost and OfxSupport libraries, and
optionally every example and Support plugin as an installable .ofx.bundle.

This is a developer convenience alongside the canonical CMake build
(see install.md). Output goes under build/pcons/<variant>/, so it never
collides with CMake's build/<Variant>/ tree.

Usage:
    uvx pcons                        # OfxHost + OfxSupport (release)
    uvx pcons --variant=debug        # debug build
    uvx pcons BUILD_PLUGINS=1        # also build all plugin bundles
    uvx pcons BUILD_PLUGINS=1 install   # copy bundles to the system plugin dir
    uvx pcons run gen-props          # regenerate the property metadata headers/docs
    uvx pcons explain                # show every target and where its flags came from

Variables (VAR=value on the command line; each persists per build dir):
    BUILD_PLUGINS     build every plugin bundle (default: false)
    BUILD_UNIVERSAL   macOS x86_64 + arm64 binaries (default: false)
    BUILD_OPENCL      OpenCL support in GPUGain (default: false)
    BUILD_CUDA        CUDA support in GPUGain; needs nvcc (default: false)
    PLUGIN_INSTALLDIR where `install` puts the bundles
                      (default: the OS plugin directory, as CMake uses)

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

# The CLI defaults the build dir to build/, which CMake also uses; keep the
# pcons tree in its own subdirectory, one per variant.
build_root = Path(os.environ.get("PCONS_BUILD_DIR", "build"))
project = Project("openfx", build_dir=build_root / "pcons" / VARIANT)
root = project.root_dir

env = project.Environment(toolchain="c")
env.set_variant(VARIANT)
env.cxx.set_standard("c++17")
env.cxx.defines.append("_HAS_AUTO_PTR_ETC")
env.cxx.defines.append("OFX_SUPPORTS_OPENGLRENDER")
if VARIANT == "debug":
    env.cxx.defines.append("DEBUG")
if not platform.is_windows:
    env.cxx.flags.extend(["-Wall", "-Wextra", "-Wno-deprecated", "-Wno-deprecated-declarations", "-fPIC"])
if platform.is_macos and BUILD_UNIVERSAL:
    env.cxx.flags.extend(["-arch", "x86_64", "-arch", "arm64"])
    env.link.flags.extend(["-arch", "x86_64", "-arch", "arm64"])

env.cxx.includes.extend([
    root / "include",
    root / "Support" / "include",
    root / "HostSupport" / "include",
    root / "openfx-cpp" / "include",
])

# ---------------------------------------------------------------------------
# Conan dependencies (conanfile.py): expat for OfxHost; with BUILD_PLUGINS,
# also the example-only packages behind the conanfile's build_examples option.
# ---------------------------------------------------------------------------

conan = ConanFinder(conanfile=root / "conanfile.py", output_folder=project.build_dir / "conan")
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

ofxhost = project.StaticLibrary("OfxHost", env, sources=sorted((root / "HostSupport" / "src").glob("*.cpp")))
ofxhost.public.include_dirs.extend([root / "include", root / "HostSupport" / "include"])
ofxhost.link(expat)

ofxsupport = project.StaticLibrary("OfxSupport", env, sources=sorted((root / "Support" / "Library").glob("*.cpp")))
ofxsupport.public.include_dirs.extend([root / "include", root / "Support" / "include"])

# ---------------------------------------------------------------------------
# Plugins
# ---------------------------------------------------------------------------

PLIST_TEMPLATE = root / "Examples" / "Info.plist.in"
project.add_configure_dependency(PLIST_TEMPLATE)


def ofx_plugin(target: str, source_dir: Path, *, env, link=(), sources=None):
    """Build one plugin as <build>/plugins/<target>.ofx.bundle.

    Returns (plugin, bundle, plist): the bundle target stands for the whole
    bundle; the plist is reused by the system install."""
    plugin = project.SharedLibrary(target, env, sources=sources or sorted(source_dir.glob("*.cpp")))
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
        project, env, plugin,
        bundle_dir=Path("plugins") / f"{target}.ofx.bundle",
        info_plist=plist, arch_subdir=ARCH_DIR,
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
    for name in ["Basic", "ChoiceParams", "DepthConverter", "DrawSuite", "Invert", "Rectangle", "Test"]:
        plugins.append(ofx_plugin(f"example-{name}", examples_dir / name, env=ex_env))
    for name in ["Custom", "OpenGL"]:
        plugins.append(ofx_plugin(f"example-{name}", examples_dir / name, env=ex_gl_env))

    cimg, spdlog = optional_package("CImg"), optional_package("spdlog")
    if cimg and spdlog:
        plugins.append(ofx_plugin("example-ColourSpace", examples_dir / "ColourSpace", env=ex_env, link=[cimg, spdlog]))
    else:
        print("Skipping example-ColourSpace: CImg and spdlog not available from Conan")

    # Support plugins: the same list as Support/Plugins/CMakeLists.txt.
    sp_env = plugin_env(env, opengl=True)
    sp_env.cxx.includes.append(plugins_dir / "include")
    for name in ["Basic", "ChoiceParams", "Field", "Generator", "Invert",
                 "MultiBundle", "Retimer", "Tester", "Transition"]:
        plugins.append(ofx_plugin(f"support-{name}", plugins_dir / name, env=sp_env, link=[ofxsupport]))

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
    plugins.append(ofx_plugin("support-GPUGain", gpu_dir, env=gpu_env, link=[ofxsupport], sources=gpu_sources))

    plugins.append(ofx_plugin("example-PropTester", support_dir / "PropTester", env=sp_env, link=[ofxsupport]))

    # `install`: the same bundles again, into the system plugin directory.
    install_dir = Path(PLUGIN_INSTALLDIR or DEFAULT_INSTALLDIR)
    installed = [
        create_macos_bundle(project, env, plugin, bundle_dir=install_dir / f"{plugin.name}.ofx.bundle",
                            info_plist=plist, arch_subdir=ARCH_DIR)
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
        subprocess.run(["uv", "run", str(root / "scripts" / script), "-v"], cwd=root, check=True)
