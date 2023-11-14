#!/bin/bash

testdir=$(dirname $(readlink -f $0))

set -e

die()
{
    echo "$@" >&2
    exit 1
}

ledcmp()
{
    [ "$1" =  "x" ] && return 0
    [ "$1" = "$2" ] && return 0

    return 1
}

uled()
{
    case $1 in
	start)
	    exec 10< <($testdir/uled)
	    uled expect 0 0 0 0
	    ;;
	stop)
	    exec 10<&-
	    ;;
	expect)
	    shift
	    while read -u 10 -t 3 -a led; do
		ledcmp $1 ${led[0]} && \
		ledcmp $2 ${led[1]} && \
		ledcmp $3 ${led[2]} && \
		ledcmp $4 ${led[3]} && \
		return 0

		last=("${led[@]}")
	    done

	    [ "${last[*]}" ] || die "Unable to read state, permission issues?"

	    echo "Expected state ($1 $2 $3 $4) does not match current state (${last[*]})" >&2
	    return 1
	    ;;
	*)
	    die "Unknown uled command"
	    ;;
    esac
}

test_self()
{
    # Just make sure that we can control the LEDs without iiotd's
    # involvement, mainly to rule out missing kernel modules and
    # permission issues.

    echo "Enable led0"
    echo 1 >'/sys/class/leds/iito-test::0/brightness'
    uled expect 1 0 0 0

    echo "Disable led0"
    echo 0 >'/sys/class/leds/iito-test::0/brightness'
    uled expect 0 0 0 0
}

test_path()
{
    f1=$(mktemp)
    f2=$(mktemp)

    $IITOD <<EOF &
{
	"input": {
		"path": {
			"f1": { "path": "${f1}" },
			"f2": { "path": "${f2}" }
		}
	},

	"output": {
		"led": {
			"iito-test::1": {
				"rules": [
					{ "if": "!f2", "then": { "brightness": true } },
					{ "if":  "f1", "then": { "brightness": 1 } },
					{ "if": "!f1", "then": { "brightness": 2 } }
				]
			},
			"iito-test::2": {
				"rules": [
					{ "if": "!f2", "then": { "brightness": true } }
				]
			}
		}
	}
}
EOF
    pid=$!

    echo "Both f1 and f2 exists"
    uled expect x 1 0 x || return 1

    echo "Remove f1"
    rm $f1
    uled expect x 2 0 x || return 1

    echo "Remove f2"
    rm $f2
    uled expect x 15 127 x || return 1

    kill $pid
    wait $pid || true
}

test_udev()
{
    $IITOD <<EOF &
{
	"input": {
		"udev": {
			"dummy": { "subsystem": "net", "sysname": "iito-test" }
		}
	},

	"output": {
		"led": {
			"iito-test::0": {
				"rules": [
					{ "if": "dummy", "then": {} }
				]
			}
		}
	}
}
EOF
    pid=$!


    echo "Create dummy interface"
    ip link add dev iito-test type dummy
    uled expect 1 x x x || return 1

    echo "Remove dummy interface"
    ip link del dev iito-test
    uled expect 0 x x x || return 1

    kill $pid
    wait $pid || true
}

test_alias()
{
    f1=$(mktemp)
    f2=$(mktemp)

    $IITOD <<EOF &
{
	"input": {
		"path": {
			"f1": { "path": "${f1}" },
			"f2": { "path": "${f2}" }
		}
	},

	"output": {
		"led": {
			"iito-test::1": {
				"rules": [
					{ "if": "!f2", "then": "@full" },
					{ "if":  "f1", "then": { "brightness": 1 } },
					{ "if": "!f1", "then": "@two" }
				]
			},
			"iito-test::2": {
				"rules": [
					{ "if": "!f2", "then": "@full" }
				]
			}
		}
	},

	"aliases": {
		"two": { "brightness": 2 },
		"full": { "brightness": true }
	}
}
EOF
    pid=$!

    echo "Both f1 and f2 exists"
    uled expect x 1 0 x || return 1

    echo "Remove f1"
    rm $f1
    uled expect x 2 0 x || return 1

    echo "Remove f2"
    rm $f2
    uled expect x 15 127 x || return 1

    kill $pid
    wait $pid || true
}

modprobe uleds || die "uleds module not available"

[ "$IITOD" ] || die "\$IITOD is not set"

for t in self path udev alias; do
    uled start

    printf ">>> START \"%s\"\n" "$t"
    test_$t || {
	printf "<<< FAIL \"%s\"\n" $t;
	uled stop
	exit 1;
    }
    printf "<<< PASS \"%s\"\n" "$t"

    uled stop
done

