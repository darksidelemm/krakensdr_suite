#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: KrakenSDR v2 MUSIC DoA
# Author: KrakenRF Inc.
# Copyright: 2026 KrakenRF Inc.
# Description: KrakenSDR v2 MUSIC DoA
# GNU Radio version: 3.10.12.0

from PyQt5 import Qt
from gnuradio import qtgui
from PyQt5 import QtCore
from PyQt5.QtCore import QObject, pyqtSlot
from gnuradio import blocks
from gnuradio import eng_notation
from gnuradio import filter
from gnuradio.filter import firdes
from gnuradio import gr
from gnuradio.fft import window
import sys
import signal
from PyQt5 import Qt
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import krakensdr_v2
import sip
import threading



class kraken_music_doa(gr.top_block, Qt.QWidget):

    def __init__(self):
        gr.top_block.__init__(self, "KrakenSDR v2 MUSIC DoA", catch_exceptions=True)
        Qt.QWidget.__init__(self)
        self.setWindowTitle("KrakenSDR v2 MUSIC DoA")
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

        self.settings = Qt.QSettings("gnuradio/flowgraphs", "kraken_music_doa")

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
        self.samp_rate = samp_rate = 2400000
        self.decim = decim = 10
        self.vec_len = vec_len = 256
        self.radius = radius = 0.25
        self.gain = gain = 40.2
        self.freq = freq = 100.0
        self.fir_taps = fir_taps = firdes.low_pass(1.0, samp_rate, 0.4*samp_rate/decim, 0.2*samp_rate/decim)
        self.array_type = array_type = "UCA"

        ##################################################
        # Blocks
        ##################################################

        self.tab_full_spectrum = Qt.QTabWidget()
        self.tab_full_spectrum_widget_0 = Qt.QWidget()
        self.tab_full_spectrum_layout_0 = Qt.QBoxLayout(Qt.QBoxLayout.TopToBottom, self.tab_full_spectrum_widget_0)
        self.tab_full_spectrum_grid_layout_0 = Qt.QGridLayout()
        self.tab_full_spectrum_layout_0.addLayout(self.tab_full_spectrum_grid_layout_0)
        self.tab_full_spectrum.addTab(self.tab_full_spectrum_widget_0, 'CH0 Full Spectrum (2.4 MHz)')
        self.top_grid_layout.addWidget(self.tab_full_spectrum, 4, 0, 1, 3)
        for r in range(4, 5):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 3):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.tab_decimated = Qt.QTabWidget()
        self.tab_decimated_widget_0 = Qt.QWidget()
        self.tab_decimated_layout_0 = Qt.QBoxLayout(Qt.QBoxLayout.TopToBottom, self.tab_decimated_widget_0)
        self.tab_decimated_grid_layout_0 = Qt.QGridLayout()
        self.tab_decimated_layout_0.addLayout(self.tab_decimated_grid_layout_0)
        self.tab_decimated.addTab(self.tab_decimated_widget_0, 'CH0 Decimated (the band MUSIC sees)')
        self.top_grid_layout.addWidget(self.tab_decimated, 4, 3, 1, 3)
        for r in range(4, 5):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(3, 6):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._radius_tool_bar = Qt.QToolBar(self)
        self._radius_tool_bar.addWidget(Qt.QLabel("UCA Radius / ULA Spacing [m]" + ": "))
        self._radius_line_edit = Qt.QLineEdit(str(self.radius))
        self._radius_tool_bar.addWidget(self._radius_line_edit)
        self._radius_line_edit.editingFinished.connect(
            lambda: self.set_radius(eng_notation.str_to_num(str(self._radius_line_edit.text()))))
        self.top_grid_layout.addWidget(self._radius_tool_bar, 1, 2, 1, 2)
        for r in range(1, 2):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(2, 4):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._gain_range = qtgui.Range(-1, 49.6, 0.1, 40.2, 200)
        self._gain_win = qtgui.RangeWidget(self._gain_range, self.set_gain, "Gain [dB] (-1 = AGC)", "counter_slider", float, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._gain_win, 0, 3, 1, 3)
        for r in range(0, 1):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(3, 6):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._freq_range = qtgui.Range(24, 1766, 0.1, 100.0, 200)
        self._freq_win = qtgui.RangeWidget(self._freq_range, self.set_freq, "Center Frequency [MHz]", "counter_slider", float, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._freq_win, 0, 0, 1, 3)
        for r in range(0, 1):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 3):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._decim_range = qtgui.Range(1, 48, 1, 10, 200)
        self._decim_win = qtgui.RangeWidget(self._decim_range, self.set_decim, "Decimation", "counter_slider", int, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._decim_win, 1, 0, 1, 2)
        for r in range(1, 2):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        # Create the options list
        self._array_type_options = ['UCA', 'ULA']
        # Create the labels list
        self._array_type_labels = ['UCA', 'ULA']
        # Create the combo box
        self._array_type_tool_bar = Qt.QToolBar(self)
        self._array_type_tool_bar.addWidget(Qt.QLabel("Array Type" + ": "))
        self._array_type_combo_box = Qt.QComboBox()
        self._array_type_tool_bar.addWidget(self._array_type_combo_box)
        for _label in self._array_type_labels: self._array_type_combo_box.addItem(_label)
        self._array_type_callback = lambda i: Qt.QMetaObject.invokeMethod(self._array_type_combo_box, "setCurrentIndex", Qt.Q_ARG("int", self._array_type_options.index(i)))
        self._array_type_callback(self.array_type)
        self._array_type_combo_box.currentIndexChanged.connect(
            lambda i: self.set_array_type(self._array_type_options[i]))
        # Create the radio buttons
        self.top_grid_layout.addWidget(self._array_type_tool_bar, 1, 4, 1, 2)
        for r in range(1, 2):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(4, 6):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.s2v_ch4 = blocks.stream_to_vector(gr.sizeof_gr_complex*1, vec_len)
        self.s2v_ch3 = blocks.stream_to_vector(gr.sizeof_gr_complex*1, vec_len)
        self.s2v_ch2 = blocks.stream_to_vector(gr.sizeof_gr_complex*1, vec_len)
        self.s2v_ch1 = blocks.stream_to_vector(gr.sizeof_gr_complex*1, vec_len)
        self.s2v_ch0 = blocks.stream_to_vector(gr.sizeof_gr_complex*1, vec_len)
        self.krakensdr_v2_source_0 = krakensdr_v2.krakensdr_source('127.0.0.1', 8091, 8092, 5, freq, gain, True, True, False)
        self.keep_ch4 = blocks.keep_one_in_n(gr.sizeof_gr_complex*1, decim)
        self.keep_ch3 = blocks.keep_one_in_n(gr.sizeof_gr_complex*1, decim)
        self.keep_ch2 = blocks.keep_one_in_n(gr.sizeof_gr_complex*1, decim)
        self.keep_ch1 = blocks.keep_one_in_n(gr.sizeof_gr_complex*1, decim)
        self.keep_ch0 = blocks.keep_one_in_n(gr.sizeof_gr_complex*1, decim)
        self.fir_ch4 = filter.fir_filter_ccf(1, fir_taps)
        self.fir_ch4.declare_sample_delay(0)
        self.fir_ch3 = filter.fir_filter_ccf(1, fir_taps)
        self.fir_ch3.declare_sample_delay(0)
        self.fir_ch2 = filter.fir_filter_ccf(1, fir_taps)
        self.fir_ch2.declare_sample_delay(0)
        self.fir_ch1 = filter.fir_filter_ccf(1, fir_taps)
        self.fir_ch1.declare_sample_delay(0)
        self.fir_ch0 = filter.fir_filter_ccf(1, fir_taps)
        self.fir_ch0.declare_sample_delay(0)
        self.doa_spectrum_sink = qtgui.vector_sink_f(
            360,
            0,
            1.0,
            "Angle [deg]",
            "dB",
            "MUSIC Pseudospectrum",
            1, # Number of inputs
            None # parent
        )
        self.doa_spectrum_sink.set_update_time(0.10)
        self.doa_spectrum_sink.set_y_axis((-100), 0)
        self.doa_spectrum_sink.enable_autoscale(True)
        self.doa_spectrum_sink.enable_grid(True)
        self.doa_spectrum_sink.set_x_axis_units("")
        self.doa_spectrum_sink.set_y_axis_units("")
        self.doa_spectrum_sink.set_ref_level(0)

        self.doa_spectrum_sink.disable_legend()

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
                self.doa_spectrum_sink.set_line_label(i, "Data {0}".format(i))
            else:
                self.doa_spectrum_sink.set_line_label(i, labels[i])
            self.doa_spectrum_sink.set_line_width(i, widths[i])
            self.doa_spectrum_sink.set_line_color(i, colors[i])
            self.doa_spectrum_sink.set_line_alpha(i, alphas[i])

        self._doa_spectrum_sink_win = sip.wrapinstance(self.doa_spectrum_sink.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._doa_spectrum_sink_win, 2, 0, 1, 6)
        for r in range(2, 3):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 6):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.doa_music_0 = krakensdr_v2.doa_music(vec_len, freq, radius, 5, array_type, 1)
        self.doa_conf_sink = qtgui.number_sink(
            gr.sizeof_float,
            0,
            qtgui.NUM_GRAPH_HORIZ,
            1,
            None # parent
        )
        self.doa_conf_sink.set_update_time(0.10)
        self.doa_conf_sink.set_title("")

        labels = ['Confidence', '', '', '', '',
            '', '', '', '', '']
        units = ['', '', '', '', '',
            '', '', '', '', '']
        colors = [("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"),
            ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black")]
        factor = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]

        for i in range(1):
            self.doa_conf_sink.set_min(i, 0)
            self.doa_conf_sink.set_max(i, 1)
            self.doa_conf_sink.set_color(i, colors[i][0], colors[i][1])
            if len(labels[i]) == 0:
                self.doa_conf_sink.set_label(i, "Data {0}".format(i))
            else:
                self.doa_conf_sink.set_label(i, labels[i])
            self.doa_conf_sink.set_unit(i, units[i])
            self.doa_conf_sink.set_factor(i, factor[i])

        self.doa_conf_sink.enable_autoscale(False)
        self._doa_conf_sink_win = sip.wrapinstance(self.doa_conf_sink.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._doa_conf_sink_win, 3, 3, 1, 3)
        for r in range(3, 4):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(3, 6):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.doa_angle_sink = qtgui.number_sink(
            gr.sizeof_float,
            0,
            qtgui.NUM_GRAPH_HORIZ,
            1,
            None # parent
        )
        self.doa_angle_sink.set_update_time(0.10)
        self.doa_angle_sink.set_title("")

        labels = ['DoA [deg]', '', '', '', '',
            '', '', '', '', '']
        units = ['', '', '', '', '',
            '', '', '', '', '']
        colors = [("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"),
            ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black"), ("black", "black")]
        factor = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]

        for i in range(1):
            self.doa_angle_sink.set_min(i, 0)
            self.doa_angle_sink.set_max(i, 360)
            self.doa_angle_sink.set_color(i, colors[i][0], colors[i][1])
            if len(labels[i]) == 0:
                self.doa_angle_sink.set_label(i, "Data {0}".format(i))
            else:
                self.doa_angle_sink.set_label(i, labels[i])
            self.doa_angle_sink.set_unit(i, units[i])
            self.doa_angle_sink.set_factor(i, factor[i])

        self.doa_angle_sink.enable_autoscale(False)
        self._doa_angle_sink_win = sip.wrapinstance(self.doa_angle_sink.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._doa_angle_sink_win, 3, 0, 1, 3)
        for r in range(3, 4):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 3):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.ch0_full_waterfall = qtgui.waterfall_sink_c(
            1024, #size
            window.WIN_BLACKMAN_hARRIS, #wintype
            (freq*1e6), #fc
            samp_rate, #bw
            "", #name
            1, #number of inputs
            None # parent
        )
        self.ch0_full_waterfall.set_update_time(0.10)
        self.ch0_full_waterfall.enable_grid(False)
        self.ch0_full_waterfall.enable_axis_labels(True)



        labels = ['', '', '', '', '',
                  '', '', '', '', '']
        colors = [0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
                  1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.ch0_full_waterfall.set_line_label(i, "Data {0}".format(i))
            else:
                self.ch0_full_waterfall.set_line_label(i, labels[i])
            self.ch0_full_waterfall.set_color_map(i, colors[i])
            self.ch0_full_waterfall.set_line_alpha(i, alphas[i])

        self.ch0_full_waterfall.set_intensity_range(-120, -20)

        self._ch0_full_waterfall_win = sip.wrapinstance(self.ch0_full_waterfall.qwidget(), Qt.QWidget)

        self.tab_full_spectrum_grid_layout_0.addWidget(self._ch0_full_waterfall_win, 1, 0, 1, 1)
        for r in range(1, 2):
            self.tab_full_spectrum_grid_layout_0.setRowStretch(r, 1)
        for c in range(0, 1):
            self.tab_full_spectrum_grid_layout_0.setColumnStretch(c, 1)
        self.ch0_full_fft = qtgui.freq_sink_c(
            4096, #size
            window.WIN_BLACKMAN_hARRIS, #wintype
            (freq*1e6), #fc
            samp_rate, #bw
            "", #name
            1,
            None # parent
        )
        self.ch0_full_fft.set_update_time(0.10)
        self.ch0_full_fft.set_y_axis((-120), 0)
        self.ch0_full_fft.set_y_label('Relative Gain', 'dB')
        self.ch0_full_fft.set_trigger_mode(qtgui.TRIG_MODE_FREE, 0.0, 0, "")
        self.ch0_full_fft.enable_autoscale(False)
        self.ch0_full_fft.enable_grid(True)
        self.ch0_full_fft.set_fft_average(1.0)
        self.ch0_full_fft.enable_axis_labels(True)
        self.ch0_full_fft.enable_control_panel(False)
        self.ch0_full_fft.set_fft_window_normalized(False)

        self.ch0_full_fft.disable_legend()


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
                self.ch0_full_fft.set_line_label(i, "Data {0}".format(i))
            else:
                self.ch0_full_fft.set_line_label(i, labels[i])
            self.ch0_full_fft.set_line_width(i, widths[i])
            self.ch0_full_fft.set_line_color(i, colors[i])
            self.ch0_full_fft.set_line_alpha(i, alphas[i])

        self._ch0_full_fft_win = sip.wrapinstance(self.ch0_full_fft.qwidget(), Qt.QWidget)
        self.tab_full_spectrum_grid_layout_0.addWidget(self._ch0_full_fft_win, 0, 0, 1, 1)
        for r in range(0, 1):
            self.tab_full_spectrum_grid_layout_0.setRowStretch(r, 1)
        for c in range(0, 1):
            self.tab_full_spectrum_grid_layout_0.setColumnStretch(c, 1)
        self.ch0_decim_waterfall = qtgui.waterfall_sink_c(
            1024, #size
            window.WIN_BLACKMAN_hARRIS, #wintype
            (freq*1e6), #fc
            (samp_rate/decim), #bw
            "", #name
            1, #number of inputs
            None # parent
        )
        self.ch0_decim_waterfall.set_update_time(0.10)
        self.ch0_decim_waterfall.enable_grid(False)
        self.ch0_decim_waterfall.enable_axis_labels(True)



        labels = ['', '', '', '', '',
                  '', '', '', '', '']
        colors = [0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
                  1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.ch0_decim_waterfall.set_line_label(i, "Data {0}".format(i))
            else:
                self.ch0_decim_waterfall.set_line_label(i, labels[i])
            self.ch0_decim_waterfall.set_color_map(i, colors[i])
            self.ch0_decim_waterfall.set_line_alpha(i, alphas[i])

        self.ch0_decim_waterfall.set_intensity_range(-120, -20)

        self._ch0_decim_waterfall_win = sip.wrapinstance(self.ch0_decim_waterfall.qwidget(), Qt.QWidget)

        self.tab_decimated_grid_layout_0.addWidget(self._ch0_decim_waterfall_win, 1, 0, 1, 1)
        for r in range(1, 2):
            self.tab_decimated_grid_layout_0.setRowStretch(r, 1)
        for c in range(0, 1):
            self.tab_decimated_grid_layout_0.setColumnStretch(c, 1)
        self.ch0_decim_fft = qtgui.freq_sink_c(
            4096, #size
            window.WIN_BLACKMAN_hARRIS, #wintype
            (freq*1e6), #fc
            (samp_rate/decim), #bw
            "", #name
            1,
            None # parent
        )
        self.ch0_decim_fft.set_update_time(0.10)
        self.ch0_decim_fft.set_y_axis((-120), 0)
        self.ch0_decim_fft.set_y_label('Relative Gain', 'dB')
        self.ch0_decim_fft.set_trigger_mode(qtgui.TRIG_MODE_FREE, 0.0, 0, "")
        self.ch0_decim_fft.enable_autoscale(False)
        self.ch0_decim_fft.enable_grid(True)
        self.ch0_decim_fft.set_fft_average(1.0)
        self.ch0_decim_fft.enable_axis_labels(True)
        self.ch0_decim_fft.enable_control_panel(False)
        self.ch0_decim_fft.set_fft_window_normalized(False)

        self.ch0_decim_fft.disable_legend()


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
                self.ch0_decim_fft.set_line_label(i, "Data {0}".format(i))
            else:
                self.ch0_decim_fft.set_line_label(i, labels[i])
            self.ch0_decim_fft.set_line_width(i, widths[i])
            self.ch0_decim_fft.set_line_color(i, colors[i])
            self.ch0_decim_fft.set_line_alpha(i, alphas[i])

        self._ch0_decim_fft_win = sip.wrapinstance(self.ch0_decim_fft.qwidget(), Qt.QWidget)
        self.tab_decimated_grid_layout_0.addWidget(self._ch0_decim_fft_win, 0, 0, 1, 1)
        for r in range(0, 1):
            self.tab_decimated_grid_layout_0.setRowStretch(r, 1)
        for c in range(0, 1):
            self.tab_decimated_grid_layout_0.setColumnStretch(c, 1)


        ##################################################
        # Connections
        ##################################################
        self.connect((self.doa_music_0, 1), (self.doa_angle_sink, 0))
        self.connect((self.doa_music_0, 2), (self.doa_conf_sink, 0))
        self.connect((self.doa_music_0, 0), (self.doa_spectrum_sink, 0))
        self.connect((self.fir_ch0, 0), (self.keep_ch0, 0))
        self.connect((self.fir_ch1, 0), (self.keep_ch1, 0))
        self.connect((self.fir_ch2, 0), (self.keep_ch2, 0))
        self.connect((self.fir_ch3, 0), (self.keep_ch3, 0))
        self.connect((self.fir_ch4, 0), (self.keep_ch4, 0))
        self.connect((self.keep_ch0, 0), (self.ch0_decim_fft, 0))
        self.connect((self.keep_ch0, 0), (self.ch0_decim_waterfall, 0))
        self.connect((self.keep_ch0, 0), (self.s2v_ch0, 0))
        self.connect((self.keep_ch1, 0), (self.s2v_ch1, 0))
        self.connect((self.keep_ch2, 0), (self.s2v_ch2, 0))
        self.connect((self.keep_ch3, 0), (self.s2v_ch3, 0))
        self.connect((self.keep_ch4, 0), (self.s2v_ch4, 0))
        self.connect((self.krakensdr_v2_source_0, 0), (self.ch0_full_fft, 0))
        self.connect((self.krakensdr_v2_source_0, 0), (self.ch0_full_waterfall, 0))
        self.connect((self.krakensdr_v2_source_0, 0), (self.fir_ch0, 0))
        self.connect((self.krakensdr_v2_source_0, 1), (self.fir_ch1, 0))
        self.connect((self.krakensdr_v2_source_0, 2), (self.fir_ch2, 0))
        self.connect((self.krakensdr_v2_source_0, 3), (self.fir_ch3, 0))
        self.connect((self.krakensdr_v2_source_0, 4), (self.fir_ch4, 0))
        self.connect((self.s2v_ch0, 0), (self.doa_music_0, 0))
        self.connect((self.s2v_ch1, 0), (self.doa_music_0, 1))
        self.connect((self.s2v_ch2, 0), (self.doa_music_0, 2))
        self.connect((self.s2v_ch3, 0), (self.doa_music_0, 3))
        self.connect((self.s2v_ch4, 0), (self.doa_music_0, 4))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("gnuradio/flowgraphs", "kraken_music_doa")
        self.settings.setValue("geometry", self.saveGeometry())
        self.stop()
        self.wait()

        event.accept()

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate
        self.set_fir_taps(firdes.low_pass(1.0, self.samp_rate, 0.4*self.samp_rate/self.decim, 0.2*self.samp_rate/self.decim))
        self.ch0_decim_fft.set_frequency_range((self.freq*1e6), (self.samp_rate/self.decim))
        self.ch0_decim_waterfall.set_frequency_range((self.freq*1e6), (self.samp_rate/self.decim))
        self.ch0_full_fft.set_frequency_range((self.freq*1e6), self.samp_rate)
        self.ch0_full_waterfall.set_frequency_range((self.freq*1e6), self.samp_rate)

    def get_decim(self):
        return self.decim

    def set_decim(self, decim):
        self.decim = decim
        self.set_fir_taps(firdes.low_pass(1.0, self.samp_rate, 0.4*self.samp_rate/self.decim, 0.2*self.samp_rate/self.decim))
        self.ch0_decim_fft.set_frequency_range((self.freq*1e6), (self.samp_rate/self.decim))
        self.ch0_decim_waterfall.set_frequency_range((self.freq*1e6), (self.samp_rate/self.decim))
        self.keep_ch0.set_n(self.decim)
        self.keep_ch1.set_n(self.decim)
        self.keep_ch2.set_n(self.decim)
        self.keep_ch3.set_n(self.decim)
        self.keep_ch4.set_n(self.decim)

    def get_vec_len(self):
        return self.vec_len

    def set_vec_len(self, vec_len):
        self.vec_len = vec_len

    def get_radius(self):
        return self.radius

    def set_radius(self, radius):
        self.radius = radius
        Qt.QMetaObject.invokeMethod(self._radius_line_edit, "setText", Qt.Q_ARG("QString", eng_notation.num_to_str(self.radius)))
        self.doa_music_0.set_array_dist(self.radius)

    def get_gain(self):
        return self.gain

    def set_gain(self, gain):
        self.gain = gain
        self.krakensdr_v2_source_0.set_gain(self.gain)

    def get_freq(self):
        return self.freq

    def set_freq(self, freq):
        self.freq = freq
        self.ch0_decim_fft.set_frequency_range((self.freq*1e6), (self.samp_rate/self.decim))
        self.ch0_decim_waterfall.set_frequency_range((self.freq*1e6), (self.samp_rate/self.decim))
        self.ch0_full_fft.set_frequency_range((self.freq*1e6), self.samp_rate)
        self.ch0_full_waterfall.set_frequency_range((self.freq*1e6), self.samp_rate)
        self.doa_music_0.set_freq(self.freq)
        self.krakensdr_v2_source_0.set_freq(self.freq)

    def get_fir_taps(self):
        return self.fir_taps

    def set_fir_taps(self, fir_taps):
        self.fir_taps = fir_taps
        self.fir_ch0.set_taps(self.fir_taps)
        self.fir_ch1.set_taps(self.fir_taps)
        self.fir_ch2.set_taps(self.fir_taps)
        self.fir_ch3.set_taps(self.fir_taps)
        self.fir_ch4.set_taps(self.fir_taps)

    def get_array_type(self):
        return self.array_type

    def set_array_type(self, array_type):
        self.array_type = array_type
        self._array_type_callback(self.array_type)
        self.doa_music_0.set_array_type(self.array_type)




def main(top_block_cls=kraken_music_doa, options=None):

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
