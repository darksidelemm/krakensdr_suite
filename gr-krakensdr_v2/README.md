# gr-krakensdr_v2

GNU Radio 3.10 out-of-tree module for the **KrakenSDR** running the
**Heimdall v2** server (`krakensdr_v2/heimdall_v2`). It replaces the old
Python-based [gr-krakensdr](https://github.com/krakenrf/gr-krakensdr) module,
which spoke the heimdall v1 DAQ protocol. All blocks and example programs are
native C++ for efficiency on small ARM boards.

The module talks to heimdall_v2's **native TCP interface**:

| Port | Purpose |
|------|---------|
| 8091 | Multi-channel IQ data stream (per-packet header + uint8 IQ) |
| 8092 | JSON control (`set_frequency`, `set_gain`, ...) |
| 8070 | heimdall web UI (noise source / bias-tee toggle lives here) |

## Blocks (GRC category `[KrakenSDR v2]`)

### KrakenSDR v2 Source (`krakensdr_v2.krakensdr_source`)

Streams the phase-coherent, calibration-compensated channels as N complex
streams. Runs its own reader thread (the heimdall data server drops clients
that stall, so the block always drains the socket and drops whole frames
internally if the flowgraph is too slow).

- `ip_addr`, `data_port` (8091), `ctrl_port` (8092), `num_channels` (5)
- `freq` — center frequency in **MHz**, settable at runtime
- `gain` — **single dB value applied to all tuners** by heimdall_v2
  (0–50 dB; negative = AGC). Settable at runtime.
- `coherent_only` (default on) — drop packets that carry calibration noise or
  were captured mid-retune, so the outputs only ever contain antenna data
- `dc_correction` (default on) — subtract the per-frame DC mean per channel
- Stream tags on every output when the frame metadata changes:
  `rx_freq` (double, Hz), `phase_state` (long), `noise_source` (bool)

**Calibration semantics** (heimdall_v2): every frequency or gain change
triggers a ~3 s cooldown followed by an automatic phase+amplitude
recalibration using the built-in noise source. `phase_state` walks
1 → 3 → 4; **4 = CONVERGED**, 5 = cooldown. Samples are calibrated antenna
data when `phase_state == 4` and `noise_source == 0`. With `coherent_only`
enabled the block pauses output during noise-source calibration
automatically; expect a several-second gap after each retune.

### DoA MUSIC v2 (`krakensdr_v2.doa_music`)

MUSIC direction finder, C++/Eigen port of the kraken_doa_v2 algorithm.
Inputs: `num_elements` complex vectors of `vec_len` (must be sample-aligned;
decimate with **identical** filters per channel). Per input vector it outputs:

- `doa` — float[360] pseudospectrum (peak-normalized dB, floor −100, 1°/bin)
- `angle` — interpolated peak angle in degrees (optional output)
- `conf` — 0..1 confidence, peak-to-mean of the linear spectrum (optional)

Parameters: `vec_len`, `freq` (MHz, runtime-settable), `array_dist`
(UCA radius / ULA spacing in meters, runtime-settable), `num_elements`,
`array_type` (`"UCA"`/`"ULA"`, runtime-settable), `signal_dimension`
(assumed source count, default 1).

**Angle convention** (matches kraken_doa_v2, *not* the old V1 module): unit
circle — 0° is the antenna-0 direction (+x axis), counter-clockwise
positive. The UCA ring is expected to be wired **clockwise**, so element k
sits at −360·k/M degrees; that mirror is what keeps the reported azimuth
CCW. ULA uses the kraken_doa_v2 ULA frame (steering ∝ sin θ): 0° is
broadside and θ is ambiguous with 180°−θ. For a compass bearing:
`bearing = (array_offset − angle) mod 360` with whatever offset your array
mounting requires. The old V1 module shared the clockwise element ordering
but used a chord-based radius; results are **not** bit-identical.

### KrakenSDR v2 Cross Correlator (`krakensdr_v2.correlator`)

Zero-padded-FFT linear cross-correlation of two `vec_len` vectors (FFTW3f).
Outputs per vector: `xcorr` float[fft_cut] (peak-normalized dB, zero lag at
bin fft_cut/2), `phase` (degrees at zero lag), `lag` (peak lag in samples,
sub-sample interpolated; positive = input 1 delayed vs input 0). On a
calibrated KrakenSDR every pair shows lag ≈ 0 and a stable phase.

## Requirements

```
sudo apt install gnuradio-dev cmake build-essential pkg-config \
                 libeigen3-dev libfftw3-dev pybind11-dev
```

(GNU Radio ≥ 3.10. On the KrakenSDR Pi image everything is present.)

## Build and install

```bash
cd gr-krakensdr_v2
mkdir -p build && cd build

# --- Option A: system-wide (needs sudo) ---
cmake ..
make -j$(nproc)
sudo make install && sudo ldconfig

# --- Option B: user prefix, no sudo ---
cmake -DCMAKE_INSTALL_PREFIX=$HOME/.local \
      -DGR_PYTHON_DIR=$HOME/.local/lib/python3.13/site-packages ..
make -j$(nproc)
make install
```

(Adjust `python3.13` to your Python version. The binaries and bindings carry
an rpath, so `LD_LIBRARY_PATH` is not needed. The install prefix is **cached**
in the build directory — running `sudo make install` in an Option B build dir
still installs to `~/.local`, just root-owned; use a fresh build dir to switch
options.)

**GRC block discovery (Option B only)**: gnuradio-companion does not search
`~/.local` by default, so the blocks show as *Missing Block* unless you point
GRC at them. Persistent fix — add to `~/.config/gnuradio/config.conf`:

```ini
[grc]
global_blocks_path = /usr/share/gnuradio/grc/blocks:/usr/local/share/gnuradio/grc/blocks:/home/YOURUSER/.local/share/gnuradio/grc/blocks
```

(the key *overrides* the default, so keep the stock paths in the list).
Alternatively `. environment.sh` sets `GRC_BLOCKS_PATH` for the current shell
only — that does not help GRC launched from a desktop menu.

Self-tests (synthetic signals, no hardware): `cd build && ctest`

## Usage

Start heimdall_v2 first and let it calibrate (~30–60 s until the web UI /
status shows phase compensation CONVERGED):

```bash
cd ../heimdall_v2 && ./heimdall
```

> If heimdall fails with `undefined symbol: rtlsdr_set_dithering`, a stock
> `librtlsdr0` package is shadowing the KrakenSDR librtlsdr fork; run it as
> `LD_LIBRARY_PATH=/usr/local/lib ./heimdall`.

### C++ console examples (installed to `<prefix>/bin`)

```bash
kraken_fft_test   [--ip 127.0.0.1] [--freq 100.0] [--gain 40.2] [--seconds 0]
kraken_corr_test  [--ip ...] [--freq ...] [--gain ...] [--seconds ...]
kraken_music_doa  [--ip ...] [--freq ...] [--gain ...] [--radius 0.25]
                  [--array UCA|ULA] [--seconds ...]
```

- **kraken_fft_test** — FFTs all 5 channels, prints per-channel power and the
  3 strongest peaks once per second. Tune `--freq` to a busy band (e.g. FM
  broadcast) and check all channels see the same spectrum.
- **kraken_corr_test** — cross-correlates ch0 against ch1–ch4 and prints
  peak lag / zero-lag phase / peak quality once per second. Lag ≈ 0 samples
  with a stable phase on every pair = coherent. Runs with
  `coherent_only=false` so heimdall's calibration bursts are visible too; for
  a continuous strong test signal force the noise source on from the
  heimdall web UI (port 8070).
- **kraken_music_doa** — decimates to 100 kHz, runs MUSIC, prints the bearing,
  confidence and an ASCII pseudospectrum once per second. Requires the
  antenna array connected and calibration converged.

### GRC examples (`examples/`, GUI equivalents)

Open in gnuradio-companion (for user-prefix installs, set up block discovery
first — see *Build and install* above):

- `kraken_fft_display.grc` — 5× spectrum + waterfall, live freq/gain entry
- `kraken_music_doa.grc` — MUSIC pseudospectrum plot + DoA/confidence readout
- `kraken_correlator_test.grc` — 4× correlation plots + phase/lag readouts

## Differences from the V1 gr-krakensdr module

| | V1 (gr-krakensdr) | V2 (this module) |
|---|---|---|
| Server | heimdall DAQ fw (ports 5000/5001) | heimdall_v2 (ports 8091/8092) |
| Implementation | Python/numpy blocks | C++ (Eigen, FFTW3f) |
| Gain | per-channel gain vector | one gain for all tuners (server-side) |
| Cal frames | silently dropped | `coherent_only` switch + stream tags |
| Retune | manual, no recal awareness | triggers server cooldown + auto recal |
| DoA convention | clockwise elements, chord/λ radius | kraken_doa_v2 unit-circle CCW output, clockwise elements, radius/λ |
| DoA output | 360-bin dB spectrum | spectrum + interpolated angle + confidence |
| Correlator output | xcorr dB + phase | xcorr dB + phase + sub-sample lag |

## Ports / files

```
lib/                  block implementations (C++)
include/gnuradio/krakensdr_v2/   public block APIs
python/krakensdr_v2/  pybind11 bindings + QA self-tests
grc/                  GRC block definitions
apps/                 C++ console examples
examples/             GRC flowgraph examples
docs/                 protocol + design references
```
