#!/usr/bin/env bash
# Runs real flogg processes against each other. Usage: named_windows_e2e.sh <flogg binary>
set -u

FLOGG=$1
export QT_QPA_PLATFORM=offscreen

# flogg keeps the sockets of the named windows under the home folder
HOME=$(mktemp -d)
export HOME
SOCKETS="$HOME/.flogg-windows"

pids=()
failures=0

cleanup() {
    kill -CONT "${pids[@]}" 2>/dev/null
    kill -9 "${pids[@]}" 2>/dev/null
    wait 2>/dev/null
    rm -rf "$HOME"
}
trap cleanup EXIT

check() {
    if [ "$2" = "$3" ]; then
        echo "ok: $1"
    else
        echo "FAILED: $1 (expected $3, got $2)"
        failures=$((failures + 1))
    fi
}

run() {
    "$FLOGG" -platform offscreen --config "$HOME/flogg.conf" "$@" >/dev/null 2>&1
}

start() {
    # exec: $! has to be flogg itself, the signals below are meant for it
    (exec "$FLOGG" -platform offscreen --config "$HOME/flogg.conf" "$@" >/dev/null 2>&1) &
    started=$!
    pids+=("$started")
}

alive() {
    kill -0 "$1" 2>/dev/null && echo yes || echo no
}

socket_count() {
    find "$SOCKETS" -type s 2>/dev/null | wc -l | tr -d ' '
}

wait_for_sockets() {
    for _ in $(seq 1 100); do
        [ "$(socket_count)" -ge "$1" ] && return
        perl -e 'select(undef, undef, undef, 0.1)'
    done
}

# Unlike wait_for_sockets it skips the socket a killed process left behind
wait_for_listeners() {
    for _ in $(seq 1 100); do
        listeners=0
        for socket in "$SOCKETS"/*; do
            [ -S "$socket" ] && perl -MIO::Socket::UNIX -e \
                'exit !IO::Socket::UNIX->new(Peer => $ARGV[0])' "$socket" \
                && listeners=$((listeners + 1))
        done
        [ "$listeners" -ge "$1" ] && return
        perl -e 'select(undef, undef, undef, 0.1)'
    done
}

a_log="$HOME/a.log"
b_log="$HOME/b.log"
printf 'line\n' >"$a_log"
printf 'line\n' >"$b_log"

start --window A "$a_log"
window_a=$started
wait_for_sockets 1
check "a new name stays open as a window" "$(alive "$window_a")" yes
check "the window listens on one socket" "$(socket_count)" 1

run --window A "$b_log"
check "a running window takes the file" $? 0
check "the window is still there" "$(alive "$window_a")" yes
check "no second window was made" "$(socket_count)" 1

run --window " a "
check "the name ignores case and spaces" $? 0
check "still one window" "$(socket_count)" 1

start --window B "$b_log"
window_b=$started
wait_for_sockets 2
check "another name is another window" "$(alive "$window_b")" yes
check "two windows listen" "$(socket_count)" 2

run --window "   " "$a_log"
check "a blank name is an error" $? 1

kill -STOP "$window_a"
run --window A "$b_log"
check "a window that does not answer is an error" $? 1
kill -CONT "$window_a"
check "that window keeps running" "$(alive "$window_a")" yes

kill -9 "$window_a"
wait "$window_a" 2>/dev/null
start --window A "$a_log"
window_a=$started
wait_for_listeners 2
run --window A
check "the name of a crashed window can be used again" "$(alive "$window_a")" yes

start --window C "$a_log"
first=$started
start --window C "$b_log"
second=$started
start --window C "$b_log"
third=$started
for _ in $(seq 1 100); do
    survivors=0
    for pid in "$first" "$second" "$third"; do
        [ "$(alive "$pid")" = yes ] && survivors=$((survivors + 1))
    done
    [ "$survivors" -eq 1 ] && break
    perl -e 'select(undef, undef, undef, 0.1)'
done
check "simultaneous starts end up in one window" "$survivors" 1

exit "$failures"
