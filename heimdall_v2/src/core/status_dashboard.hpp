#pragma once

// Live, in-place terminal status dashboard for the Heimdall server.
//
// Replaces the scrolling calibration/coherence log spew with a full-screen TUI
// that redraws in place a few times per second. Every parameter (RF tuning,
// per-channel lag/phase calibration, coherence watchdog, buffers, TCP clients,
// system load) is shown live and updated continuously; the server's own
// cout/cerr output is captured into a bounded ring and reduced to persistent
// ACTIVITY / ALERT lines so nothing scrolls the readout away.
//
// The dashboard only takes over when stdout is an interactive terminal
// (isatty). When output is a pipe/file/systemd journal, active() stays false
// and the plain scrolling logs are used instead, so a logfile never fills with
// ANSI escape garbage. The HEIMDALL_NO_TUI environment variable forces the
// plain path even on a terminal.
namespace StatusDashboard {

// True once begin() has decided the live TUI should run (stdout is a TTY and
// the opt-out env var is unset). Meaningful only after begin().
bool active();

// Enter the alternate screen, hide the cursor and start capturing cout/cerr.
// Call this EARLY (before device init) so startup messages are captured and
// shown on the loading screen. No-op when not attached to a terminal.
void begin();

// Signal that the pipeline is up (devices open, servers started). Until this is
// called, render() shows the startup/loading screen (what is loading); after,
// it shows the full dashboard (which reads per-device / correlation state).
void mark_ready();

// Draw one frame from the current live state. Call periodically (~4 Hz).
void render();

// Restore the terminal and stop capturing. Dumps the recent captured log to the
// restored screen so nothing is lost. Safe to call if begin() no-op'd.
void end();

}  // namespace StatusDashboard
