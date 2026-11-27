#!/usr/bin/env bash
# End-to-end smoke test: starts the built server, sends real HTTP requests
# with curl, checks status codes and bodies, then stops it with SIGINT and
# expects a clean exit.
#
# Usage: scripts/smoke_test.sh [path/to/http_server] [port]
# Run it from the repository root so the server finds public/ and errors/.

set -euo pipefail

SERVER=${1:-./build/http_server}
PORT=${2:-18080}
BASE="http://127.0.0.1:${PORT}"
LOG=$(mktemp)
FAILURES=0

if [[ ! -x "$SERVER" ]]; then
    echo "error: server binary '$SERVER' not found or not executable" >&2
    exit 2
fi

"$SERVER" --port "$PORT" --host 127.0.0.1 --config /dev/null --log-level warn >"$LOG" 2>&1 &
SERVER_PID=$!

cleanup() {
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -KILL "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$LOG"
}
trap cleanup EXIT

# Wait up to ~5 seconds for the server to accept connections.
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "$BASE/health"; then
        break
    fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "error: server exited during startup:" >&2
        cat "$LOG" >&2
        exit 1
    fi
    sleep 0.1
done

# expect NAME EXPECTED_STATUS EXPECTED_BODY_SUBSTRING curl-args...
expect() {
    local name=$1 want_status=$2 want_body=$3
    shift 3
    local body status
    body=$(curl -s -w $'\n%{http_code}' "$@") || true
    status=${body##*$'\n'}
    body=${body%$'\n'*}
    if [[ "$status" != "$want_status" ]]; then
        echo "[FAIL] $name: expected status $want_status, got $status"
        FAILURES=$((FAILURES + 1))
    elif [[ -n "$want_body" && "$body" != *"$want_body"* ]]; then
        echo "[FAIL] $name: body does not contain '$want_body'"
        FAILURES=$((FAILURES + 1))
    else
        echo "[ OK ] $name"
    fi
}

expect "health check"            200 ""                "$BASE/health"
expect "static index page"       200 "<html"           "$BASE/"
expect "path parameter route"    200 "42"              "$BASE/users/42"
expect "query string decoding"   200 "hello world"     "$BASE/search?q=hello%20world"
expect "POST echo"               200 "ping"            -X POST --data "ping" "$BASE/echo"
expect "JSON status endpoint"    200 "\"version\""     "$BASE/api/status"
expect "JSON create todo"        201 "\"title\""       -X POST -H "Content-Type: application/json" \
                                                       --data '{"title":"from CI"}' "$BASE/api/todos"
expect "JSON invalid body"       400 "\"error\""       -X POST -H "Content-Type: application/json" \
                                                       --data '{oops' "$BASE/api/todos"
expect "unknown path is 404"     404 ""                "$BASE/does-not-exist"
expect "wrong method is 405"     405 ""                -X DELETE "$BASE/hello"
expect "path traversal blocked"  403 ""                --path-as-is "$BASE/../../etc/passwd"

# Graceful shutdown: SIGINT must stop the server with exit status 0.
kill -INT "$SERVER_PID"
EXIT_STATUS=0
for _ in $(seq 1 50); do
    kill -0 "$SERVER_PID" 2>/dev/null || break
    sleep 0.1
done
if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[FAIL] graceful shutdown: server still running 5 s after SIGINT"
    FAILURES=$((FAILURES + 1))
else
    wait "$SERVER_PID" || EXIT_STATUS=$?
    if [[ $EXIT_STATUS -eq 0 ]]; then
        echo "[ OK ] graceful shutdown"
    else
        echo "[FAIL] graceful shutdown: exit status $EXIT_STATUS"
        FAILURES=$((FAILURES + 1))
    fi
fi

if [[ $FAILURES -ne 0 ]]; then
    echo "$FAILURES smoke test(s) failed. Server log:"
    cat "$LOG"
    exit 1
fi
echo "All smoke tests passed."
