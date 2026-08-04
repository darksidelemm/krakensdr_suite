/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

#include <gnuradio/krakensdr_v2/krakensdr_source.h>

void bind_krakensdr_source(py::module& m)
{
    using krakensdr_source = ::gr::krakensdr_v2::krakensdr_source;

    py::class_<krakensdr_source,
               gr::sync_block,
               gr::block,
               gr::basic_block,
               std::shared_ptr<krakensdr_source>>(
        m, "krakensdr_source", "Coherent multi-channel source for Heimdall v2")

        .def(py::init(&krakensdr_source::make),
             py::arg("ip_addr") = "127.0.0.1",
             py::arg("data_port") = 8091,
             py::arg("ctrl_port") = 8092,
             py::arg("num_channels") = 5,
             py::arg("freq_mhz") = 100.0,
             py::arg("gain_db") = 40.2,
             py::arg("coherent_only") = true,
             py::arg("dc_correction") = true,
             py::arg("debug") = false,
             "Connect to a heimdall_v2 server and stream coherent channels")

        .def("set_freq",
             &krakensdr_source::set_freq,
             py::arg("freq_mhz"),
             "Retune the server (MHz); triggers cooldown + phase recalibration")
        .def("set_gain",
             &krakensdr_source::set_gain,
             py::arg("gain_db"),
             "Set tuner gain in dB on all channels (negative = AGC)")
        .def("phase_state",
             &krakensdr_source::phase_state,
             "Phase compensation state from the last packet (4 = CONVERGED)")
        .def("noise_source_active",
             &krakensdr_source::noise_source_active,
             "True if the last packet carried calibration noise")
        .def("center_freq_hz",
             &krakensdr_source::center_freq_hz,
             "Center frequency in Hz from the last packet's metadata");
}
