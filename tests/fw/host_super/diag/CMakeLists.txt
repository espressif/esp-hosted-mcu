# SPDX-License-Identifier: Apache-2.0
# Diagnostics super host: the mcu_host scenarios plus heap poisoning/tracing, a DEBUG log ceiling
# and the host CLI (rf_cert), for the tests that ask for that instrumentation on purpose.

cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(eh_host_super_diag)
