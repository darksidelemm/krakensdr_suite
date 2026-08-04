#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: KrakenSDR v2 FFT Display
# Author: KrakenRF Inc.
# Copyright: 2026 KrakenRF Inc.
# Description: KrakenSDR v2 FFT Display
# GNU Radio version: 3.10.12.0

from PyQt5 import Qt
from gnuradio import qtgui
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



class kraken_fft_display(gr.top_block, Qt.QWidget):

    def __init__(self):
        gr.top_block.__init__(self, "KrakenSDR v2 FFT Display", catch_exceptions=True)
        Qt.QWidget.__init__(self)
        self.setWindowTitle("KrakenSDR v2 FFT Display")
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

        self.settings = Qt.QSettings("gnuradio/flowgraphs", "kraken_fft_display")

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
        self.gain = gain = 40.2
        self.freq = freq = 100.0

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
        self.qtgui_sink_ch4 = qtgui.sink_c(
            16384, #fftsize
            window.WIN_BLACKMAN_hARRIS, #wintype
            (freq*1e6), #fc
            samp_rate, #bw
            "ch_4", #name
            True, #plotfreq
            True, #plotwaterfall
            False, #plottime
            False, #plotconst
            None # parent
        )
        self.qtgui_sink_ch4.set_update_time(1.0/10)
        self._qtgui_sink_ch4_win = sip.wrapinstance(self.qtgui_sink_ch4.qwidget(), Qt.QWidget)

        self.qtgui_sink_ch4.enable_rf_freq(True)

        self.top_grid_layout.addWidget(self._qtgui_sink_ch4_win, 3, 0, 1, 1)
        for r in range(3, 4):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 1):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.qtgui_sink_ch3 = qtgui.sink_c(
            16384, #fftsize
            window.WIN_BLACKMAN_hARRIS, #wintype
            (freq*1e6), #fc
            samp_rate, #bw
            "ch_3", #name
            True, #plotfreq
            True, #plotwaterfall
            False, #plottime
            False, #plotconst
            None # parent
        )
        self.qtgui_sink_ch3.set_update_time(1.0/10)
        self._qtgui_sink_ch3_win = sip.wrapinstance(self.qtgui_sink_ch3.qwidget(), Qt.QWidget)

        self.qtgui_sink_ch3.enable_rf_freq(True)

        self.top_grid_layout.addWidget(self._qtgui_sink_ch3_win, 2, 1, 1, 1)
        for r in range(2, 3):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(1, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.qtgui_sink_ch2 = qtgui.sink_c(
            16384, #fftsize
            window.WIN_BLACKMAN_hARRIS, #wintype
            (freq*1e6), #fc
            samp_rate, #bw
            "ch_2", #name
            True, #plotfreq
            True, #plotwaterfall
            False, #plottime
            False, #plotconst
            None # parent
        )
        self.qtgui_sink_ch2.set_update_time(1.0/10)
        self._qtgui_sink_ch2_win = sip.wrapinstance(self.qtgui_sink_ch2.qwidget(), Qt.QWidget)

        self.qtgui_sink_ch2.enable_rf_freq(True)

        self.top_grid_layout.addWidget(self._qtgui_sink_ch2_win, 2, 0, 1, 1)
        for r in range(2, 3):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 1):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.qtgui_sink_ch1 = qtgui.sink_c(
            16384, #fftsize
            window.WIN_BLACKMAN_hARRIS, #wintype
            (freq*1e6), #fc
            samp_rate, #bw
            "ch_1", #name
            True, #plotfreq
            True, #plotwaterfall
            False, #plottime
            False, #plotconst
            None # parent
        )
        self.qtgui_sink_ch1.set_update_time(1.0/10)
        self._qtgui_sink_ch1_win = sip.wrapinstance(self.qtgui_sink_ch1.qwidget(), Qt.QWidget)

        self.qtgui_sink_ch1.enable_rf_freq(True)

        self.top_grid_layout.addWidget(self._qtgui_sink_ch1_win, 1, 1, 1, 1)
        for r in range(1, 2):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(1, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.qtgui_sink_ch0 = qtgui.sink_c(
            16384, #fftsize
            window.WIN_BLACKMAN_hARRIS, #wintype
            (freq*1e6), #fc
            samp_rate, #bw
            "ch_0", #name
            True, #plotfreq
            True, #plotwaterfall
            False, #plottime
            False, #plotconst
            None # parent
        )
        self.qtgui_sink_ch0.set_update_time(1.0/10)
        self._qtgui_sink_ch0_win = sip.wrapinstance(self.qtgui_sink_ch0.qwidget(), Qt.QWidget)

        self.qtgui_sink_ch0.enable_rf_freq(True)

        self.top_grid_layout.addWidget(self._qtgui_sink_ch0_win, 1, 0, 1, 1)
        for r in range(1, 2):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 1):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.krakensdr_v2_source_0 = krakensdr_v2.krakensdr_source('127.0.0.1', 8091, 8092, 5, freq, gain, True, True, False)


        ##################################################
        # Connections
        ##################################################
        self.connect((self.krakensdr_v2_source_0, 0), (self.qtgui_sink_ch0, 0))
        self.connect((self.krakensdr_v2_source_0, 1), (self.qtgui_sink_ch1, 0))
        self.connect((self.krakensdr_v2_source_0, 2), (self.qtgui_sink_ch2, 0))
        self.connect((self.krakensdr_v2_source_0, 3), (self.qtgui_sink_ch3, 0))
        self.connect((self.krakensdr_v2_source_0, 4), (self.qtgui_sink_ch4, 0))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("gnuradio/flowgraphs", "kraken_fft_display")
        self.settings.setValue("geometry", self.saveGeometry())
        self.stop()
        self.wait()

        event.accept()

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate
        self.qtgui_sink_ch0.set_frequency_range((self.freq*1e6), self.samp_rate)
        self.qtgui_sink_ch1.set_frequency_range((self.freq*1e6), self.samp_rate)
        self.qtgui_sink_ch2.set_frequency_range((self.freq*1e6), self.samp_rate)
        self.qtgui_sink_ch3.set_frequency_range((self.freq*1e6), self.samp_rate)
        self.qtgui_sink_ch4.set_frequency_range((self.freq*1e6), self.samp_rate)

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
        self.qtgui_sink_ch0.set_frequency_range((self.freq*1e6), self.samp_rate)
        self.qtgui_sink_ch1.set_frequency_range((self.freq*1e6), self.samp_rate)
        self.qtgui_sink_ch2.set_frequency_range((self.freq*1e6), self.samp_rate)
        self.qtgui_sink_ch3.set_frequency_range((self.freq*1e6), self.samp_rate)
        self.qtgui_sink_ch4.set_frequency_range((self.freq*1e6), self.samp_rate)




def main(top_block_cls=kraken_fft_display, options=None):

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
