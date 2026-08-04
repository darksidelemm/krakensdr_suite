KrakenSDR V2 software. The new V2 software moves away from Python and implements everything in C++, which has resulted in massive speed improvements on slow hardware like the Pi 4 and Pi 5.

We are now able to have a real time spectrum and audio demodulation at the same time that the MUSIC DoA is run.

The KrakenSDR Suite V2 software is currently in beta.

Install:

```
git clone https://github.com/krakenrf/krakensdr_suite
cd krakensdr_suite
./install
```

You may need to reboot after running ./install.

To Run:

Simply use the command:

```
./run
```

In any terminal window, with your KrakenSDR connected and powered up.
