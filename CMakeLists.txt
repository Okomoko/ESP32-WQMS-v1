# Root CMakeLists.txt for Water Quality Monitoring System
# ESP-IDF v6.0.1 project

# Minimum CMake version required
cmake_minimum_required(VERSION 3.16)

# Include IDF build system functions
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define project name (must match the directory name or any name)
# This creates the executable and handles all build rules
project(water_quality_monitor)