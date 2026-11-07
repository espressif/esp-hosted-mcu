# SPDX-License-Identifier: Apache-2.0
# Test host: boot auto-init on, a sized .bss pad, reports where the transport task stacks live.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(eh_host_stack_place)
