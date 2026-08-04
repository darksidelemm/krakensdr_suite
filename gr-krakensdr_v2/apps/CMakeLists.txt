# Copyright 2026 KrakenRF Inc.
#
# This file is a part of gr-krakensdr_v2
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

########################################################################
# C++ console example applications
########################################################################

set(kraken_apps
    kraken_fft_test
    kraken_corr_test
    kraken_music_doa
)

foreach(app ${kraken_apps})
    add_executable(${app} ${app}.cc)
    target_link_libraries(${app}
        gnuradio-krakensdr_v2
        gnuradio::gnuradio-runtime
        gnuradio::gnuradio-blocks
        gnuradio::gnuradio-fft
        gnuradio::gnuradio-filter)
    install(TARGETS ${app} DESTINATION bin)
endforeach()
