"""
sim_extra.py – PlatformIO pre-build script for the native_sim environment.
Prepends the project-root sim/ directory to the C/C++ include search path so
that <Arduino.h> and <LovyanGFX.hpp> resolve to the simulator stubs instead of
system or library headers.
"""
Import("env")  # noqa: F821  (SCons injects this)
import os

project_dir = env.subst("$PROJECT_DIR")  # absolute path, handles spaces
sim_dir     = os.path.join(project_dir, "sim")

# Prepend so the sim stubs take priority over any system headers.
env.Prepend(CPPPATH=[sim_dir])
