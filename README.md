# KIMup

[Another Old VCR Artifact of Love!](http://oldvcr.blogspot.com/)

Copyright (C)2023 Cameron Kaiser.  
All rights reserved.  
Provided under the Floodgap Free Software License.

## What it does

KIMup is a serial uploader for the MOS KIM-1 (later Commodore KIM-1), the first computer to use the MOS 6502 8-bit CPU, with 1K (actually 1152 bytes) of RAM running at 1MHz. The KIM's built-in serial routines support loading binary data from paper tape. What this does is allow you to (mostly) automatically push your cross-developed binary to the KIM using those routines (KIMup will translate your binary on the fly) and optionally immediately start execution.

KIMup can upload one or multiple binary files in one transaction, and can also upload papertape for you without manually doing it in a terminal program, if you already have a compatible papertape file.

## How to build

KIMup is a single file program written entirely in basic C and only depends on POSIX and `termios.h`. It has been tested on Fedora Linux (`ppc64le`) and Mac OS X Tiger (PowerPC), but should work most anywhere that's Unix-like.

To compile it, just `gcc -O3 -o kimup kimup.c` or your compatible compiler of choice. If your OS does not provide `strol()`, you can substitute `atoi()`, though you may not be able to pass hexadecimal arguments.

## How to use

By default, KIMup will try to send to `/dev/ttyUSB0` if available, then to `/dev/cu.usbserial` if available, then fail. You can force a particular serial device by setting the environment variable `SERIALPORT` to its path. The device or port must be capable of raw transmissions at 300 baud.

KIMup expects pairs of options, first an address, then the file. For example, this will upload two files to a connected KIM, the first to address `$0000` and the second to address `$0200`:

```
kimup 0 lo.bin 512 hi.bin
```

You can also specify the addresses in hexadecimal:

```
kimup 0x0000 lo.bin 0x0200 hi.bin
```

KIMup will wait for the KIM to respond with its usual serial banner. You may need to jumper pins 21 and V to force TTY mode if this is not already done by your serial card (for example, Bob Applegate's Corsham Tech cards have a jumper or DIP switch), and you may also need to press RS to get the KIM's attention if the transfer is not progressing. It will then send the file(s) and wait for the KIM-1 to signal the transmission was accepted, and report accordingly.

Transfers are done at 300 baud for maximum reliability. KIMup will try to detect if the KIM is not responding correctly to the transmission and abort, though you can cancel at any time by pressing CTRL-C. Note that a partially sent transmission is not generally recoverable, and it is simpler just to try again from the beginning.

KIMup can also run programs for you, and/or upload previously converted ones that you might already have as text files (such as the ones from _The First Book of KIM_). If you provide `-g address` as the first argument, then the KIM-1 will automatically start ("go") from that address, e.g.,

```
kimup -g 0 0 lo.bin 512 hi.bin
```

If you provide `-p filename` instead of an address and filename, then `filename` is taken as a papertape file and sent verbatim to the KIM-1. As papertape files encode their maximal length, it must be the only file you specify. For example, this uploads a papertape file and then starts running from location `$02aa`:

```
kimup -g 0x2aa -p somethin.txt
```

## Bug reports

Please compile KIMup with `-DDEBUG` first and report any output. If you don't do this, I may mark your report invalid or remove it. Nothing personal.

Issues I can't reproduce and/or requests for features that don't have an associated pull request may be closed or removed. Again, nothing personal.

## Pull requests

Refactors will **not** be accepted (fork the project). New features **may** be accepted, as long as I like them (if not, fork the project).
