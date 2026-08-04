# Copyright 2020 Free Software Foundation, Inc.
#
# This file is part of GNU Radio
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

########################################################################
# Check if there is C++ code at all
########################################################################
if(NOT krakensdr_v2_sources)
    message(STATUS "No C++ sources... skipping python bindings")
    return()
endif(NOT krakensdr_v2_sources)

########################################################################
# Check for pygccxml
########################################################################
gr_python_check_module_raw("pygccxml" "import pygccxml" PYGCCXML_FOUND)

include(GrPybind)

########################################################################
# Python Bindings
########################################################################

list(APPEND krakensdr_v2_python_files
    krakensdr_source_python.cc
    doa_music_python.cc
    correlator_python.cc
    python_bindings.cc)

gr_pybind_make_oot(krakensdr_v2 ../../.. gr::krakensdr_v2 "${krakensdr_v2_python_files}")

# copy bindings extension for use in QA test module
add_custom_command(
    TARGET krakensdr_v2_python
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:krakensdr_v2_python>
            ${PROJECT_BINARY_DIR}/test_modules/gnuradio/krakensdr_v2/)

install(
    TARGETS krakensdr_v2_python
    DESTINATION ${GR_PYTHON_DIR}/gnuradio/krakensdr_v2
    COMPONENT pythonapi)
