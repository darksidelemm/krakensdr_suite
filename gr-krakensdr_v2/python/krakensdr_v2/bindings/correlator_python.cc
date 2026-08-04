/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

#include <gnuradio/krakensdr_v2/correlator.h>

void bind_correlator(py::module& m)
{
    using correlator = ::gr::krakensdr_v2::correlator;

    py::class_<correlator,
               gr::sync_block,
               gr::block,
               gr::basic_block,
               std::shared_ptr<correlator>>(
        m, "correlator", "FFT-based cross-correlator for coherence checking")

        .def(py::init(&correlator::make),
             py::arg("vec_len"),
             py::arg("fft_cut") = 2048,
             "Create a cross-correlator");
}
