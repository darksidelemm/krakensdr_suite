/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

#include <gnuradio/krakensdr_v2/doa_music.h>

void bind_doa_music(py::module& m)
{
    using doa_music = ::gr::krakensdr_v2::doa_music;

    py::class_<doa_music,
               gr::sync_block,
               gr::block,
               gr::basic_block,
               std::shared_ptr<doa_music>>(
        m, "doa_music", "MUSIC direction-of-arrival estimator (UCA/ULA)")

        .def(py::init(&doa_music::make),
             py::arg("vec_len"),
             py::arg("freq_mhz"),
             py::arg("array_dist_m") = 0.25,
             py::arg("num_elements") = 5,
             py::arg("array_type") = "UCA",
             py::arg("signal_dimension") = 1,
             "Create a MUSIC DoA estimator")

        .def("set_freq",
             &doa_music::set_freq,
             py::arg("freq_mhz"),
             "Update the wavelength frequency (MHz); regenerates steering vectors")

        .def("set_array_dist",
             &doa_music::set_array_dist,
             py::arg("array_dist_m"),
             "Update the UCA radius / ULA spacing (m); regenerates steering vectors")

        .def("set_array_type",
             &doa_music::set_array_type,
             py::arg("array_type"),
             "Switch between \"UCA\" and \"ULA\"; regenerates steering vectors");
}
