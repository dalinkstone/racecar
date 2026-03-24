# How Racecar Works

This document explains the unified Racecar architecture: what each layer does, how they connect, and why it's built this way.

## The Big Picture

Racecar is one project with three layers:

1. **C engine** (`src/`) -- A vector database that stores, indexes, and searches high-dimensional vectors. This is the compute layer. It runs inside Daytona cloud sandboxes, not on your machine.

2. **Go orchestrator** (`main.go`, `engine.go`, `vectorize.go`, `email_parser.go`, `header_analysis.go`, `training.go`, `training_advanced.go`, `sample_raw_emails.go`) -- A CLI that manages sandboxes, vectorizes text, parses raw emails, dispatches searches, runs context analysis, and aggregates results. This runs locally on your machine.

3. **Daytona** -- Cloud infrastructure that provides sandboxes (isolated environments with their own kernel, filesystem, and memory) and persistent volumes. You get a free API key at [app.daytona.io](https://app.daytona.io).

The Go binary is the only thing you run. It talks to Daytona's API, which manages the sandboxes where the C code actually executes. Email classification uses a two-stage pipeline: Stage 1 does vector similarity search across the sandboxes, and Stage 2 performs local context-aware analysis on emails flagged as attacks.

## What Changed

Racecar started as 3 separate tools:

- **Racecar** -- a standalone C vector database
- **Sentinel** -- a C email classifier that forked 3 processes locally to search 3 vector tables in parallel
- **Shelf** -- a Go knowledge search tool that ran a vector database inside a Daytona sandbox

Now they are unified. The C vector database code is the same, but instead of running locally it runs inside Daytona sandboxes. The Go binary from Shelf became the orchestrator for everything. Sentinel's multi-process design became multi-sandbox: instead of `fork()` on one machine, each category runs in a fully isolated cloud sandbox.

The `sentinel/` directory still exists as a standalone local alternative, but the primary workflow is the Go CLI with Daytona.

### The Two-Stage Enhancement

The original classifier used a single-stage approach: vectorize the email, search all 3 category indexes, pick the closest match. This worked for clear-cut cases but had two weaknesses:

1. **Obfuscation evasion** -- Attackers substitute characters ("acc0unt", "paypa1") to dodge keyword-based vectorization. A simple unigram+bigram hash treats "account" and "acc0unt" as completely different tokens.

2. **No structural analysis** -- Vector similarity only looks at text content. It cannot detect domain spoofing, SPF/DKIM failures, or the structural patterns that distinguish phishing from spam.

The two-stage pipeline addresses both. Stage 1 was enhanced with character trigrams and leet normalization so obfuscated words still produce similar vectors. Stage 2 was added to analyze email headers and content structure, providing attack sub-classification and risk scoring that pure vector search cannot.

## The Two-Stage Pipeline

### Why Two Stages

Stage 1 (vector search) is fast and good at broad categorization. It can tell safe emails from attacks because they use fundamentally different language. But it has limits:

- It cannot inspect email headers for authentication failures
- It cannot compare sender domains against known brands
- It cannot distinguish phishing from malware from CEO fraud
- It treats the email as a bag of words, ignoring structure

Stage 2 adds what Stage 1 cannot: structural analysis, header inspection, and attack sub-classification. It only runs when Stage 1 flags an email as ATTACK (or when processing raw emails with headers), so safe and spam emails skip the overhead entirely.

### How They Work Together

```
Email input
  │
  ├─ classify <subject> <body>
  │    │
  │    ▼
  │  Stage 1: vectorize → parallel HNSW search across 3 sandboxes
  │    │
  │    ├─ SAFE or SPAM → done (return result)
  │    │
  │    └─ ATTACK → Stage 2: content analysis (no headers available)
  │         │
  │         └─ urgency scoring, credential/financial ask detection,
  │            suspicious URL detection, malware indicators
  │              │
  │              └─ attack subtype + risk score → done
  │
  └─ classify-raw <file>
       │
       ▼
     Parse raw email (headers + body)
       │
       ▼
     Stage 1: vectorize subject+body → parallel HNSW search
       │
       ▼
     Stage 2: full header + content analysis (always runs for raw emails)
       │
       ├─ domain mismatch detection
       ├─ SPF/DKIM/DMARC failure detection
       ├─ typosquat detection (Levenshtein)
       ├─ urgency scoring
       ├─ credential/financial ask detection
       ├─ suspicious URL detection
       ├─ malware indicator detection
       │
       └─ attack subtype + risk score → done
```

## Enhanced Vectorization

Both the C code and the Go orchestrator use feature hashing to convert text into 256-dimensional vectors. The Go implementation has been enhanced with three additional techniques beyond basic unigram/bigram hashing.

### Base Feature Hashing

```
input:  "the quick brown fox"

1. Tokenize:     ["the", "quick", "brown", "fox"]
2. Remove stop words: ["quick", "brown", "fox"]
3. Add bigrams:  ["quick_brown", "brown_fox"]
4. For each token:
     index = FNV1a_hash(token) % 256
     sign  = sign_hash(token)     // +1 or -1 to reduce collisions
     vector[index] += sign * weight
5. L2-normalize the vector
```

Unigrams get weight 1.0 (or a boosted weight for known attack/spam keywords), bigrams get 0.7.

### Character Trigrams

Every word of 3+ characters is broken into overlapping 3-character subsequences, each hashed into the vector with weight 0.3:

```
"account" → ["acc", "cco", "cou", "oun", "unt"]
"acc0unt" → ["acc", "cc0", "c0u", "0un", "unt"]
```

Notice that "account" and "acc0unt" share 3 of 5 trigrams: "acc", "oun" (after leet normalization), and "unt". This means their vectors overlap substantially even though their unigram hashes are completely different. The trigram approach catches misspellings, leet speak, and character substitutions without needing an explicit dictionary.

### Leet Speak Normalization

Before hashing, the text is also processed through a leet-to-alpha converter:

| Leet | Alpha |
|------|-------|
| 0 | o |
| 1 | l |
| 3 | e |
| 4 | a |
| 5 | s |
| @ | a |
| $ | s |
| ! | i |

The normalized version is hashed with weight 0.5 (half the weight of the original) and added to the same vector. So "p@ssw0rd" produces features for both "p@ssw0rd" (original) and "password" (normalized), and the normalized version triggers the keyword boost for "password" (weight 1.8).

### Keyword Boosting

Known attack and spam indicator words get amplified weights during hashing:

**Attack indicators** (weight up to 2.0x):
- Urgency: urgent, immediately, expire, suspend, locked, compromised
- Credentials: password, credential, verify, confirm, ssn
- Financial: wire, transfer, payment, invoice, bank
- Actions: click, download, attachment

**Spam indicators** (weight up to 2.0x):
- congratulations, winner, lottery, prize, free, discount, guaranteed, miracle, unsubscribe

This means an email containing "urgent wire transfer password" will produce a vector that is strongly pulled toward the attack region of the vector space, even if the rest of the email uses innocuous language.

### The Combined Effect

For a leet-speak phishing email like "Your acc0unt has been susp3nded":

1. Original tokens ["acc0unt", "susp3nded"] hash to their own dimensions
2. Normalized tokens ["account", "suspended"] hash to different dimensions and trigger keyword boosts ("account" gets 1.3x weight)
3. Character trigrams from both versions create overlapping features with the clean training data
4. The resulting vector is close to legitimate attack training emails in cosine space

This three-pronged approach means obfuscated text still lands near its clean equivalent in vector space.

## Raw Email Analysis

The `classify-raw` command accepts a full raw email (headers + body) and runs both stages with complete header analysis.

### Email Parsing

The parser in `email_parser.go` handles:

- **Header extraction** -- Splits on the first blank line, unfolds continuation lines (RFC 2822), and parses key-value pairs into a map
- **Core headers** -- From, To, Subject, Date, Reply-To, Return-Path, Content-Type, X-Mailer, Message-ID
- **Received headers** -- All Received: headers are collected (there are typically many per email)
- **Domain extraction** -- The domain part is extracted from From, Reply-To, and Return-Path addresses for comparison
- **Authentication-Results parsing** -- SPF, DKIM, and DMARC verdict strings are extracted from the Authentication-Results header

### SPF/DKIM/DMARC Extraction

The parser looks for the Authentication-Results header and extracts verdict tokens:

```
Authentication-Results: mx.example.com;
 spf=fail smtp.mailfrom=paypa1.com;
 dkim=fail header.d=paypa1.com;
 dmarc=fail
```

This produces: SPF="fail", DKIM="fail", DMARC="fail". Each failure adds to the risk score (SPF fail: +0.15, DKIM fail: +0.15, DMARC fail: +0.10).

### Typosquat Detection

The sender domain is compared against 16 known brand domains using Levenshtein distance:

```
paypal.com, google.com, microsoft.com, apple.com,
amazon.com, facebook.com, netflix.com, linkedin.com,
chase.com, wellsfargo.com, bankofamerica.com, citibank.com,
dropbox.com, docusign.com, adobe.com, zoom.us
```

If the similarity ratio (1 - edit_distance/max_length) is between 0.7 and 1.0 (close but not exact), the domain is flagged as a potential typosquat. For example:

- "paypa1.com" vs "paypal.com": edit distance 1, similarity 0.9 -- flagged
- "g00gle-docs.com" vs "google.com": similarity > 0.7 -- flagged
- "randomsite.com" vs "paypal.com": low similarity -- not flagged

A typosquat detection adds +0.25 to the risk score.

### Risk Score Computation

The risk score accumulates from all detected signals:

| Signal | Risk Added |
|--------|-----------|
| Reply-To domain mismatch | +0.20 |
| Return-Path domain mismatch | +0.10 |
| SPF fail/softfail | +0.15 |
| DKIM fail | +0.15 |
| DMARC fail | +0.10 |
| Typosquat detection | +0.25 |
| High urgency language | +urgency_score * 0.15 |
| Credential/personal info request | +0.15 |
| Financial/payment request | +0.15 |
| Suspicious link patterns | +0.10 |
| Malware indicator | +0.10 |

The score is capped at 1.0. Risk levels are:
- **LOW**: score <= 0.3
- **MEDIUM**: score 0.3 - 0.6
- **HIGH**: score > 0.6

## Attack Sub-Classification

After collecting all signals, Stage 2 classifies the attack into a specific subtype. The rules are evaluated in priority order:

### CEO_FRAUD

Triggered when: financial ask + urgency score > 0.2

Typical signals: wire transfer requests, routing/account numbers, "do not discuss with anyone", urgent tone. These emails impersonate executives and pressure employees into making financial transactions.

### CREDENTIAL_HARVEST

Triggered when: credential/personal information request detected

Typical signals: "verify your password", "update your login", "confirm your identity", requests for SSN, credit card numbers, or bank account details. These emails try to collect authentication credentials through fake login pages or reply forms.

### PHISHING

Triggered when: domain mismatch, SPF failure, or suspicious URLs detected (and not already classified as CEO_FRAUD or CREDENTIAL_HARVEST)

Typical signals: sender domain differs from Reply-To, typosquat domain detected, SPF/DKIM failures, links to non-HTTPS URLs. These are broad phishing attacks that rely on impersonating trusted senders.

### MALWARE

Triggered when: malware-related language detected in the body (and not already classified above)

Typical signals: "enable macros", "open the attached document", references to .exe/.zip/.scr/.bat files, "download" combined with "attachment". These emails deliver malicious payloads through attachments or download links.

### GENERIC_ATTACK

Fallback when none of the specific subtypes match but Stage 1 classified the email as ATTACK.

## The Sandbox Architecture

Why 3 sandboxes instead of 3 processes on one machine?

Each Daytona sandbox is a fully isolated environment: separate kernel, separate filesystem, separate memory. This is real multi-instance architecture, not just process isolation. Each sandbox has its own compiled racecar binary, its own database, its own HNSW index.

The benefits:

- **True isolation** -- A crash or memory corruption in one sandbox cannot affect the others. This is stronger than process isolation via `fork()`.
- **Independent scaling** -- Each sandbox can have different resources. The attack category could get more memory if it has more training data.
- **Parallel compilation** -- All 3 sandboxes compile the C code simultaneously during `racecar up`.
- **Persistent volumes** -- Data survives sandbox destruction. Tear down and recreate sandboxes without losing training data.

The tradeoff is network latency. A local `fork()` search takes ~1ms; a sandbox search takes ~1-2s round-trip. For email classification, this is acceptable. For high-throughput workloads, the standalone local mode is still available.

### Data Storage

Active data (.rct table files) is stored on each sandbox's LOCAL filesystem at `/tmp/racecar-data/`. This is ephemeral — destroyed when the sandbox is torn down. Data is regenerated by `racecar init`.

Daytona volumes (FUSE-mounted, backed by S3) are available for persistence but are NOT used for active data because FUSE file operations can timeout under ExecuteCommand's time constraints. The Go orchestrator uploads binary .rct files directly to the local filesystem via `FileSystem.UploadFile`, bypassing the volume entirely.

## How `racecar up` Works

Step by step:

1. **Create Daytona client** -- Read `DAYTONA_API_KEY` from the environment, connect to the Daytona API (default: `https://app.daytona.io/api`).

2. **Create volume** -- Create a persistent Daytona volume called `racecar-data`. This is free storage that survives sandbox destruction.

3. **Fork 3 goroutines** -- One for each category (safe, spam, attack). All 3 run in parallel.

4. **Each goroutine creates a sandbox** with the volume mounted:
   - Provision an Ubuntu sandbox from Daytona
   - Mount the `racecar-data` volume
   - Install `gcc` and `make` inside the sandbox
   - Upload the C source files from `src/` into the sandbox
   - Compile racecar (`make` inside the sandbox)
   - Create the database and vector table for that category on the local filesystem (`/tmp/racecar-data/`), not on the FUSE volume
   - Report back to the main goroutine

5. **Save state file** -- Write sandbox IDs and metadata to `~/.racecar/state.json` so future commands know which sandboxes to connect to.

The whole process takes about 30-60 seconds. Most of that is sandbox provisioning and `apt-get install gcc`.

## How `racecar classify` Works

1. **Vectorize email locally** -- The Go binary takes the subject and body, concatenates them as "subject: {subject} body: {body}", and runs enhanced feature hashing (unigrams with keyword boosting, bigrams, character trigrams, leet normalization) to produce a 256-dimensional float vector. This happens instantly on your machine, no network needed.

2. **Read state file** -- Load `~/.racecar/state.json` to get the 3 sandbox IDs.

3. **Connect to 3 sandboxes** -- Open connections to all 3 Daytona sandboxes.

4. **Launch 3 goroutines** -- Each goroutine:
   - Connects to its sandbox
   - Sends the vector as a CSV string
   - Runs `racecar search` (the C binary) inside the sandbox
   - The C binary does an HNSW index search and returns the 5 nearest neighbors with cosine distances
   - Parses the results

5. **Collect results** -- Wait for all 3 goroutines to finish. Each returns an average cosine distance across its 5 nearest neighbors.

6. **Pick the winner** -- The category with the lowest average distance is the classification. Compute confidence from the ratio of best-to-second-best distance.

7. **Stage 2 (if ATTACK)** -- If the winning category is ATTACK, run `AnalyzeFromText` which checks the email body for urgency language, credential/financial requests, suspicious URLs, and malware indicators. Determine attack subtype and risk score.

## How `racecar classify-raw` Works

1. **Parse raw email** -- The raw email text is split into headers and body. Headers are unfolded, parsed into key-value pairs, and core fields (From, Reply-To, Return-Path, Authentication-Results, etc.) are extracted. Domains are parsed from email address headers.

2. **Run Stage 1** -- The parsed subject and body go through the same vector classification as `racecar classify`.

3. **Run Stage 2 (always)** -- For raw emails, Stage 2 always runs regardless of the Stage 1 result, because header analysis can surface risks that vector search misses. The full analysis includes domain mismatch detection, SPF/DKIM/DMARC checks, typosquat detection, and all content-based checks.

4. **Return combined result** -- The output includes both Stage 1 scores and Stage 2 signals, subtype, and risk score.

## Data Flow

```
Email text
  → Go enhanced feature hashing (local, instant)
    → leet normalization + character trigrams + keyword boosting
  → 256-dim float vector
  → sent as CSV string to 3 sandboxes (parallel)
  → racecar search (C, inside each sandbox)
  → cosine distances returned
  → Go aggregates across 3 sandboxes
  → Stage 1 classification result
  → [if ATTACK] Stage 2 context analysis (local)
  → final verdict with attack subtype + risk score
```

The vectorization uses enhanced feature hashing: FNV-1a hashing of unigrams (with keyword boosting), bigrams (weight 0.7), and character trigrams (weight 0.3), with sign hashing to reduce collisions, followed by L2 normalization. The leet-normalized version is processed with weight 0.5 and added to the same vector. The Go implementation in `vectorize.go` produces vectors compatible with the storage format in the sandboxes.

## Persistence

Daytona volumes survive sandbox destruction. This means:

- `racecar down` tears down the 3 sandboxes but leaves the volume intact
- `racecar up` creates new sandboxes and mounts the same volume
- All training data, vector tables, and HNSW indexes persist across sandbox cycles
- The volume is free -- no cost for persistent storage

The state file at `~/.racecar/state.json` tracks which sandboxes are currently active. If sandboxes are destroyed (via `racecar down` or from the Daytona dashboard), running `racecar up` creates fresh ones that reconnect to the existing volume data.

## The C Engine

The C code in `src/` is the same vector database that has always been in this repo. Here is how it works internally.

### Storage Model

A database is a directory. A table is a binary file inside it.

```
/data/                        Data directory (inside sandbox)
  sentinel/                   Database = a directory
    emails_safe.rct           Table = a binary file
    emails_safe.rcx           HNSW index = a separate binary file
```

### Table File Format (.rct)

Each `.rct` file has a 64-byte header followed by fixed-size records:

```
[Header: 64 bytes]
  magic (4)          "RCRB" identifier
  version (4)        format version
  dimensions (4)     vector size (e.g. 256)
  metric (4)         0=cosine, 1=euclidean, 2=dot
  meta_size (4)      metadata bytes per record (default 1024)
  record_count (8)   number of records
  next_id (8)        auto-incrementing ID counter
  capacity (8)       allocated slots
  ...padding...

[Record 0]
  id (8 bytes)                    uint64, auto-assigned
  flags (4 bytes)                 bit 0 = active (deleted records have flag cleared)
  meta_len (4 bytes)              actual metadata length
  vector (dimensions * 4 bytes)   the float array
  metadata (meta_size bytes)      JSON string, null-padded

[Record 1]
  ...same layout...
```

Every record is exactly the same size (`16 + dimensions*4 + meta_size` bytes). Record N lives at byte offset `64 + N * record_size` -- direct array indexing, no seeking.

### HNSW Index

The HNSW (Hierarchical Navigable Small World) index provides fast approximate nearest-neighbor search. It is a multi-layer graph:

```
Layer 3:  [A] ---- [D]                           (very few nodes, long-range links)
Layer 2:  [A] -- [C] -- [D] -- [F]               (more nodes, medium links)
Layer 1:  [A] [B] [C] [D] [E] [F] [G]            (more nodes, shorter links)
Layer 0:  [A] [B] [C] [D] [E] [F] [G] [H] [I]   (all nodes, local links)
```

Search starts at the top layer and greedily walks toward the query vector, dropping down a layer at each step. At layer 0, a wider beam search collects final candidates. This gives O(log N) search complexity instead of O(N) brute-force.

Parameters:
- `M` -- max neighbors per node per layer. Default: 16.
- `ef_construction` -- beam width when building the index. Default: 200.
- `ef_search` -- beam width at query time. Default: 50.

### Vector Distance Functions

The vector math in `vector.c` is written for speed:
- 4x loop unrolling to reduce loop overhead
- `__restrict__` pointers so the compiler can auto-vectorize (SSE/AVX/NEON)
- Single-pass cosine: computes dot product, norm-a, and norm-b in one loop
- Compiler flags: `-O3 -march=native -ffast-math`

## Standalone Mode

The C tools still work locally without Daytona. The `sentinel/` directory contains a standalone email classifier that uses `fork()` for parallel search instead of cloud sandboxes.

```bash
# Build C tools locally (requires gcc/clang)
cd src && make && cd ..
cd sentinel && make && cd ..
```

This is useful for development, testing, or environments where you do not want cloud infrastructure.

## Lessons Learned

### FUSE Volumes and ExecuteCommand Timeouts

Daytona's `ExecuteCommand` has a timeout. Daytona volumes are FUSE-mounted (backed by S3-compatible storage). File operations on FUSE mounts — even simple reads — can exceed the ExecuteCommand timeout, causing exit code -1.

The fix: store active data on the sandbox's local filesystem (`/tmp/`), which is a regular Linux tmpfs with no network latency. Use `FileSystem.UploadFile` to transfer data (binary upload has no timeout issues). Reserve FUSE volumes for data that must survive sandbox destruction.

### Binary File Upload vs CLI Commands

Inserting records via `ExecuteCommand` (running `racecar insert` for each record) has two problems:
1. The 256-dimensional vector as a command-line argument is ~3000 characters, which can cause issues
2. Each insert is a network round-trip, and many inserts can collectively timeout

The fix: build the binary `.rct` file in Go memory (matching the exact C binary format) and upload it as a single file via `FileSystem.UploadFile`. This transfers 160KB of data in one operation instead of 78 separate command executions.

### Compilation in Sandboxes

Use `gcc` directly (single invocation, all source files) rather than `make` (multiple invocations). The single `gcc` command is faster and avoids spawning multiple processes that could collectively timeout. Use `-std=gnu11` (not `-std=c11`) to enable POSIX extensions like `strdup` and `dirent`.

## How the Pieces Connect

```
main.go (Go CLI)
  │
  ├── engine.go
  │     ├── Daytona SDK → create/destroy sandboxes
  │     ├── ClassifyEmail() → two-stage pipeline orchestration
  │     ├── ClassifyRawEmail() → parse headers + two-stage pipeline
  │     └── Goroutines → parallel search across 3 sandboxes
  │
  ├── vectorize.go
  │     ├── Enhanced feature hashing (unigrams, bigrams, char trigrams)
  │     ├── Leet speak normalization
  │     └── Keyword boosting (attack/spam indicator weights)
  │
  ├── email_parser.go
  │     ├── Raw email parsing (headers + body split)
  │     ├── Domain extraction from From/Reply-To/Return-Path
  │     ├── Authentication-Results → SPF/DKIM/DMARC verdicts
  │     └── URL extraction from body text
  │
  ├── header_analysis.go
  │     ├── Domain mismatch detection
  │     ├── Typosquat detection (Levenshtein distance vs 16 brands)
  │     ├── Urgency language density scoring
  │     ├── Credential/financial request pattern matching
  │     ├── Suspicious URL and malware indicator detection
  │     └── Attack subtype classification + risk scoring
  │
  ├── training.go
  │     └── 142 base training emails (47 safe, 44 spam, 51 attack)
  │
  ├── training_advanced.go
  │     └── 54 adversarial training emails (14 safe, 13 spam, 27 attack)
  │         (leet speak, misspellings, boundary-case language)
  │
  ├── sample_raw_emails.go
  │     └── 12 raw test emails with full headers for test-raw
  │
  └── (inside each sandbox)
        │
        └── racecar (C binary, compiled from src/)
              ├── main.c        CLI entry point
              ├── db.c          database management
              ├── table.c       storage engine (binary .rct files)
              ├── vector.c      SIMD-optimized distance math
              ├── hnsw.c        HNSW index (binary .rcx files)
              ├── json.c        JSON parser
              ├── tokenizer.c   text vectorizer
              └── util.c        utilities
```

The dependency chain:
- `vector.c` depends on nothing (pure math)
- `table.c` depends on `vector.c` (for distance computation during scan)
- `hnsw.c` depends on `vector.c` (for distance computation during graph traversal)
- `json.c` depends on nothing (standalone parser)
- `tokenizer.c` depends on `json.c` (for JSON vectorization)
- `main.c` ties the C layer together
- `vectorize.go` runs locally, mirrors and extends the C tokenizer's feature hashing
- `email_parser.go` parses raw emails into structured data for analysis
- `header_analysis.go` runs Stage 2 analysis using parsed email data
- `engine.go` ties the Go layer together, orchestrates the two-stage pipeline
- `main.go` exposes everything as CLI commands
