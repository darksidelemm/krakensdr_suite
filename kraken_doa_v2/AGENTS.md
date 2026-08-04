# Repository Guidelines

## Project Structure & Module Organization
- `src/` holds runtime networking, signal-processing pipelines, and the entrypoint in `main.cpp`.
- `include/` contains the public headers; keep synchronization notes near shared data declarations.
- `uWebSockets/` is a vendored dependency; treat it as read-only.
- `kraken_doa/` plus `kraken_doa.html` power the browser UI; align protocol updates with `src/message_builders.cpp`.
- TLS material lives in `server.crt` and `server.key`; replace for non-lab deployments.

## Build, Test, and Development Commands
- `make deps` installs required packages and fetches `uWebSockets/`.
- `make` or `make -j` produces the optimized `kraken_doa` binary in the repo root.
- `make run` builds if needed and launches `./kraken_doa`.
- `make debug` adds symbols for `gdb ./kraken_doa`.
- `make test-build` performs dependency and compile checks.
- `make clean` drops `obj/`, `dep/`, and the binary.

## Coding Style & Naming Conventions
- Target C++20, 4-space indentation, minimal `auto`, and trailing commas only where standard.
- Use PascalCase for types and snake_case for functions/variables (`initialize_persistent_buffer`).
- Keep modules grouped under `networking/`, `signal_processing/`, and `utils/`; declare new worker threads alongside other globals in `src/main.cpp`.
- Protect shared state with the existing mutexes and favor RAII over raw pointers.

## Testing Guidelines
- Rely on `make test-build` for quick regression coverage until automated tests exist.
- For runtime checks, run `./kraken_doa` and load `kraken_doa.html` via HTTPS on `WEB_PORT`, confirming FFT, DoA, and FM respond to channel changes.
- When extending DSP paths, gate temporary logging behind the `DEBUG_*` macros and record manual validation in `notes.txt`.

## Commit & Pull Request Guidelines
- Follow history conventions: concise, present-tense subjects under ~72 characters (`add second decimator`).
- Rebase before a PR, describe behavior changes and validation steps, and link issue IDs.
- Attach screenshots or bandwidth/latency metrics when altering UI or performance.

## Security & Configuration Tips
- Adjust defaults in `include/config.hpp`; never commit secrets.
- Regenerate TLS assets with `openssl req -new -x509 ...` before production distribution.
- Audit thread entry points for exception safety so long-lived workers remain alive.
