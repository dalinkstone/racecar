# Shelf

A personal knowledge search engine that runs inside a [Daytona](https://daytona.io) sandbox.

Store text snippets — notes, quotes, ideas, learnings — and search them by meaning. Shelf converts your text into vectors using feature hashing (no paid embedding API), stores them in a C vector database compiled and running inside an isolated Daytona sandbox, and persists everything to a free Daytona volume.

## Prerequisites

- **Go 1.22+** — [install Go](https://go.dev/dl/)
- **A Daytona account** — free at [app.daytona.io](https://app.daytona.io)
- **A Daytona API key** — generate one from the Daytona dashboard under API Keys

No C compiler is needed on your machine. The C code compiles inside the sandbox.

## Setup

### 1. Clone and install dependencies

```bash
cd racecar
go mod tidy
```

### 2. Set your Daytona API key

```bash
export DAYTONA_API_KEY=your_key_here
```

You can also place it in a `.env` file in the project root:

```
DAYTONA_API_KEY=your_key_here
```

### 3. Start the server

```bash
go run . serve
```

On first run this will:

1. Create a persistent Daytona volume called `shelf-data` (free, one-time)
2. Spin up a sandbox with the volume mounted
3. Install `gcc` and `make` inside the sandbox if not already present
4. Upload the C vector database source files (`vectordb/`)
5. Compile the `vectordb_cli` binary inside the sandbox
6. Load any previously stored data from the volume

You'll see output like:

```
Connecting to Daytona...
Setting up persistent volume...
Volume ready: shelf-data
Creating sandbox...
Sandbox ready: abc123def
Build tools already installed
Uploading C source files...
Compiling...
Compilation successful
Loaded 0 existing entries
Shelf server running on :8080
```

The server stays running until you press `Ctrl+C`, which cleanly shuts down the sandbox.

### 4. Use the CLI (in a second terminal)

```bash
# Add some notes
go run . add "Goroutines are lightweight threads managed by the Go runtime"
go run . add "Python's GIL prevents true CPU parallelism in threaded code"
go run . add "Rust's borrow checker prevents data races at compile time"
go run . add "Docker containers share the host kernel unlike full VMs"
go run . add "PostgreSQL MVCC allows readers to never block writers"

# Search by meaning
go run . search "concurrent programming"

# List everything
go run . list

# Check server info
go run . status

# Delete by ID (use an ID from list output)
go run . delete abc12345
```

## CLI Reference

| Command | Description |
|---------|-------------|
| `go run . serve [--port PORT]` | Start the server and sandbox (default port: 8080) |
| `go run . add "text"` | Store a note |
| `go run . search "query" [--k N]` | Find similar notes (default: top 5) |
| `go run . list` | Show all stored notes |
| `go run . delete <id>` | Remove a note by its ID |
| `go run . status` | Show entry count and sandbox ID |

You can also build a binary first:

```bash
go build -o shelf .
./shelf serve
./shelf add "some note"
```

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `DAYTONA_API_KEY` | — | **Required.** Your Daytona API key |
| `DAYTONA_API_URL` | `https://app.daytona.io/api` | Daytona API endpoint |
| `DAYTONA_TARGET` | org default | Target region (`us` or `eu`) |
| `SHELF_PORT` | `8080` | HTTP server port |

## HTTP API

All CLI commands (except `serve`) talk to the running server over HTTP. You can also call the API directly.

### Add an entry

```
POST /entries
Content-Type: application/json

{"text": "your note here"}
```

Response: `{"id": "a1b2c3d4", "text": "your note here"}`

### Search

```
POST /search
Content-Type: application/json

{"query": "search terms", "k": 5}
```

Response:

```json
[
  {"id": "a1b2c3d4", "text": "matching note...", "score": 0.87},
  {"id": "e5f6a7b8", "text": "another match...", "score": 0.52}
]
```

### List all entries

```
GET /entries
```

Response: `[{"id": "a1b2c3d4", "text": "..."}, ...]`

### Delete an entry

```
DELETE /entries/{id}
```

Response: `{"status": "ok"}`

### Server status

```
GET /status
```

Response: `{"status": "ok", "entries": 5, "sandbox_id": "abc123"}`

## How It Works

### Architecture

```
                                        Daytona Cloud
                                   ┌─────────────────────┐
  CLI / curl                       │   Sandbox            │
  ──────────                       │  ┌───────────────┐   │
  shelf add "text"                 │  │ vectordb_cli   │   │
       │                           │  │ (compiled C)   │   │
       ▼                           │  └───────┬───────┘   │
  Go HTTP Server ───── Daytona ──▸ │          │           │
  (feature hashing,    SDK         │          ▼           │
   metadata mgmt)                  │  ┌───────────────┐   │
                                   │  │ shelf.vdb      │   │
                                   │  │ metadata.json  │   │
                                   │  │ (on volume)    │   │
                                   │  └───────────────┘   │
                                   └─────────────────────┘
```

1. The **Go server** receives text, converts it to a 128-dimensional vector using feature hashing, and sends the vector to the sandbox via the Daytona SDK.

2. Inside the sandbox, the **C vector database CLI** (`vectordb_cli`) performs the insert, search, or delete operation. Each call loads the database from disk, runs the operation, and saves back.

3. The database file (`shelf.vdb`) and metadata (`metadata.json`) live on a **Daytona volume** — a free persistent FUSE mount backed by S3-compatible storage. Data survives sandbox restarts and deletions.

### Vectorization

Shelf uses **feature hashing** (the "hashing trick") to convert text into fixed-size vectors without any external API:

1. Lowercase and tokenize the text (split on non-alphanumeric characters)
2. Remove common English stop words (the, is, and, etc.)
3. Hash each remaining word with FNV-1a to an index in `[0, 128)`
4. Increment the count at that index
5. L2-normalize the resulting vector

The C database then uses **cosine similarity** for nearest-neighbor search. This approach is free, fast, and works surprisingly well for short-to-medium text — similar words hash to similar positions, producing vectors that cluster by topic.

Limitations: feature hashing doesn't understand synonyms or word order. "fast car" and "quick automobile" won't match well. For semantic understanding you'd need real embeddings (OpenAI, Cohere, etc.), but those cost money per call.

### Persistence

Daytona volumes are free (up to 100 per organization) and don't count against storage quotas. Shelf uses a single volume called `shelf-data` mounted at `/home/daytona/data` inside the sandbox. Two files are stored:

- `shelf.vdb` — binary vector database (loaded/saved by the C CLI on each operation)
- `metadata.json` — maps vector IDs to their original text (managed by Go)

When you restart `shelf serve`, it creates a new sandbox but mounts the same volume, so all your data is still there.

## Project Structure

```
vectordb/              C vector database (runs inside sandbox)
  vectordb.h           API: init, insert, delete, search, save, load
  vectordb.c           Implementation (cosine similarity, brute-force kNN, persistence)
  cli.c                CLI tool with JSON output (used by Go via ExecuteCommand)
  test_vectordb.c      24 unit tests
  Makefile             Builds test_vectordb and vectordb_cli

main.go                Go entry point — CLI parsing, HTTP server, embedded C files
engine.go              Daytona sandbox/volume management, feature hashing, metadata
handlers.go            HTTP route handlers
go.mod / go.sum        Go module dependencies
```

The C source files are embedded into the Go binary via `//go:embed`, so the compiled `shelf` binary is fully self-contained.

## Testing the C Database Locally

You don't need Daytona to test the underlying vector database:

```bash
cd vectordb
make test
```

This compiles and runs the test suite (24 tests covering insert, delete, search, save/load, and cosine similarity correctness).

## Costs

| Component | Cost |
|-----------|------|
| Daytona volume | Free (up to 100 per org) |
| Daytona sandbox | Free tier available |
| Text vectorization | Free (runs locally in Go, no API calls) |
