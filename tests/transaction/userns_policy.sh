#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Checks the skip and require policy of the user-namespace test binary (REQ-C-014).
#   userns_policy.sh skip    <binary>   with namespaces "unavailable" the binary exits 77 and says why
#   userns_policy.sh require <binary>   with HOLONIGHT_REQUIRE_USERNS=1 it fails instead (neither 0 nor 77)
mode=$1
binary=$2
output=$(HOLONIGHT_TEST_NO_USERNS=1 ${REQUIRE:+HOLONIGHT_REQUIRE_USERNS=1} "$binary" 2>&1)
status=$?
case "$mode" in
  skip)
    [ "$status" -eq 77 ] || { echo "expected exit 77, got $status: $output"; exit 1; }
    echo "$output" | grep -q "HOLONIGHT_SKIP userns" || { echo "missing HOLONIGHT_SKIP line: $output"; exit 1; }
    ;;
  require)
    REQUIRE=1
    output=$(HOLONIGHT_TEST_NO_USERNS=1 HOLONIGHT_REQUIRE_USERNS=1 "$binary" 2>&1)
    status=$?
    { [ "$status" -ne 0 ] && [ "$status" -ne 77 ]; } || { echo "expected a failure, got $status: $output"; exit 1; }
    echo "$output" | grep -q "HOLONIGHT_FAIL userns" || { echo "missing HOLONIGHT_FAIL line: $output"; exit 1; }
    ;;
  *) echo "unknown mode $mode"; exit 2 ;;
esac
