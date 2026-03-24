# Racecar

A hyper-fast vector database written in pure C, orchestrated by a Go CLI that runs everything inside [Daytona](https://daytona.io) cloud sandboxes. Classifies emails as safe, spam, or attack using a two-stage pipeline: vector similarity search followed by context-aware structural analysis.

## Requirements

- Go 1.22+
- A Daytona API key (free at [app.daytona.io](https://app.daytona.io))
- No C compiler needed locally (compiles inside sandboxes)

## Setup (Start to Finish)

### 1. Clone and enter the project

```bash
git clone <repo-url>
cd racecar
```

### 2. Set your Daytona API key

Get a free API key from [app.daytona.io](https://app.daytona.io) (Dashboard > API Keys).

```bash
export DAYTONA_API_KEY=your_key_here
```

To persist across sessions, add it to your shell profile:

```bash
echo 'export DAYTONA_API_KEY=your_key_here' >> ~/.zshrc
source ~/.zshrc
```

### 3. Build and install

```bash
make
```

This compiles the Go binary and installs it to `~/.local/bin/racecar`. Make sure `~/.local/bin` is in your `PATH`. If not:

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

Verify:

```bash
racecar version
```

### 4. Spin up sandboxes

```bash
racecar up
```

This takes 30-60 seconds. It creates a persistent Daytona volume, spins up 3 sandboxes in parallel, installs gcc/make in each, uploads the C source code, and compiles the racecar binary inside each sandbox.

### 5. Load training data

```bash
racecar init
```

This loads 196 sample training emails (61 safe, 57 spam, 78 attack) into the 3 sandboxes and builds HNSW indexes for fast search.

> **Note:** Training data is stored on each sandbox's local filesystem, which is ephemeral. If sandboxes are recreated (e.g., after `racecar down && racecar up`), you must run `racecar init` again to reload the data.

### 6. Classify emails

Classify by subject and body:

```bash
racecar classify "Meeting tomorrow at 9am" "Hi team, please prepare your sprint updates for the standup."
```

```bash
racecar classify "Your account is locked" "Click here immediately to verify your identity or your account will be suspended."
```

```bash
racecar classify "YOU WON A FREE iPHONE" "Congratulations! Click now to claim your FREE prize!"
```

Classify a raw email file (with headers):

```bash
racecar classify-raw suspicious_email.eml
```

Pipe a raw email from stdin:

```bash
cat suspicious_email.eml | racecar classify-raw -
```

### 7. Run accuracy tests

```bash
racecar test
```

```bash
racecar test-raw
```

Machine-readable output (JSON):

```bash
racecar evaluate
```

### 8. Train with your own data

Add individual emails:

```bash
racecar train safe "Lunch plans" "Want to grab tacos at noon?"
racecar train attack "Verify now" "Your bank account will be closed unless you confirm your SSN."
racecar train spam "BIG SALE" "Everything 90% off today only! Click to shop now!"
```

Batch insert from a JSONL file:

```bash
racecar populate training_data.jsonl
```

JSONL format (one JSON object per line):

```json
{"category": "attack", "subject": "Urgent: Verify Your Account", "body": "Your acc0unt has been susp3nded. Click here immediately."}
{"category": "safe", "subject": "Password changed", "body": "Your password was updated on March 23. If this was you, no action needed."}
{"category": "spam", "subject": "Limited time offer", "body": "Buy now and save 50% on all products!"}
```

Or pipe JSONL from stdin:

```bash
cat emails.jsonl | racecar populate -
```

After adding training data, rebuild indexes:

```bash
racecar build-index
```

### 9. Check status and stats

```bash
racecar status
racecar stats
```

### 10. Tear down (when done)

```bash
racecar down
```

This destroys the sandboxes. Since data is stored on each sandbox's local filesystem (ephemeral), you will need to run `racecar init` again after `racecar up` to reload training data.

## All Commands

```
Sandbox Management:
  up                                       Spin up 3 sandboxes
  down                                     Tear down sandboxes (data is ephemeral)
  status                                   Show sandbox states and record counts
  diag                                     Run diagnostics on all sandboxes

Email Classification:
  init                                     Load sample training data + build indexes
  classify <subject> <body>                Classify email (two-stage analysis)
  classify-raw <file>                      Classify raw email with headers
  classify-raw -                           Read raw email from stdin
  train <safe|spam|attack> <subject> <body> Add training email
  populate <file.jsonl>                    Batch insert from JSONL file
  populate -                               Batch insert from stdin
  build-index                              Build HNSW indexes in all sandboxes
  test                                     Run accuracy test
  test-raw                                 Run raw email accuracy test (with headers)
  evaluate                                 Run test and output JSON (machine-readable)
  stats                                    Show per-category email counts

Utility:
  version                                  Show version
  help                                     Show help
```

## Two-Stage Classification Pipeline

### Stage 1: Enhanced Vector Classification

The email is converted into a 256-dimensional vector using enhanced feature hashing, then searched against 3 parallel HNSW indexes (one per category). The category with the lowest average cosine distance wins.

- **196 training emails** including adversarial examples with leet speak and misspellings
- **Character trigrams** catch misspellings ("acc0unt" and "account" share trigrams)
- **Leet speak normalization** (0→o, 1→l, 3→e, @→a, $→s)
- **Keyword boosting** amplifies attack indicators (urgent, password, wire) up to 2x
- **Parallel HNSW search** across 3 Daytona sandboxes via goroutines

### Stage 2: Context-Aware Analysis

Runs automatically when Stage 1 flags an email as ATTACK. For raw emails with headers, it also analyzes authentication results and sender metadata.

- **Domain mismatch detection** (sender vs reply-to vs return-path)
- **SPF/DKIM/DMARC failure detection**
- **Typosquat detection** (Levenshtein distance against 16 known brands)
- **Urgency language density scoring**
- **Credential/financial request pattern matching**
- **Malware indicator detection** (attachment/macro language)
- **Attack sub-classification**: PHISHING, CEO_FRAUD, MALWARE, CREDENTIAL_HARVEST

## Agent-Assisted Data Generation

Use Claude Code as an agent to generate synthetic training data:

1. Ask Claude Code to generate emails: "Generate 30 sophisticated phishing emails with varied techniques"
2. Claude Code writes them to a JSONL file
3. Run: `racecar populate synthetic.jsonl`
4. Run: `racecar build-index`
5. Run: `racecar evaluate`

See CLAUDE.md for detailed agent workflow instructions.

## Agent Workflow (Self-Improving Loop)

Use Claude Code (or any LLM agent) to generate synthetic training data and improve classification accuracy:

```bash
# Step 1: Evaluate current accuracy
racecar evaluate

# Step 2: Generate synthetic emails (via Claude Code or manually)
# Write JSONL with {"category": "...", "subject": "...", "body": "..."}

# Step 3: Populate the database
racecar populate synthetic_emails.jsonl

# Step 4: Rebuild indexes
racecar build-index

# Step 5: Re-evaluate
racecar evaluate
```

Repeat steps 2-5 until accuracy reaches target. See CLAUDE.md for detailed agent instructions.

## Architecture

```
                              Daytona Cloud
                    ┌───────────────────────────────┐
  racecar CLI       │                               │
  (Go binary)       │  Sandbox 1: SAFE emails       │
       │            │    racecar C binary            │
       │  parallel  │    HNSW index + vector table   │
       ├──────────► │    (local filesystem)          │
       │            │                               │
       │            │  Sandbox 2: SPAM emails        │
       ├──────────► │    racecar C binary            │
       │            │    HNSW index + vector table   │
       │            │    (local filesystem)          │
       │            │                               │
       ├──────────► │  Sandbox 3: ATTACK emails      │
       │            │    racecar C binary            │
                    │    HNSW index + vector table   │
                    │    (local filesystem)          │
                    │                               │
                    │  Volume: racecar-data          │
                    │    (future persistence)        │
                    └───────────────────────────────┘
```

Data (vectors, HNSW indexes) is stored on each sandbox's **local filesystem** for performance. Local filesystems are ephemeral — data must be reloaded with `racecar init` after sandboxes are recreated. The volume exists for potential future persistence but is not currently used for data storage.

## Configuration

| Env Variable | Required | Description |
|---|---|---|
| `DAYTONA_API_KEY` | Yes | Daytona API key (get free at app.daytona.io) |
| `DAYTONA_API_URL` | No | API endpoint (default: https://app.daytona.io/api) |
| `DAYTONA_TARGET` | No | Region (default: org default) |

## Project Structure

```
main.go                Go CLI (orchestrator)
engine.go              Daytona sandbox management + two-stage pipeline
vectorize.go           Enhanced feature hashing (char n-grams, leet normalization)
email_parser.go        Raw email parser (headers, MIME, auth results)
header_analysis.go     Context analysis, risk scoring, attack sub-classification
training.go            142 base training emails
training_advanced.go   54 additional training emails (misspellings, adversarial)
sample_raw_emails.go   12 raw test emails with headers
CLAUDE.md              Agent workflow instructions for Claude Code

src/                   Racecar C vector database (runs inside sandboxes)
  racecar.h              Master header
  main.c                 CLI entry point
  db.c                   Database management
  table.c                Storage engine
  vector.c               SIMD-optimized distance functions
  hnsw.c                 HNSW index
  json.c                 JSON parser
  tokenizer.c            Text vectorizer
  util.c                 Utilities

sentinel/              Standalone local classifier (no Daytona needed)
Makefile               Build system
```

## Standalone Local Mode

The C tools work standalone without Daytona (requires gcc/clang):

```bash
make local
./racecar-local version
sentinel init
sentinel test
```

## Uninstall

```bash
racecar down
make uninstall
make clean
rm -rf ~/.racecar
```

## Troubleshooting

### `racecar test` shows wrong results after `racecar up`
Run `racecar init` to reload training data. Sandbox local filesystems are ephemeral — data must be reloaded after `racecar up`.

### Commands return "exit -1"
This means the Daytona ExecuteCommand timed out. Run `racecar diag` to check sandbox health. If the binary is missing, run `racecar down && racecar up`.

### Sandboxes won't start
Check your API key: `echo $DAYTONA_API_KEY`. Verify at https://app.daytona.io.
