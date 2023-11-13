iito - If Input, Then Output
----------------------------

Monitor input sources, and reflect their state on output sinks.

Inputs are things like file attributes (e.g. presence or absence) or
device properties (e.g. the online status of a power supply). The
canonical output sink is an LED.


## Usage

By default, the iito daemon, `iitod`, will its configuration file
`iitod.json` from the configured `--sysconfdir` (typically `/etc` or
`/usr/local/etc`). In this scenario, `iitod` can be started without
any options:

```sh
~# iitod
```

The runtime behavior is wholly determined by the contents of its JSON
config file. The top-level object contains one `input` and one
`output` which, in turn, contain one object per input/output
driver. Each driver object then contains all the objects of that type.

In addition to any driver specific config options, output devices also
define a list of `rules`. Each rule defines a condition that must be
met in order for the rule to match via its `if` property. When a
matching rule is found, the contents rule's `then` property is applied
to the output device in question.

### Example:

Let's say we have a system with 4 pairs of red/green LEDs:

```
.------------- - - -
| Foobar2000
|
| (@) BOOT
| (@) STAT
|
| (@) PWR1
| (@) PWR2
'------------- - - -
```

Both `BOOT` and `STAT` are fully software controlled. The red channel
of each power LED is also software controlled while the green channel
is hardwired to the source's power-good signal.

Here is an example `iitod` configuration for this system:

```json
{
	"input": {
		"path": {
			"here-i-am": { "path": "/run/here-i-am" },
			"panic": { "path": "/run/panic" },
			"boot-ok": { "path": "/run/boot-ok" },
			"mydaemon": { "path": "/run/mydaemon.pid" }
		},
		"udev": {
			"power-1": { "subsystem": "power_supply" },
			"power-2": { "subsystem": "power_supply" }
		}
	},

	"output": {
		"led": {
			"red:boot": {
				"rules": [
					{ "if": "here-i-am", "then": { "brightness": false } },
					{ "if": "panic", "then": { "trigger": "timer" } },
					{ "if": "!boot-ok", "then": { "trigger": "timer" } }
				]
			},
			"green:boot": {
				"rules": [
					{ "if": "here-i-am", "then": { "trigger": "timer" } },
					{ "if": "panic", "then": { "brightness": false } },
					{ "if": "boot-ok", "then": { "brightness": true } }
				]
			},
			"red:status": {
				"rules": [
					{ "if": "here-i-am", "then": { "brightness": false } },
					{ "if": "panic", "then": { "trigger": "timer" } },
					{ "if": "!mydaemon", "then": { "trigger": "timer" } }
				]
			},
			"green:status": {
				"rules": [
					{ "if": "here-i-am", "then": { "trigger": "timer" } },
					{ "if": "panic", "then": { "brightness": false } },
					{ "if": "mydaemon", "then":
						{
							"trigger": "timer",
							"delay_on": 900,
							"delay_off": 100
						}
					}
				]
			},

			"red:power-1": {
				"rules": [
					{ "if": "panic", "then": { "trigger": "timer" } },
					{ "if": "!power-1:online", "then": { "trigger": "timer" } }
				]
			},
			"red:power-2": {
				"rules": [
					{ "if": "panic", "then": { "trigger": "timer" } },
					{ "if": "!power-1:online", "then": { "trigger": "timer" } }
				]
			}
		}
	}
}

```

In this example, we define a handful of marker file inputs in
`/run`. These are created and destroyed by other components in the
system, e.g. `/run/mydaemon.pid` is likely managed by the `mydaemon`
process, others may be created by init scripts, etc.

Some observations about rules:
- They are evaluated in-order. The first condition to match is applied
  to the output.
- Conditions may be inverted by prepending a `!` to it.
- Input devices may support multiple _properties_, e.g. `online` in
  the case of power supplies. If a condition does not specify any
  property, the input's default property is used.
- If no rule matches, the output reverts to a default state determined
  by the driver.

#### _BOOT_ and _STAT_ Behavior

There are two global states that control both LEDs in unison:

| Cond        | _BOOT_ & _STAT_ |
|-------------|-----------------|
| `here-i-am` | Blink green     |
| `panic`     | Blink red       |

If none of these apply, the _BOOT_ LED is controlled by the `boot-ok`
marker file:

| `boot-ok` | _BOOT_      |
|-----------|-------------|
| `true`    | Solid green |
| `false`   | Blink red   |

The _STAT_ LED is tied to the running status of `mydaemon`:

| `mydaemon` | _STAT_                       |
|------------|------------------------------|
| `true`     | Blink green (90% duty cycle) |
| `false`    | Blink red                    |


#### _PWR1_ / _PWR2_ Behavior

In case of a `panic` condition, both LEDs blink red in unison with
_BOOT_ and _STAT_. Otherwise, a power LED will blink red in the case
when their respective power supply is offline (`!online`). When none
of these rules match, the LED will show a solid green, since an LEDs
default rule is "off", and the green LEDs are hardwired to "on".


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
