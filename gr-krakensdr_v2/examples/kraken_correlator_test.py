#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: KrakenSDR v2 Correlation Check
# Author: KrakenRF Inc.
# Copyright: 2026 KrakenRF Inc.
# Description: KrakenSDR v2 Correlation Check
# GNU Radio version: 3.10.12.0

from PyQt5 import Qt
from gnuradio import qtgui
from gnuradio import blocks
from gnuradio import eng_notation
from gnuradio import gr
from gnuradio.filter import firdes
from gnuradio.fft import window
import sys
import signal
from PyQt5 import Qt
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import krakensdr_v2
import sip
import threading



class kraken_correlator_test(gr.top_block, Qt.QWidget):

    def __init__(self):
        gr.top_block.__init__(self, "KrakenSDR v2 Correlation Check", catch_exceptions=True)
        Qt.QWidget.__init__(self)
        self.setWindowTitle("KrakenSDR v2 Correlation Check")
        qtgui.util.check_set_qss()
        try:
            self.setWindowIcon(Qt.QIcon.fromTheme('gnuradio-grc'))
        except BaseException as exc:
            print(f"Qt GUI: Could not set Icon: {str(exc)}", file=sys.stderr)
        self.top_scroll_layout = Qt.QVBoxLayout()
        self.setLayout(self.top_scroll_layout)
        self.top_scroll = Qt.QScrollArea()
        self.top_scroll.setFrameStyle(Qt.QFrame.NoFrame)
        self.top_scroll_layout.addWidget(self.top_scroll)
        self.top_scroll.setWidgetResizable(True)
        self.top_widget = Qt.QWidget()
        self.top_scroll.setWidget(self.top_widget)
        self.top_layout = Qt.QVBoxLayout(self.top_widget)
        self.top_grid_layout = Qt.QGridLayout()
        self.top_layout.addLayout(self.top_grid_layout)

        self.settings = Qt.QSettings("gnuradio/flowgraphs", "kraken_correlator_test")

        try:
            geometry = self.settings.value("geometry")
            if geometry:
                self.restoreGeometry(geometry)
        except BaseException as exc:
            print(f"Qt GUI: Could not restore geometry: {str(exc)}", file=sys.stderr)
        self.flowgraph_started = threading.Event()

        ##################################################
        # Variables
        ##################################################
        self.vec_len = vec_len = 16384
        self.samp_rate = samp_rate = 2400000
        self.gain = gain = 40.2
        self.freq = freq = 100.0
        self.fft_cut = fft_cut = 4096

        ##################################################
        # Blocks
        ##################################################

        self._gain_tool_bar = Qt.QToolBar(self)
        self._gain_tool_bar.addWidget(Qt.QLabel("Gain [dB] (neg = AGC)" + ": "))
        self._gain_line_edit = Qt.QLineEdit(str(self.gain))
        self._gain_tool_bar.addWidget(self._gain_line_edit)
        self._gain_line_edit.editingFinished.connect(
            lambda: self.set_gain(eng_notation.str_to_num(str(self._gain_line_edit.text()))))
        self.top_grid_layout.addWidget(self._gain_tool_bar, 0, 1, 1, 1)
        for r in range(0, 1):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(1, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._freq_tool_bar = Qt.QToolBar(self)
        self._freq_tool_bar.addWidget(Qt.QLabel("Center Frequency [MHz]" + ": "))
        self._freq_line_edit = Qt.QLineEdit(str(self.freq))
        self._freq_tool_bar.addWidget(self._freq_line_edit)
        self._freq_line_edit.editingFinished.connect(
            lambda: self.set_freq(eng_notation.str_to_num(str(self._freq_line_edit.text()))))
        self.top_grid_layout.addWidget(self._freq_tool_bar, 0, 0, 1, 1)
        for r in range(0, 1):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 1):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.xcorr_sink_04 = qtgui.vector_sink_f(
            fft_cut,
            (-fft_cut//2),
            1.0,
            "Lag [samples]",
            "dB",
            "XCorr CH0xCH4",
            1, # Number of inputs
            None # parent
        )
        self.xcorr_sink_04.set_update_time(0.10)
        self.xcorr_sink_04.set_y_axis((-50), 0)
        self.xcorr_sink_04.enable_autoscale(False)
        self.xcorr_sink_04.enable_grid(True)
        self.xcorr_sink_04.set_x_axis_units("")
        self.xcorr_sink_04.set_y_axis_units("")
        self.xcorr_sink_04.set_ref_level(0)

        self.xcorr_sink_04.disable_legend()

        labels = ['', '', '', '', '',
            '', '', '', '', '']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ["blue", "red", "green", "black", "cyan",
            "magenta", "yellow", "dark red", "dark green", "dark blue"]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.xcorr_sink_04.set_line_label(i, "Data {0}".format(i))
            else:
                self.xcorr_sink_04.set_line_label(i, labels[i])
            self.xcorr_sink_04.set_line_width(i, widths[i])
            self.xcorr_sink_04.set_line_color(i, colors[i])
            self.xcorr_sink_04.set_line_alpha(i, alphas[i])

        self._xcorr_sink_04_win = sip.wrapinstance(self.xcorr_sink_04.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._xcorr_sink_04_win, 4, 0, 1, 1)
        for r in range(4, 5):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 1):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.xcorr_sink_03 = qtgui.vector_sink_f(
            fft_cut,
            (-fft_cut//2),
            1.0,
            "Lag [samples]",
            "dB",
            "XCorr CH0xCH3",
            1, # Number of inputs
            None # parent
        )
        self.xcorr_sink_03.set_update_time(0.10)
        self.xcorr_sink_03.set_y_axis((-50), 0)
        self.xcorr_sink_03.enable_autoscale(False)
        self.xcorr_sink_03.enable_grid(True)
        self.xcorr_sink_03.set_x_axis_units("")
        self.xcorr_sink_03.set_y_axis_units("")
        self.xcorr_sink_03.set_ref_level(0)

        self.xcorr_sink_03.disable_legend()

        labels = ['', '', '', '', '',
            '', '', '', '', '']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ["blue", "red", "green", "black", "cyan",
            "magenta", "yellow", "dark red", "dark green", "dark blue"]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.xcorr_sink_03.set_line_label(i, "Data {0}".format(i))
            else:
                self.xcorr_sink_03.set_line_label(i, labels[i])
            self.xcorr_sink_03.set_line_width(i, widths[i])
            self.xcorr_sink_03.set_line_color(i, colors[i])
            self.xcorr_sink_03.set_line_alpha(i, alphas[i])

        self._xcorr_sink_03_win = sip.wrapinstance(self.xcorr_sink_03.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._xcorr_sink_03_win, 3, 0, 1, 1)
        for r in range(3, 4):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 1):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.xcorr_sink_02 = qtgui.vector_sink_f(
            fft_cut,
            (-fft_cut//2),
            1.0,
            "Lag [samples]",
            "dB",
            "XCorr CH0xCH2",
            1, # Number of inputs
            None # parent
        )
        self.xcorr_sink_02.set_update_time(0.10)
        self.xcorr_sink_02.set_y_axis((-50), 0)
        self.xcorr_sink_02.enable_autoscale(False)
        self.xcorr_sink_02.enable_grid(True)
        self.xcorr_sink_02.set_x_axis_units("")
        self.xcorr_sink_02.set_y_axis_units("")
        self.xcorr_sink_02.set_ref_level(0)

        self.xcorr_sink_02.disable_legend()

        labels = ['', '', '', '', '',
            '', '', '', '', '']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ["blue", "red", "green", "black", "cyan",
            "magenta", "yellow", "dark red", "dark green", "dark blue"]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.xcorr_sink_02.set_line_label(i, "Data {0}".format(i))
            else:
                self.xcorr_sink_02.set_line_label(i, labels[i])
            self.xcorr_sink_02.set_line_width(i, widths[i])
            self.xcorr_sink_02.set_line_color(i, colors[i])
            self.xcorr_sink_02.set_line_alpha(i, alphas[i])

        self._xcorr_sink_02_win = sip.wrapinstance(self.xcorr_sink_02.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._xcorr_sink_02_win, 2, 0, 1, 1)
        for r in range(2, 3):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 1):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.xcorr_sink_01 = qtgui.vector_sink_f(
            fft_cut,
            (-fft_cut//2),
            1.0,
            "Lag [samples]",
            "dB",
            "XCorr CH0xCH1",
            1, # Number of inputs
            None # parent
        )
        self.xcorr_sink_01.set_update_time(0.10)
        self.xcorr_sink_01.set_y_axis((-50), 0)
        self.xcorr_sink_01.enable_autoscale(False)
        self.xcorr_sink_01.enable_grid(True)
        self.xcorr_sink_01.set_x_axis_units("")
        self.xcorr_sink_01.set_y_axis_units("")
        self.xcorr_sink_01.set_ref_level(0)

        self.xcorr_sink_01.disable_legend()

        labels = ['', '', '', '', '',
            '', '', '', '', '']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ["blue", "red", "green", "black", "cyan",
            "magenta", "yellow", "dark red", "dark green", "dark blue"]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.xcorr_sink_01.set_line_label(i, "Data {0}".format(i))
            else:
                self.xcorr_sink_01.set_line_label(i, labels[i])
            self.xcorr_sink_01.set_line_width(i, widths[i])
            self.xcorr_sink_01.set_line_color(i, colors[i])
            self.xcorr_sink_01.set_line_alpha(i, alphas[i])

        self._xcorr_sink_01_win = sip.wrapinstance(self.xcorr_sink_01.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._xcorr_sink_01_win, 1, 0, 1, 1)
        for r in range(1, 2):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 1):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.s2v_ch4 = blocks.stream_to_vector(gr.sizeof_gr_complex*1, vec_len)
        self.s2v_ch3 = blocks.stream_to_vector(gr.sizeof_gr_complex*1, vec_len)
        self.s2v_ch2 = blocks.stream_to_vector(gr.sizeof_gr_complex*1, vec_len)
        self.s2v_ch1 = blocks.stream_to_vector(gr.sizeof_gr_complex*1, vec_len)
        self.s2v_ch0 = blocks.stream_to_vector(gr.sizeof_gr_complex*1, vec_len)
        self.phase_sink_04 = qtgui.number_sink(
            gr.sizeof_float,
            0,
            qtgui.NUM_GRAPH_HORIZ,
            1,
            None # parent
        )
        self.phase_sink_04.set_update_time(0.10)
        self.phase_sink_04.set_title("")

        labels = ['Phase 0-4 [deg]', '', '', '', '',
            '', '', '', '', '']
        units = ['', '', '', '', '',
            '', '', '', '', '']
        colors = [("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"),
            ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black")]
        factor = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]

        for i in range(1):
            self.phase_sink_04.set_min(i, -180)
            self.phase_sink_04.set_max(i, 180)
            self.phase_sink_04.set_color(i, colors[i][0], colors[i][1])
            if len(labels[i]) == 0:
                self.phase_sink_04.set_label(i, "Data {0}".format(i))
            else:
                self.phase_sink_04.set_label(i, labels[i])
            self.phase_sink_04.set_unit(i, units[i])
            self.phase_sink_04.set_factor(i, factor[i])

        self.phase_sink_04.enable_autoscale(False)
        self._phase_sink_04_win = sip.wrapinstance(self.phase_sink_04.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._phase_sink_04_win, 4, 1, 1, 1)
        for r in range(4, 5):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(1, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.phase_sink_03 = qtgui.number_sink(
            gr.sizeof_float,
            0,
            qtgui.NUM_GRAPH_HORIZ,
            1,
            None # parent
        )
        self.phase_sink_03.set_update_time(0.10)
        self.phase_sink_03.set_title("")

        labels = ['Phase 0-3 [deg]', '', '', '', '',
            '', '', '', '', '']
        units = ['', '', '', '', '',
            '', '', '', '', '']
        colors = [("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"),
            ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black")]
        factor = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]

        for i in range(1):
            self.phase_sink_03.set_min(i, -180)
            self.phase_sink_03.set_max(i, 180)
            self.phase_sink_03.set_color(i, colors[i][0], colors[i][1])
            if len(labels[i]) == 0:
                self.phase_sink_03.set_label(i, "Data {0}".format(i))
            else:
                self.phase_sink_03.set_label(i, labels[i])
            self.phase_sink_03.set_unit(i, units[i])
            self.phase_sink_03.set_factor(i, factor[i])

        self.phase_sink_03.enable_autoscale(False)
        self._phase_sink_03_win = sip.wrapinstance(self.phase_sink_03.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._phase_sink_03_win, 3, 1, 1, 1)
        for r in range(3, 4):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(1, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.phase_sink_02 = qtgui.number_sink(
            gr.sizeof_float,
            0,
            qtgui.NUM_GRAPH_HORIZ,
            1,
            None # parent
        )
        self.phase_sink_02.set_update_time(0.10)
        self.phase_sink_02.set_title("")

        labels = ['Phase 0-2 [deg]', '', '', '', '',
            '', '', '', '', '']
        units = ['', '', '', '', '',
            '', '', '', '', '']
        colors = [("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"),
            ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black")]
        factor = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]

        for i in range(1):
            self.phase_sink_02.set_min(i, -180)
            self.phase_sink_02.set_max(i, 180)
            self.phase_sink_02.set_color(i, colors[i][0], colors[i][1])
            if len(labels[i]) == 0:
                self.phase_sink_02.set_label(i, "Data {0}".format(i))
            else:
                self.phase_sink_02.set_label(i, labels[i])
            self.phase_sink_02.set_unit(i, units[i])
            self.phase_sink_02.set_factor(i, factor[i])

        self.phase_sink_02.enable_autoscale(False)
        self._phase_sink_02_win = sip.wrapinstance(self.phase_sink_02.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._phase_sink_02_win, 2, 1, 1, 1)
        for r in range(2, 3):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(1, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.phase_sink_01 = qtgui.number_sink(
            gr.sizeof_float,
            0,
            qtgui.NUM_GRAPH_HORIZ,
            1,
            None # parent
        )
        self.phase_sink_01.set_update_time(0.10)
        self.phase_sink_01.set_title("")

        labels = ['Phase 0-1 [deg]', '', '', '', '',
            '', '', '', '', '']
        units = ['', '', '', '', '',
            '', '', '', '', '']
        colors = [("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"),
            ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black")]
        factor = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]

        for i in range(1):
            self.phase_sink_01.set_min(i, -180)
            self.phase_sink_01.set_max(i, 180)
            self.phase_sink_01.set_color(i, colors[i][0], colors[i][1])
            if len(labels[i]) == 0:
                self.phase_sink_01.set_label(i, "Data {0}".format(i))
            else:
                self.phase_sink_01.set_label(i, labels[i])
            self.phase_sink_01.set_unit(i, units[i])
            self.phase_sink_01.set_factor(i, factor[i])

        self.phase_sink_01.enable_autoscale(False)
        self._phase_sink_01_win = sip.wrapinstance(self.phase_sink_01.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._phase_sink_01_win, 1, 1, 1, 1)
        for r in range(1, 2):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(1, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.lag_sink_04 = qtgui.number_sink(
            gr.sizeof_float,
            0,
            qtgui.NUM_GRAPH_HORIZ,
            1,
            None # parent
        )
        self.lag_sink_04.set_update_time(0.10)
        self.lag_sink_04.set_title("")

        labels = ['Lag 0-4 [samp]', '', '', '', '',
            '', '', '', '', '']
        units = ['', '', '', '', '',
            '', '', '', '', '']
        colors = [("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"),
            ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black")]
        factor = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]

        for i in range(1):
            self.lag_sink_04.set_min(i, -100)
            self.lag_sink_04.set_max(i, 100)
            self.lag_sink_04.set_color(i, colors[i][0], colors[i][1])
            if len(labels[i]) == 0:
                self.lag_sink_04.set_label(i, "Data {0}".format(i))
            else:
                self.lag_sink_04.set_label(i, labels[i])
            self.lag_sink_04.set_unit(i, units[i])
            self.lag_sink_04.set_factor(i, factor[i])

        self.lag_sink_04.enable_autoscale(False)
        self._lag_sink_04_win = sip.wrapinstance(self.lag_sink_04.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._lag_sink_04_win, 4, 2, 1, 1)
        for r in range(4, 5):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(2, 3):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.lag_sink_03 = qtgui.number_sink(
            gr.sizeof_float,
            0,
            qtgui.NUM_GRAPH_HORIZ,
            1,
            None # parent
        )
        self.lag_sink_03.set_update_time(0.10)
        self.lag_sink_03.set_title("")

        labels = ['Lag 0-3 [samp]', '', '', '', '',
            '', '', '', '', '']
        units = ['', '', '', '', '',
            '', '', '', '', '']
        colors = [("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"),
            ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black")]
        factor = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]

        for i in range(1):
            self.lag_sink_03.set_min(i, -100)
            self.lag_sink_03.set_max(i, 100)
            self.lag_sink_03.set_color(i, colors[i][0], colors[i][1])
            if len(labels[i]) == 0:
                self.lag_sink_03.set_label(i, "Data {0}".format(i))
            else:
                self.lag_sink_03.set_label(i, labels[i])
            self.lag_sink_03.set_unit(i, units[i])
            self.lag_sink_03.set_factor(i, factor[i])

        self.lag_sink_03.enable_autoscale(False)
        self._lag_sink_03_win = sip.wrapinstance(self.lag_sink_03.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._lag_sink_03_win, 3, 2, 1, 1)
        for r in range(3, 4):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(2, 3):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.lag_sink_02 = qtgui.number_sink(
            gr.sizeof_float,
            0,
            qtgui.NUM_GRAPH_HORIZ,
            1,
            None # parent
        )
        self.lag_sink_02.set_update_time(0.10)
        self.lag_sink_02.set_title("")

        labels = ['Lag 0-2 [samp]', '', '', '', '',
            '', '', '', '', '']
        units = ['', '', '', '', '',
            '', '', '', '', '']
        colors = [("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"),
            ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black")]
        factor = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]

        for i in range(1):
            self.lag_sink_02.set_min(i, -100)
            self.lag_sink_02.set_max(i, 100)
            self.lag_sink_02.set_color(i, colors[i][0], colors[i][1])
            if len(labels[i]) == 0:
                self.lag_sink_02.set_label(i, "Data {0}".format(i))
            else:
                self.lag_sink_02.set_label(i, labels[i])
            self.lag_sink_02.set_unit(i, units[i])
            self.lag_sink_02.set_factor(i, factor[i])

        self.lag_sink_02.enable_autoscale(False)
        self._lag_sink_02_win = sip.wrapinstance(self.lag_sink_02.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._lag_sink_02_win, 2, 2, 1, 1)
        for r in range(2, 3):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(2, 3):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.lag_sink_01 = qtgui.number_sink(
            gr.sizeof_float,
            0,
            qtgui.NUM_GRAPH_HORIZ,
            1,
            None # parent
        )
        self.lag_sink_01.set_update_time(0.10)
        self.lag_sink_01.set_title("")

        labels = ['Lag 0-1 [samp]', '', '', '', '',
            '', '', '', '', '']
        units = ['', '', '', '', '',
            '', '', '', '', '']
        colors = [("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"),
            ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black")]
        factor = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]

        for i in range(1):
            self.lag_sink_01.set_min(i, -100)
            self.lag_sink_01.set_max(i, 100)
            self.lag_sink_01.set_color(i, colors[i][0], colors[i][1])
            if len(labels[i]) == 0:
                self.lag_sink_01.set_label(i, "Data {0}".format(i))
            else:
                self.lag_sink_01.set_label(i, labels[i])
            self.lag_sink_01.set_unit(i, units[i])
            self.lag_sink_01.set_factor(i, factor[i])

        self.lag_sink_01.enable_autoscale(False)
        self._lag_sink_01_win = sip.wrapinstance(self.lag_sink_01.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._lag_sink_01_win, 1, 2, 1, 1)
        for r in range(1, 2):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(2, 3):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.krakensdr_v2_source_0 = krakensdr_v2.krakensdr_source('127.0.0.1', 8091, 8092, 5, freq, gain, False, True, False)
        self.corr_04 = krakensdr_v2.correlator(vec_len, fft_cut)
        self.corr_03 = krakensdr_v2.correlator(vec_len, fft_cut)
        self.corr_02 = krakensdr_v2.correlator(vec_len, fft_cut)
        self.corr_01 = krakensdr_v2.correlator(vec_len, fft_cut)


        ##################################################
        # Connections
        ##################################################
        self.connect((self.corr_01, 2), (self.lag_sink_01, 0))
        self.connect((self.corr_01, 1), (self.phase_sink_01, 0))
        self.connect((self.corr_01, 0), (self.xcorr_sink_01, 0))
        self.connect((self.corr_02, 2), (self.lag_sink_02, 0))
        self.connect((self.corr_02, 1), (self.phase_sink_02, 0))
        self.connect((self.corr_02, 0), (self.xcorr_sink_02, 0))
        self.connect((self.corr_03, 2), (self.lag_sink_03, 0))
        self.connect((self.corr_03, 1), (self.phase_sink_03, 0))
        self.connect((self.corr_03, 0), (self.xcorr_sink_03, 0))
        self.connect((self.corr_04, 2), (self.lag_sink_04, 0))
        self.connect((self.corr_04, 1), (self.phase_sink_04, 0))
        self.connect((self.corr_04, 0), (self.xcorr_sink_04, 0))
        self.connect((self.krakensdr_v2_source_0, 0), (self.s2v_ch0, 0))
        self.connect((self.krakensdr_v2_source_0, 1), (self.s2v_ch1, 0))
        self.connect((self.krakensdr_v2_source_0, 2), (self.s2v_ch2, 0))
        self.connect((self.krakensdr_v2_source_0, 3), (self.s2v_ch3, 0))
        self.connect((self.krakensdr_v2_source_0, 4), (self.s2v_ch4, 0))
        self.connect((self.s2v_ch0, 0), (self.corr_01, 0))
        self.connect((self.s2v_ch0, 0), (self.corr_02, 0))
        self.connect((self.s2v_ch0, 0), (self.corr_03, 0))
        self.connect((self.s2v_ch0, 0), (self.corr_04, 0))
        self.connect((self.s2v_ch1, 0), (self.corr_01, 1))
        self.connect((self.s2v_ch2, 0), (self.corr_02, 1))
        self.connect((self.s2v_ch3, 0), (self.corr_03, 1))
        self.connect((self.s2v_ch4, 0), (self.corr_04, 1))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("gnuradio/flowgraphs", "kraken_correlator_test")
        self.settings.setValue("geometry", self.saveGeometry())
        self.stop()
        self.wait()

        event.accept()

    def get_vec_len(self):
        return self.vec_len

    def set_vec_len(self, vec_len):
        self.vec_len = vec_len

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate

    def get_gain(self):
        return self.gain

    def set_gain(self, gain):
        self.gain = gain
        Qt.QMetaObject.invokeMethod(self._gain_line_edit, "setText", Qt.Q_ARG("QString", eng_notation.num_to_str(self.gain)))
        self.krakensdr_v2_source_0.set_gain(self.gain)

    def get_freq(self):
        return self.freq

    def set_freq(self, freq):
        self.freq = freq
        Qt.QMetaObject.invokeMethod(self._freq_line_edit, "setText", Qt.Q_ARG("QString", eng_notation.num_to_str(self.freq)))
        self.krakensdr_v2_source_0.set_freq(self.freq)

    def get_fft_cut(self):
        return self.fft_cut

    def set_fft_cut(self, fft_cut):
        self.fft_cut = fft_cut
        self.xcorr_sink_01.set_x_axis((-self.fft_cut//2), 1.0)
        self.xcorr_sink_02.set_x_axis((-self.fft_cut//2), 1.0)
        self.xcorr_sink_03.set_x_axis((-self.fft_cut//2), 1.0)
        self.xcorr_sink_04.set_x_axis((-self.fft_cut//2), 1.0)




def main(top_block_cls=kraken_correlator_test, options=None):

    qapp = Qt.QApplication(sys.argv)

    tb = top_block_cls()

    tb.start()
    tb.flowgraph_started.set()

    tb.show()

    def sig_handler(sig=None, frame=None):
        tb.stop()
        tb.wait()

        Qt.QApplication.quit()

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    timer = Qt.QTimer()
    timer.start(500)
    timer.timeout.connect(lambda: None)

    qapp.exec_()

if __name__ == '__main__':
    main()
