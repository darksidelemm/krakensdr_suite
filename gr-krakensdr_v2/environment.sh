#!/bin/sh
# Environment for a user-prefix install of gr-krakensdr_v2 (no sudo).
#
# Needed when the module was installed with
#   cmake -DCMAKE_INSTALL_PREFIX=$HOME/.local \
#         -DGR_PYTHON_DIR=$HOME/.local/lib/python3.X/site-packages ..
# so that gnuradio-companion finds the block YAMLs and the shell finds the
# example binaries. (Python imports and library paths work without this:
# ~/.local site-packages is on the default user path and the binaries carry
# an rpath to the install libdir.)
#
# Usage:  . ./environment.sh      (or add the exports to ~/.bashrc)

PREFIX="$HOME/.local"

export GRC_BLOCKS_PATH="$PREFIX/share/gnuradio/grc/blocks${GRC_BLOCKS_PATH:+:$GRC_BLOCKS_PATH}"
export PATH="$PREFIX/bin:$PATH"

# Only required if your Python does NOT already search the user site dir
# (e.g. a venv without --system-site-packages):
PYVER=$(python3 -c 'import sys; print(f"{sys.version_info[0]}.{sys.version_info[1]}")')
export PYTHONPATH="$PREFIX/lib/python$PYVER/site-packages${PYTHONPATH:+:$PYTHONPATH}"
