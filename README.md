iito - If Input, Then Output
----------------------------

Monitor input sources, and reflect their state on output sinks.

Inputs are things like file attributes (e.g. presence or absence) or
device properties (e.g. the online status of a power supply). The
canonical output sink is an LED.

## Usage

**TODO:** Describe config file and behavior

## Building and Installing

iito uses Autotools, so the procdure is hopefully familiar to many.

If you are building from a cloned GIT repo (as opposed to a release
tarball), you have to start by generating the configure script:

```sh
~/iito$ ./autogen.sh
```

To build and install with the default settings:

```sh
~/iito$ ./configure && make && sudo make install
```

## Testing

As the test suite needs to create virtual LEDs, dummy network device,
etc., it requires root privileges to run. A suitable incantation is
therefore typically:

```sh
~/iito$ sudo make check
```
