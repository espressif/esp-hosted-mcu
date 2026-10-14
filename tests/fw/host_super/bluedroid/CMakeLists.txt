# SPDX-License-Identifier: Apache-2.0
# Test-owned super host: each example's own main/ linked as a scenario component; one runs per boot.

cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(eh_host_super_bluedroid)
