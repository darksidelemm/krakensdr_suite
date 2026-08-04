# gr-krakensdr_v2 Design Specification

GNU Radio 3.10 OOT module for the Heimdall v2 coherent receiver
(`~/krakensdr_v2/heimdall_v2`). Replaces the Python-based V1 module
(`~/gr-krakensdr`) with native C++ blocks speaking heimdall_v2's TCP
protocol (data port 8091, JSON control port 8092).

Companion references in this directory (authoritative, derived from source):
- `heimdall_v2_data_protocol.md`  — port 8091 wire format + reference client behavior
- `heimdall_v2_control_protocol.md` — port 8092 JSON commands + status broadcast
- `music_reference.md` — kraken_doa_v2 MUSIC algorithm details

## Module naming

- GR module: `krakensdr_v2` (python: `gnuradio.krakensdr_v2`, lib: `libgnuradio-krakensdr_v2`)
- GRC block ids: `krakensdr_v2_krakensdr_source`, `krakensdr_v2_doa_music`,
  `krakensdr_v2_correlator`; GRC category `[KrakenSDR v2]`
- C++ namespace: `gr::krakensdr_v2`

## Wire protocol essentials (port 8091)

Packet = header + payload, streamed back-to-back. A new connection always
starts on a packet boundary (server writes whole packets per client), but the
parser must still resync on magic after any desync.

| Offset | Field | Type / endianness |
|---|---|---|
| +0  | magic = `0x4D434851` ('MCHQ') | uint32 **big-endian** |
| +4  | num_channels | uint32 BE |
| +8  | samples_per_channel | uint32 BE |
| +12 | phase_state (enum below) | uint32 BE |
| +16 | noise_source (1 = cal noise routed in) | uint32 BE |
| +20 | freq_change_counter | uint32 BE |
| +24 | current_group_index | uint32 BE |
| +28 | retuning_in_progress | uint32 BE |
| +32 + 8·ch | channel ch frequency in Hz | float32 **little-endian** |
| +36 + 8·ch | channel ch gain in dB (−1 = auto) | float32 LE |
| +32 + 8·N | payload: N channels × samples × 2 bytes, channel-major, interleaved I,Q uint8 (128 ≈ 0) | uint8 |

Defaults on this system: N=5 channels, 16384 samples, 2.4 MSPS → ~146.5
packets/s, ~24 MB/s. Sanity-gate header values (channels 1..16, samples
1..65536) before trusting a magic match; on failure advance 1 byte and rescan.

uint8 → float conversion: `(b − 127.5) / 127.5`; with DC correction, subtract
the per-channel per-packet mean of the raw uint8 values instead of 127.5, then
scale by 1/127.5 (matches kraken_doa_v2).

`PhaseCompensatorState` (heimdall types.hpp): 0 WAITING_FOR_LAG_COMPLETION,
1 MEASURING_INITIAL_PHASE, 2 APPLYING_COMPENSATION (legacy), 3
VERIFYING_CONVERGENCE, **4 CONVERGED**, 5 WAITING_FOR_STABILITY (post-retune
cooldown, identity calibration), 6 MEASURING_PER_BIN.

Data-validity rule: samples are calibrated antenna data iff
`phase_state == 4 && noise_source == 0 && retuning_in_progress == 0`.
During states 1/3/6 or `noise_source==1` the channels carry calibration noise;
during state 5 they carry uncalibrated (identity) antenna data.

**Server drops slow clients**: the data server's sockets are non-blocking; a
short/failed `send()` marks the client inactive. The reader thread must never
stall — always drain the socket and drop whole frames internally when
downstream is slow.

## Control protocol essentials (port 8092)

- Send single-line JSON + `\n`. The server extracts values by substring search
  (`"frequency":`, `"gain":`), so the canonical flat shape works:
  - `{"command":"set_frequency","frequency":100000000}` (integer Hz, 24e6–1766e6)
  - `{"command":"set_gain","gain":40.2}` (dB float; negative → AGC; 0–50 valid)
- Every processed command gets a one-line JSON response with `"status"`.
  The server ALSO broadcasts a status JSON (top-level `"settings"` key, no
  `"status"` key) every 500 ms to all connected control clients. **The control
  socket must be drained continuously** or the server may drop the client.
- A frequency or gain change triggers: noise source OFF → ~3 s cooldown
  (phase_state 5, identity cal) → noise ON + phase recal (states 1→3→4) →
  noise OFF, converged. Re-sending during cooldown restarts the timer.
- There is NO command to force the noise source on via 8092. The web UI
  (port 8070) WebSocket accepts `BIAS_TEE_ENABLE` / `BIAS_TEE_DISABLE`.
- `set_freq_offset` does NOT exist in heimdall (dead command in the reference
  client). No per-channel gain: `set_gain` applies to all tuners.

## Blocks

### 1. `krakensdr_source` — files `lib/krakensdr_source_impl.{h,cc}`

`gr::sync_block`, no inputs, `num_channels` outputs of `gr_complex` (plain
streams, vlen 1 — examples re-vectorize with `stream_to_vector`, like V1).

```cpp
static sptr make(const std::string& ip_addr = "127.0.0.1",
                 int data_port = 8091,
                 int ctrl_port = 8092,
                 int num_channels = 5,
                 double freq_mhz = 100.0,
                 double gain_db = 40.2,
                 bool coherent_only = true,
                 bool dc_correction = true,
                 bool debug = false);
virtual void set_freq(double freq_mhz) = 0;     // GRC callback
virtual void set_gain(double gain_db) = 0;      // GRC callback
virtual uint32_t phase_state() const = 0;       // last seen header value
virtual bool noise_source_active() const = 0;
virtual double center_freq_hz() const = 0;      // from ch0 packet metadata
```

Threading (all joined in `stop()`, guarded by `std::atomic<bool> d_running`):

- **Reader thread**: connect loop to `ip:data_port` (1 s retry backoff,
  `SO_RCVTIMEO` 500 ms, reset parse buffer on each new connection). Userspace
  accumulation buffer 4 MiB. Parse loop identical to the reference client:
  resync-on-magic, sanity gates, wait for full packet, decode, advance.
  - Frame filtering when `coherent_only == true`: drop frames with
    `noise_source == 1` or `retuning_in_progress == 1` (keep state-5 cooldown
    frames: uncalibrated but continuous antenna data — matches spectrum-display
    semantics; DoA consumers gate further via tags if needed).
    When `coherent_only == false` every frame is passed.
  - If packet `num_channels < block num_channels`: drop frame + throttled
    (5 s) log. If more: use the first `num_channels`.
  - Convert uint8→gr_complex per the formula above into a `Frame`
    (per-channel contiguous `std::vector<gr_complex>` + metadata: freq Hz,
    gain, phase_state, noise, retuning). Recycle Frame buffers through a
    small freelist (mutex-protected stack) to avoid 24 MB/s of allocation.
  - Push into bounded frame queue (`std::deque` + mutex + condvar, cap 16);
    when full, **drop the oldest** frame (freshness wins), count drops,
    throttled log.
- **Control thread**: lazy connect to `ip:ctrl_port` (retry 5 s). On (re)connect
  send `set_frequency` (from `freq_mhz`, as integer Hz) then `set_gain`.
  Loop: `poll()` 200 ms; drain incoming lines (parse `"status":"error"` →
  log via `d_logger`; in debug mode log broadcasts' `center_freq`/`gain`);
  send any queued commands (from `set_freq`/`set_gain` callbacks, which are
  callable from the GUI thread → command queue + mutex, never block).
- **work()**: stream out the current frame across calls
  (`min(noutput_items, remaining)` per channel, memcpy). When exhausted, pop
  the next frame (wait on condvar up to 250 ms; if none, `return 0`).
  On the FIRST output item of each frame, if metadata changed vs the previous
  frame (or first frame), `add_item_tag` on **all** output streams at the
  absolute offset: keys `rx_freq` (double, Hz), `phase_state` (long),
  `noise_source` (bool). Cheap: only on change.

`stop()` override: clear `d_running`, close sockets (shutdown() to unblock
recv), notify condvar, join threads. Also implement destructor safety.
Use `d_logger`/`GR_LOG_*` (thread-safe) for all logging; throttle anything
per-packet-rate to ≥5 s.

### 2. `doa_music` — files `lib/doa_music_impl.{h,cc}`

`gr::sync_block`. Inputs: `num_elements` ports of `gr_complex` vectors,
vlen = `vec_len`. Outputs (1 item out per input item, process EVERY item in
the work call — do not repeat V1's "only item 0" bug):
- out0 `doa`: float32 vector, vlen 360 — MUSIC pseudospectrum, normalized to
  peak = 0 dB, `10*log10`, floored at −100 dB (same display convention as V1).
- out1 `angle`: float32 scalar — interpolated peak angle in degrees [0,360),
  **unit-circle convention: 0° = +x = ANT0 direction, CCW positive** (matches
  kraken_doa_v2; NOT the V1 mirrored convention).
- out2 `conf`: float32 scalar — confidence `min(1, (peak/mean − 1)/3)` of the
  linear pseudospectrum.

```cpp
static sptr make(int vec_len,
                 double freq_mhz,
                 double array_dist_m = 0.25,   // UCA radius / ULA spacing, meters
                 int num_elements = 5,
                 const std::string& array_type = "UCA",  // "UCA" | "ULA"
                 int signal_dimension = 1);    // 1..num_elements-1
virtual void set_freq(double freq_mhz) = 0;    // GRC callback, regenerates steering
```

Algorithm (Eigen3, double precision internally — `MatrixXcd`; matches
kraken_doa_v2 conventions, see `music_reference.md`):
1. Stack inputs into `X` (M × vec_len, upcast to complex<double>).
2. `R = X·Xᴴ / vec_len` (use `selfadjointView` rankUpdate), trace-normalize
   (`R /= trace(R)` if > 1e-15), add `1e-12` to the diagonal.
3. `Eigen::SelfAdjointEigenSolver<MatrixXcd>` (ascending eigenvalues); noise
   subspace `E` = first `M − signal_dimension` eigenvectors.
4. Steering matrix `S` (M × 360, precomputed, regenerated on `set_freq`,
   guarded by mutex vs work thread):
   - λ = c / f, `r = array_dist_m / λ`
   - UCA: element k at angle `-2πk/M` (**clockwise ring** — v2 convention,
     `UCA_ANGLE_SIGN`; the mirror keeps the reported azimuth CCW),
     position `(r·cos, r·sin)`
   - ULA: `x_k = k·r`, `y_k = 0`; steering phase uses
     `2π·x_k·sin(θ)` (v2 ULA convention)
   - UCA/generic: phase = `2π·(x·cosθ + y·sinθ)`; element value
     `exp(+j·phase)`; **each column normalized to unit norm**
   - Grid: θ = 0..359° at 1°.
5. Pseudospectrum `P(θ) = 1 / max(‖Eᴴ·s(θ)‖², 1e-15)` (with unit-norm s this
   is classic MUSIC).
6. Peak: global argmax + 3-point parabolic refinement on `ln P` with circular
   indexing, clamp delta ±0.5 bins.
7. Output transform for out0: `10·log10(P/Pmax)` floored at −100.

Notes: pre-allocate all Eigen work matrices in the constructor. `vec_len`
is the snapshot count N (examples feed decimated data). No FB averaging, no
spatial smoothing (parity with kraken_doa_v2 defaults).

### 3. `correlator` — files `lib/correlator_impl.{h,cc}`

`gr::sync_block`. Inputs: 2 ports `gr_complex` vectors vlen = `vec_len`.
Outputs (process every item):
- out0 `xcorr`: float32 vector vlen `fft_cut` — cross-correlation magnitude in
  dB, normalized peak = 0 dB, centered on zero lag (bin `fft_cut/2` = lag 0).
- out1 `phase`: float32 — phase at the zero-lag bin, degrees.
- out2 `lag`: float32 — lag of the correlation peak in samples (sub-sample,
  3-point parabolic on magnitude; positive = input1 delayed vs input0).

```cpp
static sptr make(int vec_len, int fft_cut = 2048);
```

Computation (FFTW3f single precision, same math as V1):
```
N = vec_len;   L = 2N
X = FFT_L( [x[0..N-1], zeros(N)] )
Y = FFT_L( [zeros(N), y[0..N-1]] )
xc = IFFT_L( conj(X) · Y )            // zero lag at index N
out0 = 10·log10(|xc[N−fft_cut/2 .. N+fft_cut/2−1]|) − max(...)
out1 = arg(xc[N]) · 180/π
out2 = (argmax_k |xc[k]| ) − N  + parabolic sub-sample delta
```
Plans: `fftwf_plan_dft_1d` × 3 (fwd ×2 reuse one plan on separate buffers, or
FFTW_ESTIMATE new-array; simplest: two in-place fwd plans + one inverse),
created in ctor with a static mutex around plan creation (FFTW planner is not
thread-safe), `fftwf_malloc`-aligned buffers, destroyed in dtor. Scale the
IFFT by 1/L. Guard log10 with a small epsilon (1e-20). No NaN writes.

## Python bindings — `python/krakensdr_v2/bindings/`

Hand-written pybind11 (pygccxml is NOT available on this machine; do not use
gr_modtool bind). Files: `python_bindings.cc` (register all three) +
`krakensdr_source_python.cc`, `doa_music_python.cc`, `correlator_python.cc`.
Follow the standard GR 3.10 OOT binding pattern (`#include <pybind11/...>`,
`bind_krakensdr_source(py::module& m)` with `py::class_<..., gr::sync_block,
std::shared_ptr<...>>`, expose `make` as static, expose setters/getters).
`python/krakensdr_v2/__init__.py`: standard OOT loader (`from .krakensdr_v2_python import *`).

## GRC block YAMLs — `grc/`

- `krakensdr_v2_krakensdr_source.block.yml`: label "KrakenSDR v2 Source";
  params ip_addr (str), data_port (int 8091), ctrl_port (int 8092),
  num_channels (int 5), freq (real, MHz, default 100.0), gain (real dB 40.2),
  coherent_only (bool true), dc_correction (bool true), debug (bool false).
  Callbacks: `set_freq(${freq})`, `set_gain(${gain})`. Outputs: multiplicity
  `${num_channels}`, label ch, complex. Assert num_channels >= 1.
- `krakensdr_v2_doa_music.block.yml`: params vec_len, freq (MHz), array_dist,
  num_elements, array_type (enum UCA/ULA), signal_dimension. Callback
  `set_freq(${freq})`. Inputs multiplicity num_elements complex vlen vec_len;
  outputs: doa float vlen 360; angle float (optional: 1); conf float
  (optional: 1).
- `krakensdr_v2_correlator.block.yml`: params vec_len, fft_cut. 2 complex
  vector inputs; outputs xcorr float vlen fft_cut, phase float (optional),
  lag float (optional).

Python-output templates only (no cpp templates). Every block documentation
string should explain the heimdall_v2 context.

## Examples

### C++ console apps — `apps/` (built and installed by CMake)

All three: `boost::program_options`-free — parse simple argv by hand
(`--ip`, `--freq`, `--gain`, `--seconds` etc., with defaults); build a
`gr::top_block`, run for a duration (or until Ctrl-C via sigint handler →
`tb->stop()`), print results to stdout at ~1 Hz. Console sinks are small
in-app `gr::sync_block` subclasses (C++ OOT-style, defined in the .cc).

1. `kraken_fft_test.cc`: source → per channel: `stream_to_vector(16384)` →
   `gr::fft::fft_v<gr_complex,true>` (Blackman-Harris window) →
   console sink computing `10log10(|.|²)`: prints, per channel, total power
   and the 3 strongest bins as frequency offsets from center (MHz) once per
   second. Validates: all 5 channels alive, same spectrum shape.
2. `kraken_corr_test.cc`: source (`coherent_only=false`) → 5×
   `stream_to_vector(16384)` → 4× `correlator(16384, 2048)` (ch0×ch1..ch0×ch4)
   → console sink printing per pair: peak lag (samples), peak-to-median dB,
   zero-lag phase (deg) once per second. Validates coherence: |lag| < 1 sample
   and stable phase after calibration.
3. `kraken_music_doa.cc`: source → 5× (`freq_xlating_fir_filter_ccf` or plain
   `fir_filter_ccf` low-pass, decim 24, cutoff 40 kHz, transition 20 kHz,
   identical taps for all channels) → `stream_to_vector(2048)` →
   `doa_music(2048, freq, radius, 5, "UCA", 1)` → console sink printing
   angle°, confidence, and a coarse 36-char ASCII pseudospectrum once per
   second.

### GRC flowgraphs — `examples/` (mirroring the V1 examples)

1. `kraken_fft_display.grc`: source (params via qtgui entry widgets: freq MHz
   default 100.0, gain default 40.2) → 5× `qtgui_sink_c` (fftsize 16384,
   Blackman-Harris, fc=freq·1e6, bw 2.4e6, freq+waterfall).
2. `kraken_music_doa.grc`: source → 5× `fir_filter_ccf` low-pass (decim 24,
   firdes.low_pass(1, samp_rate, 40e3, 20e3)) → `stream_to_vector(2048)` →
   `doa_music(2048, freq, 0.25, 5, UCA, 1)` → out0 → `qtgui_vector_sink_f`
   (vlen 360, x-axis 0–359°, y −100..0 dB); out1 → `qtgui_number_sink`
   (0–360); out2 → `qtgui_number_sink` (0–1).
3. `kraken_correlator_test.grc`: source (coherent_only=false) → 5×
   `stream_to_vector(16384)` → 4× correlator(16384, 4096) → 4×
   `qtgui_vector_sink_f` (x from −2048 step 1, y −50..0) + phase →
   `qtgui_number_sink` (−180..180) + lag → `qtgui_number_sink` (−100..100).
   Note in the flowgraph title/docs: noise source can be forced on from the
   heimdall web UI (port 8070) for a hard coherence test; otherwise antenna
   signals show the correlation.

GRC format: GNU Radio 3.10 `.grc` YAML — copy structural conventions from
`~/gr-krakensdr/examples/*.grc` (options block, connections lists) but with
`gnuradio-companion` 3.10.12 field conventions.

## Build system

- Top-level CMake: modtool skeleton + `find_package(Eigen3 REQUIRED)` +
  FFTW3f (via GR's FindFFTW3f or pkg-config `fftw3f`) + Threads.
- `lib/`: builds the three impls into `gnuradio-krakensdr_v2`; links
  `gnuradio::gnuradio-runtime`, `gnuradio::gnuradio-fft` (window functions),
  Eigen3::Eigen, fftw3f, Threads::Threads.
- `apps/`: three executables linking the module lib + gnuradio-blocks,
  gnuradio-filter, gnuradio-fft.
- Everything must build with the system GNU Radio 3.10.12 / gcc 14 / cmake
  3.31 on this Pi 5 (aarch64). `-O3` release default. No sudo assumed:
  document `cmake -DCMAKE_INSTALL_PREFIX=~/.local
  -DGR_PYTHON_DIR=~/.local/lib/python3.13/site-packages`, and
  `GRC_BLOCKS_PATH`/`LD_LIBRARY_PATH` env for user installs (provide
  `environment.sh`).
