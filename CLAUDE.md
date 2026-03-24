# Racecar — Agent Instructions

This project is a vector database with email anomaly detection running inside Daytona cloud sandboxes.

## Project Layout

- `src/` — C vector database engine (compiles inside Daytona sandboxes)
- `sentinel/` — Standalone local C classifier (no Daytona needed)
- `*.go` — Go CLI that orchestrates 3 Daytona sandboxes for parallel classification
- `Makefile` — `make` builds Go binary, `make local` builds standalone C tools

## Key Commands

> **Note:** Sandbox data is ephemeral. After `racecar up`, you must run `racecar init` to load data. Data is lost when sandboxes are torn down (`racecar down`).

```bash
racecar up           # spin up 3 sandboxes
racecar init         # load built-in training data
racecar classify <subject> <body>   # classify an email
racecar classify-raw <file>         # classify raw email with headers
racecar train <safe|spam|attack> <subject> <body>  # add one training email
racecar populate <file.jsonl>       # batch insert from JSONL
racecar populate -                  # batch insert from stdin
racecar build-index                 # rebuild HNSW indexes
racecar test                        # human-readable accuracy test
racecar evaluate                    # machine-readable JSON test output
racecar test-raw                    # test raw emails with headers
racecar stats                       # show record counts
racecar diag                        # check sandbox health (binary, table, search)
racecar down                        # tear down sandboxes
```

## Agent Workflow

### Full Start-to-Finish Workflow

1. Build: `make`
2. Start sandboxes: `racecar up`
3. Load training data: `racecar init`
4. Verify health: `racecar diag`
5. Test accuracy: `racecar test`
6. Generate synthetic data (optional)
7. Populate: `racecar populate synthetic.jsonl`
8. Re-test: `racecar evaluate`
9. Tear down when done: `racecar down`

### Generating Synthetic Training Data

When asked to populate the database with synthetic emails, follow this workflow:

### Step 1: Check sandbox status

```bash
racecar status
```

If no sandboxes are running, run `racecar up` then `racecar init`.

### Step 2: Evaluate current accuracy

```bash
racecar evaluate
```

Parse the JSON output. Look at the `misses` array to identify which categories need improvement.

### Step 3: Generate synthetic emails

Write a JSONL file where each line is:

```json
{"category": "attack", "subject": "...", "body": "..."}
```

Guidelines for generating good training data:

**Attack emails should include:**
- Phishing with typosquat domains (paypa1, micr0soft, amaz0n)
- CEO fraud / business email compromise (urgent wire transfers, payroll changes)
- Credential harvesting (fake password resets, account verification)
- Malware delivery (fake invoices with attachment references, enable macros)
- Leet speak and misspellings (acc0unt, verif1cation, susp3nded)
- Social engineering (impersonating IT, HR, executives)
- Urgency language (expire, suspend, locked, immediately, within 24 hours)

**Safe emails should include adversarial examples that look like attacks but aren't:**
- Legitimate password reset confirmations
- Real wire transfer confirmations from accounting
- IT security notifications about completed scans
- Legitimate shared document notifications
- Real invoice emails between known vendors

**Spam emails should include aggressive marketing that overlaps with attack language:**
- "Your account needs attention" (subscription renewal)
- "Act now" (limited time sale)
- "Verify your email" (mailing list confirmation)

### Step 4: Populate the database

```bash
racecar populate /tmp/synthetic_emails.jsonl
```

Or pipe directly:

```bash
cat <<'JSONL' | racecar populate -
{"category": "attack", "subject": "Urgent: Verify Your Account", "body": "Your acc0unt has been susp3nded. Click here immediately to verify."}
{"category": "safe", "subject": "Password changed successfully", "body": "Your password was updated on March 23. If this was you, no action needed."}
JSONL
```

### Step 5: Rebuild indexes

```bash
racecar build-index
```

### Step 6: Re-evaluate

```bash
racecar evaluate
```

Compare accuracy before and after. If misses remain, generate targeted training data for the missed categories and repeat from Step 3.

## Self-Improving Loop

For a full self-improving cycle, repeat steps 2-6 until accuracy is satisfactory. Each iteration should:

1. Run `racecar evaluate` and parse the JSON
2. Identify which test cases are failing (the `misses` array)
3. For each miss, generate 3-5 training emails that address the gap
4. Write to JSONL and run `racecar populate`
5. Run `racecar build-index`
6. Run `racecar evaluate` again

Stop when accuracy reaches the target or no further improvement is possible.

### Raw Email Format

Raw emails for `racecar classify-raw` use standard email format:

```
From: "Sender Name" <sender@example.com>
To: recipient@example.com
Subject: Email Subject Here
Date: Mon, 23 Mar 2026 10:00:00 -0500
Authentication-Results: mx.example.com;
    spf=pass smtp.mailfrom=example.com;
    dkim=pass header.d=example.com

Email body text here. This is where the actual message goes.
Multiple paragraphs are fine.
```

Headers and body are separated by a blank line. Key headers for Stage 2 analysis:
- From / Reply-To / Return-Path (domain mismatch detection)
- Authentication-Results (SPF/DKIM/DMARC analysis)
- X-Mailer (suspicious mailer detection)

## JSONL Format Reference

```json
{"category": "attack", "subject": "Subject line here", "body": "Email body text here"}
{"category": "spam", "subject": "Subject line here", "body": "Email body text here"}
{"category": "safe", "subject": "Subject line here", "body": "Email body text here"}
```

- `category` must be one of: `safe`, `spam`, `attack`
- `subject` and `body` are plain text strings
- One JSON object per line, no trailing commas
- Lines starting with `#` are ignored
- Empty lines are ignored

## Common Issues

- "No active sandboxes": Run `racecar up` first
- "exit -1" errors: ExecuteCommand timeout — run `racecar diag` to check
- Wrong classifications after `racecar up`: Run `racecar init` to reload data
- Stale state: `rm ~/.racecar/state.json` then `racecar up`

## Architecture Notes

- 3 Daytona sandboxes (one per category) each run a compiled C racecar binary
- Vectorization (feature hashing, 256 dims) happens locally in Go
- Search happens inside sandboxes via `ExecuteCommand`
- State persisted at `~/.racecar/state.json`
- Volume data persists across sandbox cycles
- The C vector database uses HNSW indexes for sub-millisecond search
