#!/bin/bash
set -uo pipefail

##############################################################################
# Racecar End-to-End Test
#
# Tests the complete racecar workflow:
#   build -> up -> init -> diag -> test -> evaluate -> classify -> status ->
#   stats -> down
#
# Prerequisites:
#   - DAYTONA_API_KEY must be set
#   - Go toolchain available
#   - Internet access (Daytona cloud sandboxes)
##############################################################################

echo "============================================"
echo "  Racecar End-to-End Test"
echo "============================================"
echo ""
echo "Started: $(date)"
echo ""

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------

if [ -z "${DAYTONA_API_KEY:-}" ]; then
    echo "FAIL: DAYTONA_API_KEY is not set. Export it before running this test."
    exit 1
fi
echo "PASS: DAYTONA_API_KEY is set"

if ! command -v go &>/dev/null; then
    echo "FAIL: Go toolchain not found in PATH"
    exit 1
fi
echo "PASS: Go toolchain available ($(go version | cut -d' ' -f3))"

# ---------------------------------------------------------------------------
# Counters and helpers
# ---------------------------------------------------------------------------

START_TIME=$(date +%s)
PASSED=0
FAILED=0
SKIPPED=0
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RACECAR="$SCRIPT_DIR/racecar"

pass() {
    echo "PASS: $1"
    PASSED=$((PASSED + 1))
}

fail() {
    echo "FAIL: $1"
    FAILED=$((FAILED + 1))
}

skip() {
    echo "SKIP: $1"
    SKIPPED=$((SKIPPED + 1))
}

section() {
    echo ""
    echo "--- $1 ---"
}

# ---------------------------------------------------------------------------
# Step 1: Build
# ---------------------------------------------------------------------------

section "Step 1: Build"

cd "$SCRIPT_DIR"
if make clean &>/dev/null && make 2>&1; then
    if [ -x "$RACECAR" ]; then
        pass "Build succeeded (binary: $RACECAR)"
    else
        fail "Build produced no executable"
        exit 1
    fi
else
    fail "Build failed"
    exit 1
fi

# Verify binary runs
VERSION_OUT=$("$RACECAR" version 2>&1)
if [ $? -eq 0 ] && echo "$VERSION_OUT" | grep -qi "racecar"; then
    pass "Binary runs ($VERSION_OUT)"
else
    fail "Binary does not run: $VERSION_OUT"
    exit 1
fi

# ---------------------------------------------------------------------------
# Step 2: Clean slate — tear down any existing sandboxes
# ---------------------------------------------------------------------------

section "Step 2: Clean slate (tear down existing sandboxes if any)"

if "$RACECAR" down 2>&1 | grep -qi "no active\|destroyed"; then
    echo "  (Existing sandboxes torn down or none found)"
else
    echo "  (Down command returned unexpected output; proceeding anyway)"
fi

# ---------------------------------------------------------------------------
# Step 3: racecar up
# ---------------------------------------------------------------------------

section "Step 3: racecar up"

UP_OUT=$("$RACECAR" up 2>&1)
UP_RC=$?

if [ $UP_RC -eq 0 ]; then
    if echo "$UP_OUT" | grep -qi "sandbox\|ready"; then
        pass "racecar up (sandboxes created)"
        echo "$UP_OUT" | sed 's/^/  /'
    else
        pass "racecar up (exit 0)"
        echo "$UP_OUT" | sed 's/^/  /'
    fi
else
    fail "racecar up (exit code $UP_RC)"
    echo "$UP_OUT" | sed 's/^/  /'
    echo ""
    echo "CRITICAL: Cannot proceed without sandboxes."
    exit 1
fi

# ---------------------------------------------------------------------------
# Step 4: racecar init
# ---------------------------------------------------------------------------

section "Step 4: racecar init"

INIT_OUT=$("$RACECAR" init 2>&1)
INIT_RC=$?

if [ $INIT_RC -eq 0 ]; then
    if echo "$INIT_OUT" | grep -qi "loaded\|ready\|training"; then
        pass "racecar init (training data loaded)"
        echo "$INIT_OUT" | sed 's/^/  /'
    else
        pass "racecar init (exit 0)"
        echo "$INIT_OUT" | sed 's/^/  /'
    fi
else
    fail "racecar init (exit code $INIT_RC)"
    echo "$INIT_OUT" | sed 's/^/  /'
    echo ""
    echo "CRITICAL: Cannot proceed without training data."
    exit 1
fi

# ---------------------------------------------------------------------------
# Step 5: racecar diag — verify all sandboxes show valid table-info
# ---------------------------------------------------------------------------

section "Step 5: racecar diag"

DIAG_OUT=$("$RACECAR" diag 2>&1)
DIAG_RC=$?

if [ $DIAG_RC -eq 0 ]; then
    # Check that each category sandbox has valid table-info (Records: N where N > 0)
    DIAG_PROBLEMS=0

    for CAT in SAFE SPAM ATTACK; do
        if echo "$DIAG_OUT" | grep -A 20 "\[$CAT\]" | grep -q "BINARY MISSING"; then
            fail "diag [$CAT]: binary missing"
            DIAG_PROBLEMS=$((DIAG_PROBLEMS + 1))
        elif echo "$DIAG_OUT" | grep -A 20 "\[$CAT\]" | grep -q "table-info:.*exit=0"; then
            pass "diag [$CAT]: table-info valid"
        else
            # Not necessarily fatal — could just be unexpected output format
            echo "  WARN: Could not confirm table-info for [$CAT]"
            echo "$DIAG_OUT" | grep -A 10 "\[$CAT\]" | sed 's/^/    /'
            DIAG_PROBLEMS=$((DIAG_PROBLEMS + 1))
        fi
    done

    if [ "$DIAG_PROBLEMS" -gt 0 ]; then
        fail "diag: $DIAG_PROBLEMS sandbox(es) have issues"
        echo ""
        echo "Full diag output:"
        echo "$DIAG_OUT" | sed 's/^/  /'
        echo ""
        echo "CRITICAL: Sandbox diagnostics failed."
        exit 1
    fi
else
    fail "racecar diag (exit code $DIAG_RC)"
    echo "$DIAG_OUT" | sed 's/^/  /'
    exit 1
fi

# ---------------------------------------------------------------------------
# Step 6: racecar test — verify accuracy > 50%
# ---------------------------------------------------------------------------

section "Step 6: racecar test"

TEST_OUT=$("$RACECAR" test 2>&1)
TEST_RC=$?

if [ $TEST_RC -eq 0 ]; then
    # Extract accuracy line: "Accuracy: X/Y (Z.Z%)"
    ACCURACY_LINE=$(echo "$TEST_OUT" | grep -i "Accuracy:")
    if [ -n "$ACCURACY_LINE" ]; then
        # Extract the percentage
        ACCURACY_PCT=$(echo "$ACCURACY_LINE" | grep -oE '[0-9]+\.[0-9]+%' | head -1 | tr -d '%')
        if [ -n "$ACCURACY_PCT" ]; then
            # Compare: accuracy > 50 using awk (handles floating point)
            ABOVE_50=$(echo "$ACCURACY_PCT" | awk '{print ($1 > 50.0) ? "yes" : "no"}')
            if [ "$ABOVE_50" = "yes" ]; then
                pass "racecar test (accuracy: ${ACCURACY_PCT}% > 50%)"
            else
                fail "racecar test (accuracy: ${ACCURACY_PCT}% <= 50%)"
            fi
        else
            fail "racecar test (could not parse accuracy percentage)"
        fi
        echo "  $ACCURACY_LINE"
    else
        fail "racecar test (no accuracy line in output)"
    fi

    # Show individual results (non-critical — just informational)
    echo ""
    echo "  Individual results:"
    echo "$TEST_OUT" | grep -E "\[(PASS|FAIL)\]" | sed 's/^/    /'
else
    fail "racecar test (exit code $TEST_RC)"
    echo "$TEST_OUT" | sed 's/^/  /'
fi

# ---------------------------------------------------------------------------
# Step 7: racecar evaluate — verify valid JSON output
# ---------------------------------------------------------------------------

section "Step 7: racecar evaluate"

EVAL_OUT=$("$RACECAR" evaluate 2>&1)
EVAL_RC=$?

if [ $EVAL_RC -eq 0 ]; then
    # Verify it's valid JSON
    if echo "$EVAL_OUT" | python3 -m json.tool &>/dev/null; then
        pass "racecar evaluate (valid JSON)"

        # Extract key fields from JSON
        EVAL_TOTAL=$(echo "$EVAL_OUT" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('total',0))" 2>/dev/null)
        EVAL_CORRECT=$(echo "$EVAL_OUT" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('correct',0))" 2>/dev/null)
        EVAL_ACCURACY=$(echo "$EVAL_OUT" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('accuracy',0))" 2>/dev/null)
        EVAL_MISSES=$(echo "$EVAL_OUT" | python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d.get('misses',[])))" 2>/dev/null)

        echo "  Total: ${EVAL_TOTAL:-?}, Correct: ${EVAL_CORRECT:-?}, Accuracy: ${EVAL_ACCURACY:-?}%, Misses: ${EVAL_MISSES:-?}"

        # Verify accuracy > 50% in evaluate too
        if [ -n "$EVAL_ACCURACY" ]; then
            EVAL_ABOVE_50=$(echo "$EVAL_ACCURACY" | awk '{print ($1 > 50.0) ? "yes" : "no"}')
            if [ "$EVAL_ABOVE_50" = "yes" ]; then
                pass "evaluate accuracy ${EVAL_ACCURACY}% > 50%"
            else
                fail "evaluate accuracy ${EVAL_ACCURACY}% <= 50%"
            fi
        fi

        # Show misses if any
        if [ "${EVAL_MISSES:-0}" -gt 0 ] 2>/dev/null; then
            echo ""
            echo "  Misses:"
            echo "$EVAL_OUT" | python3 -c "
import sys, json
d = json.load(sys.stdin)
for m in d.get('misses', []):
    print(f\"    expected={m['expected']} got={m['got']} subject={m['subject']}\")
" 2>/dev/null
        fi
    else
        fail "racecar evaluate (output is not valid JSON)"
        echo "$EVAL_OUT" | head -20 | sed 's/^/  /'
    fi
else
    fail "racecar evaluate (exit code $EVAL_RC)"
    echo "$EVAL_OUT" | sed 's/^/  /'
fi

# ---------------------------------------------------------------------------
# Step 8: racecar classify — classify a test email
# ---------------------------------------------------------------------------

section "Step 8: racecar classify (test email)"

CLASSIFY_OUT=$("$RACECAR" classify \
    "Urgent: Verify your PayPal account" \
    "Dear customer, your PayPal acc0unt has been susp3nded due to unusual activity. Click here immediately to restore access or your account will be permanently locked within 24 hours." \
    2>&1)
CLASSIFY_RC=$?

if [ $CLASSIFY_RC -eq 0 ]; then
    if echo "$CLASSIFY_OUT" | grep -qi "classification\|category\|attack\|safe\|spam"; then
        # Extract the classification result
        CLASSIFY_RESULT=$(echo "$CLASSIFY_OUT" | grep -i "^Classification:" | head -1 | awk '{print $2}')
        if [ -z "$CLASSIFY_RESULT" ]; then
            CLASSIFY_RESULT=$(echo "$CLASSIFY_OUT" | grep -i "Final Verdict:" | head -1)
        fi
        pass "racecar classify (result: ${CLASSIFY_RESULT:-see output})"
        echo "$CLASSIFY_OUT" | sed 's/^/  /'
    else
        pass "racecar classify (exit 0, unexpected format)"
        echo "$CLASSIFY_OUT" | sed 's/^/  /'
    fi
else
    fail "racecar classify (exit code $CLASSIFY_RC)"
    echo "$CLASSIFY_OUT" | sed 's/^/  /'
fi

# Also test classifying a safe email
echo ""
echo "  (Classifying a safe email...)"
CLASSIFY_SAFE_OUT=$("$RACECAR" classify \
    "Team standup notes" \
    "Hi all, here are the notes from today's standup. Alice is working on the API refactor, Bob is fixing the login bug." \
    2>&1)
CLASSIFY_SAFE_RC=$?

if [ $CLASSIFY_SAFE_RC -eq 0 ]; then
    SAFE_RESULT=$(echo "$CLASSIFY_SAFE_OUT" | grep -i "^Classification:" | head -1 | awk '{print $2}')
    pass "racecar classify safe email (result: ${SAFE_RESULT:-see output})"
else
    fail "racecar classify safe email (exit code $CLASSIFY_SAFE_RC)"
fi

# ---------------------------------------------------------------------------
# Step 9: racecar status
# ---------------------------------------------------------------------------

section "Step 9: racecar status"

STATUS_OUT=$("$RACECAR" status 2>&1)
STATUS_RC=$?

if [ $STATUS_RC -eq 0 ]; then
    if echo "$STATUS_OUT" | grep -qi "sandbox\|safe\|spam\|attack"; then
        pass "racecar status"
        echo "$STATUS_OUT" | sed 's/^/  /'
    else
        pass "racecar status (exit 0)"
        echo "$STATUS_OUT" | sed 's/^/  /'
    fi
else
    fail "racecar status (exit code $STATUS_RC)"
    echo "$STATUS_OUT" | sed 's/^/  /'
fi

# ---------------------------------------------------------------------------
# Step 10: racecar stats
# ---------------------------------------------------------------------------

section "Step 10: racecar stats"

STATS_OUT=$("$RACECAR" stats 2>&1)
STATS_RC=$?

if [ $STATS_RC -eq 0 ]; then
    if echo "$STATS_OUT" | grep -qi "records\|safe\|spam\|attack\|statistics"; then
        pass "racecar stats"
        echo "$STATS_OUT" | sed 's/^/  /'
    else
        pass "racecar stats (exit 0)"
        echo "$STATS_OUT" | sed 's/^/  /'
    fi
else
    fail "racecar stats (exit code $STATS_RC)"
    echo "$STATS_OUT" | sed 's/^/  /'
fi

# ---------------------------------------------------------------------------
# Step 11: racecar down
# ---------------------------------------------------------------------------

section "Step 11: racecar down"

DOWN_OUT=$("$RACECAR" down 2>&1)
DOWN_RC=$?

if [ $DOWN_RC -eq 0 ]; then
    pass "racecar down (sandboxes destroyed)"
    echo "$DOWN_OUT" | sed 's/^/  /'
else
    fail "racecar down (exit code $DOWN_RC)"
    echo "$DOWN_OUT" | sed 's/^/  /'
fi

# Verify sandboxes are actually gone
VERIFY_OUT=$("$RACECAR" status 2>&1)
VERIFY_RC=$?
if [ $VERIFY_RC -ne 0 ]; then
    pass "Verified: no active sandboxes after down"
else
    fail "Sandboxes still appear active after down"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))
TOTAL=$((PASSED + FAILED))

echo ""
echo "============================================"
echo "  Test Summary"
echo "============================================"
echo ""
echo "  Passed:  $PASSED"
echo "  Failed:  $FAILED"
echo "  Skipped: $SKIPPED"
echo "  Total:   $TOTAL"
echo ""
printf "  Duration: %dm %ds\n" $((ELAPSED / 60)) $((ELAPSED % 60))
echo ""
echo "  Finished: $(date)"
echo ""

if [ "$FAILED" -eq 0 ]; then
    echo "  Result: ALL TESTS PASSED"
    echo ""
    exit 0
else
    echo "  Result: $FAILED TEST(S) FAILED"
    echo ""
    exit 1
fi
