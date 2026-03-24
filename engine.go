package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/daytonaio/daytona/libs/sdk-go/pkg/daytona"
	"github.com/daytonaio/daytona/libs/sdk-go/pkg/types"
)

// execWithRetry runs a command in a sandbox, retrying once on timeout (exit code -1).
func (e *Engine) execWithRetry(ctx context.Context, sandbox *daytona.Sandbox, cmd string) (*types.ExecuteResponse, error) {
	start := time.Now()
	resp, err := sandbox.Process.ExecuteCommand(ctx, cmd)
	elapsed := time.Since(start)
	if elapsed > 5*time.Second {
		truncCmd := cmd
		if len(truncCmd) > 80 {
			truncCmd = truncCmd[:80]
		}
		log.Printf("[slow-cmd] %.1fs: %s", elapsed.Seconds(), truncCmd)
	}
	if err == nil && resp.ExitCode == -1 {
		// Timeout — retry once after a short pause
		log.Printf("[retry] command timed out (exit -1), retrying after 1s: %s", cmd[:min(80, len(cmd))])
		time.Sleep(1 * time.Second)
		start = time.Now()
		resp, err = sandbox.Process.ExecuteCommand(ctx, cmd)
		elapsed = time.Since(start)
		if elapsed > 5*time.Second {
			truncCmd := cmd
			if len(truncCmd) > 80 {
				truncCmd = truncCmd[:80]
			}
			log.Printf("[slow-cmd] %.1fs (retry): %s", elapsed.Seconds(), truncCmd)
		}
	}
	return resp, err
}

// ================================================================
// Constants
// ================================================================

const (
	TopK          = 5
	volumeName    = "racecar-data"
	remoteSrcDir  = "/home/daytona/racecar"
	remoteDataDir = "/home/daytona/data"
	remoteBin     = "/home/daytona/racecar/racecar"
	stateFileName = "state.json"
	activeDataDir = "/tmp/racecar-data" // local filesystem — fast, not FUSE
)

// Categories are the three email classification buckets.
// Each gets its own Daytona sandbox with its own racecar vector database.
var Categories = [3]string{"safe", "spam", "attack"}

// Source files to upload into each sandbox
var sourceFileList = []string{
	"src/racecar.h",
	"src/main.c",
	"src/db.c",
	"src/table.c",
	"src/vector.c",
	"src/hnsw.c",
	"src/json.c",
	"src/tokenizer.c",
	"src/util.c",
}

// sandboxMakefile is the C-only Makefile uploaded as "Makefile" to each sandbox
const sandboxMakefileEmbed = "Makefile.sandbox"

// ================================================================
// Types
// ================================================================

// SandboxState holds persisted sandbox IDs so we can reconnect.
type SandboxState struct {
	VolumeID  string            `json:"volume_id"`
	Sandboxes map[string]string `json:"sandboxes"` // category -> sandbox ID
}

// Engine manages 3 Daytona sandboxes for parallel vector search.
type Engine struct {
	client    *daytona.Client
	sandboxes map[string]*daytona.Sandbox // category -> sandbox
	volumeID  string
	mu        sync.RWMutex
}

// ================================================================
// State file helpers
// ================================================================

func stateFilePath() string {
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".racecar", stateFileName)
}

func loadState() (*SandboxState, error) {
	data, err := os.ReadFile(stateFilePath())
	if err != nil {
		return nil, err
	}
	var state SandboxState
	err = json.Unmarshal(data, &state)
	return &state, err
}

func saveState(state *SandboxState) error {
	dir := filepath.Dir(stateFilePath())
	os.MkdirAll(dir, 0755)
	data, err := json.MarshalIndent(state, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(stateFilePath(), data, 0644)
}

func clearState() {
	os.Remove(stateFilePath())
}

// ================================================================
// NewEngine — create 3 sandboxes from scratch
// ================================================================

func NewEngine(ctx context.Context) (*Engine, error) {
	log.Println("Connecting to Daytona...")
	client, err := daytona.NewClient()
	if err != nil {
		return nil, fmt.Errorf("create client: %w", err)
	}

	// Get or create volume
	log.Println("Setting up persistent volume...")
	vol, err := client.Volume.Get(ctx, volumeName)
	if err != nil {
		vol, err = client.Volume.Create(ctx, volumeName)
		if err != nil {
			return nil, fmt.Errorf("create volume: %w", err)
		}
		vol, err = client.Volume.WaitForReady(ctx, vol, 60*time.Second)
		if err != nil {
			return nil, fmt.Errorf("wait for volume: %w", err)
		}
	}
	log.Printf("Volume ready: %s", vol.Name)

	eng := &Engine{
		client:    client,
		sandboxes: make(map[string]*daytona.Sandbox),
		volumeID:  vol.ID,
	}

	// Create 3 sandboxes in parallel
	var wg sync.WaitGroup
	var mu sync.Mutex
	var firstErr error

	for _, cat := range Categories {
		wg.Add(1)
		go func(category string) {
			defer wg.Done()
			log.Printf("Creating sandbox [%s]...", category)

			sandbox, err := client.Create(ctx, types.SnapshotParams{
				SandboxBaseParams: types.SandboxBaseParams{
					Volumes: []types.VolumeMount{
						{VolumeID: vol.ID, MountPath: remoteDataDir},
					},
				},
			})
			if err != nil {
				mu.Lock()
				if firstErr == nil {
					firstErr = fmt.Errorf("create sandbox [%s]: %w", category, err)
				}
				mu.Unlock()
				return
			}

			mu.Lock()
			eng.sandboxes[category] = sandbox
			mu.Unlock()
			log.Printf("Sandbox [%s] ready: %s", category, sandbox.ID)
		}(cat)
	}
	wg.Wait()

	if firstErr != nil {
		eng.Destroy(ctx)
		return nil, firstErr
	}

	// Setup each sandbox in parallel (install tools, upload code, compile)
	for _, cat := range Categories {
		wg.Add(1)
		go func(category string) {
			defer wg.Done()
			sandbox := eng.sandboxes[category]

			if err := eng.setupSandbox(ctx, sandbox, category); err != nil {
				mu.Lock()
				if firstErr == nil {
					firstErr = fmt.Errorf("setup [%s]: %w", category, err)
				}
				mu.Unlock()
			}
		}(cat)
	}
	wg.Wait()

	if firstErr != nil {
		eng.Destroy(ctx)
		return nil, firstErr
	}

	// Save state for later reconnection
	state := &SandboxState{
		VolumeID:  vol.ID,
		Sandboxes: make(map[string]string),
	}
	for cat, sb := range eng.sandboxes {
		state.Sandboxes[cat] = sb.ID
	}
	if err := saveState(state); err != nil {
		log.Printf("Warning: could not save state: %v", err)
	}

	return eng, nil
}

// ================================================================
// ConnectEngine — reconnect to existing sandboxes from state file
// ================================================================

// ConnectEngine reconnects to existing sandboxes from the state file.
// If forcePartial is true, partial connectivity is allowed (some sandboxes
// may be unavailable). By default all sandboxes must be reachable.
func ConnectEngine(ctx context.Context, forcePartial ...bool) (*Engine, error) {
	allowPartial := len(forcePartial) > 0 && forcePartial[0]

	state, err := loadState()
	if err != nil {
		return nil, fmt.Errorf("no active sandboxes (run 'racecar up' first): %w", err)
	}

	client, err := daytona.NewClient()
	if err != nil {
		return nil, fmt.Errorf("create client: %w", err)
	}

	eng := &Engine{
		client:    client,
		sandboxes: make(map[string]*daytona.Sandbox),
		volumeID:  state.VolumeID,
	}

	// Reconnect to each sandbox
	var connectErrors []string
	for cat, id := range state.Sandboxes {
		sandbox, err := client.Get(ctx, id)
		if err != nil {
			errMsg := fmt.Sprintf("[%s] (%s): %v", cat, id, err)
			connectErrors = append(connectErrors, errMsg)
			log.Printf("WARNING: failed to reconnect to sandbox %s", errMsg)
			continue
		}
		eng.sandboxes[cat] = sandbox
	}

	if len(connectErrors) > 0 {
		if len(eng.sandboxes) == 0 {
			return nil, fmt.Errorf("all sandbox connections failed: %s", strings.Join(connectErrors, "; "))
		}
		if !allowPartial {
			return nil, fmt.Errorf("some sandbox connections failed (use --force to continue with partial connectivity): %s",
				strings.Join(connectErrors, "; "))
		}
		log.Printf("WARNING: running with %d/%d sandboxes (failed: %s)",
			len(eng.sandboxes), len(state.Sandboxes), strings.Join(connectErrors, "; "))
	}

	return eng, nil
}

// ================================================================
// setupSandbox — per-sandbox initialization
// ================================================================

func (e *Engine) setupSandbox(ctx context.Context, sandbox *daytona.Sandbox, category string) error {
	// Check if build tools are already available
	resp, err := sandbox.Process.ExecuteCommand(ctx, "gcc --version")
	if err != nil {
		return fmt.Errorf("check gcc: %w", err)
	}

	if resp.ExitCode != 0 {
		log.Printf("[%s] Installing build tools...", category)

		// Try apt-get (Debian/Ubuntu) — most common for Daytona sandboxes
		resp, err = sandbox.Process.ExecuteCommand(ctx, "which apt-get")
		if err == nil && resp.ExitCode == 0 {
			resp, err = sandbox.Process.ExecuteCommand(ctx, "apt-get update")
			if err != nil {
				return fmt.Errorf("apt-get update: %w", err)
			}
			if resp.ExitCode != 0 {
				log.Printf("[%s] apt-get update output: %s", category, resp.Result)
				return fmt.Errorf("apt-get update failed (exit %d)", resp.ExitCode)
			}
			resp, err = sandbox.Process.ExecuteCommand(ctx, "apt-get install -y gcc make")
			if err != nil {
				return fmt.Errorf("apt-get install: %w", err)
			}
			if resp.ExitCode != 0 {
				return fmt.Errorf("apt-get install failed (exit %d): %s", resp.ExitCode, resp.Result)
			}
		} else {
			// Try apk (Alpine)
			resp, err = sandbox.Process.ExecuteCommand(ctx, "apk add --no-cache gcc musl-dev make")
			if err != nil || resp.ExitCode != 0 {
				return fmt.Errorf("cannot install build tools: %s", resp.Result)
			}
		}
		log.Printf("[%s] Build tools installed", category)
	} else {
		log.Printf("[%s] Build tools already present", category)
	}

	// Create directories
	catDataDir := activeDataDir + "/" + category
	resp, err = sandbox.Process.ExecuteCommand(ctx, "mkdir -p "+remoteSrcDir+"/src")
	if err != nil || resp.ExitCode != 0 {
		return fmt.Errorf("mkdir src: %w", err)
	}
	resp, err = sandbox.Process.ExecuteCommand(ctx, "mkdir -p "+catDataDir+"/sentinel")
	if err != nil || resp.ExitCode != 0 {
		return fmt.Errorf("mkdir data: %w", err)
	}

	// Upload C source files
	log.Printf("[%s] Uploading racecar source...", category)
	for _, name := range sourceFileList {
		content, err := embeddedFiles.ReadFile(name)
		if err != nil {
			return fmt.Errorf("read embedded %s: %w", name, err)
		}
		remotePath := remoteSrcDir + "/" + name
		if err := sandbox.FileSystem.UploadFile(ctx, content, remotePath); err != nil {
			return fmt.Errorf("upload %s: %w", name, err)
		}
	}

	// Upload sandbox-specific Makefile
	makeContent, err := embeddedFiles.ReadFile(sandboxMakefileEmbed)
	if err != nil {
		return fmt.Errorf("read embedded Makefile.sandbox: %w", err)
	}
	if err := sandbox.FileSystem.UploadFile(ctx, makeContent, remoteSrcDir+"/Makefile"); err != nil {
		return fmt.Errorf("upload Makefile: %w", err)
	}

	// Verify files are in place
	resp, err = sandbox.Process.ExecuteCommand(ctx, "ls "+remoteSrcDir+"/Makefile "+remoteSrcDir+"/src/racecar.h")
	if err != nil || resp.ExitCode != 0 {
		return fmt.Errorf("files not uploaded correctly: %s", resp.Result)
	}
	log.Printf("[%s] Files verified: %s", category, resp.Result)

	// Compile — single gcc invocation is faster and avoids make timeout issues
	log.Printf("[%s] Compiling racecar...", category)
	compileCmd := fmt.Sprintf(
		"gcc -O2 -Wall -std=gnu11 -o %s/racecar "+
			"%s/src/main.c %s/src/db.c %s/src/table.c %s/src/vector.c "+
			"%s/src/hnsw.c %s/src/json.c %s/src/tokenizer.c %s/src/util.c -lm",
		remoteSrcDir,
		remoteSrcDir, remoteSrcDir, remoteSrcDir, remoteSrcDir,
		remoteSrcDir, remoteSrcDir, remoteSrcDir, remoteSrcDir,
	)
	// Use a longer timeout context for compilation
	compileCtx, cancel := context.WithTimeout(ctx, 120*time.Second)
	defer cancel()
	resp, err = sandbox.Process.ExecuteCommand(compileCtx, compileCmd)
	if err != nil {
		return fmt.Errorf("compile: %w", err)
	}
	if resp.ExitCode != 0 {
		return fmt.Errorf("compilation failed (exit %d):\n%s", resp.ExitCode, resp.Result)
	}
	log.Printf("[%s] Compilation successful", category)

	// Verify binary exists
	resp, err = sandbox.Process.ExecuteCommand(ctx, "ls -la "+remoteSrcDir+"/racecar")
	if err != nil || resp.ExitCode != 0 {
		return fmt.Errorf("racecar binary not found after compilation")
	}
	log.Printf("[%s] Binary: %s", category, resp.Result)

	// Create the database and table for this category
	dbName := "sentinel"
	tableName := "emails_" + category
	catBin := remoteSrcDir + "/racecar"

	// Create db (ignore "exists" errors)
	sandbox.Process.ExecuteCommand(ctx, fmt.Sprintf("%s --data-dir %s db-create %s", catBin, catDataDir, dbName))

	// Create table (ignore "exists" errors) — 256-dim cosine
	sandbox.Process.ExecuteCommand(ctx, fmt.Sprintf("%s --data-dir %s table-create %s %s 256 cosine", catBin, catDataDir, dbName, tableName))

	log.Printf("[%s] Ready", category)
	return nil
}

// ================================================================
// Core operations
// ================================================================

// Exec runs a racecar CLI command in a specific category's sandbox.
func (e *Engine) Exec(ctx context.Context, category string, args string) (string, error) {
	sandbox, ok := e.sandboxes[category]
	if !ok {
		return "", fmt.Errorf("unknown category: %s", category)
	}
	catDataDir := activeDataDir + "/" + category
	cmd := fmt.Sprintf("%s --data-dir %s %s", remoteBin, catDataDir, args)

	start := time.Now()
	resp, err := e.execWithRetry(ctx, sandbox, cmd)
	elapsed := time.Since(start)
	if elapsed > 5*time.Second {
		log.Printf("[%s] Slow command (%.1fs): %s", category, elapsed.Seconds(), args[:min(80, len(args))])
	}

	if err != nil {
		return "", fmt.Errorf("exec in [%s]: %w", category, err)
	}
	if resp.ExitCode != 0 {
		return resp.Result, fmt.Errorf("command failed in [%s] (exit %d): %s", category, resp.ExitCode, resp.Result)
	}
	return resp.Result, nil
}

// Insert vectorizes text and inserts into the specified category's sandbox.
func (e *Engine) Insert(ctx context.Context, category string, subject, body string) (string, error) {
	return e.BatchInsert(ctx, category, []struct{ Subject, Body string }{{subject, body}})
}

// BatchInsert vectorizes multiple emails and writes the .rct binary file directly.
// This bypasses ExecuteCommand entirely for data loading — no timeout issues.
func (e *Engine) BatchInsert(ctx context.Context, category string, emails []struct{ Subject, Body string }) (string, error) {
	sandbox, ok := e.sandboxes[category]
	if !ok {
		return "", fmt.Errorf("unknown category: %s", category)
	}

	const (
		magic      = 0x52435242
		version    = 1
		dimensions = 256
		metric     = 0 // cosine
		metaSize   = 1024
		headerSize = 64
		recordSize = 16 + dimensions*4 + metaSize // 2064 bytes
	)

	count := uint64(len(emails))
	capacity := count
	if capacity < 1024 {
		capacity = 1024
	}

	// Build binary .rct file in memory
	fileSize := headerSize + int(count)*recordSize
	data := make([]byte, fileSize)

	// Write header (64 bytes, little-endian)
	le := func(b []byte, v uint32) { b[0] = byte(v); b[1] = byte(v >> 8); b[2] = byte(v >> 16); b[3] = byte(v >> 24) }
	le64 := func(b []byte, v uint64) {
		b[0] = byte(v); b[1] = byte(v >> 8); b[2] = byte(v >> 16); b[3] = byte(v >> 24)
		b[4] = byte(v >> 32); b[5] = byte(v >> 40); b[6] = byte(v >> 48); b[7] = byte(v >> 56)
	}

	le(data[0:4], magic)
	le(data[4:8], version)
	le(data[8:12], dimensions)
	le(data[12:16], metric)
	le(data[16:20], metaSize)
	le(data[20:24], 0) // pad
	le64(data[24:32], count)
	le64(data[32:40], count+1) // next_id
	le64(data[40:48], capacity)
	// bytes 48-63: reserved (already zeroed)

	// Write records
	for i, email := range emails {
		combined := fmt.Sprintf("subject: %s body: %s", email.Subject, email.Body)
		vec := vectorize(combined)

		meta := fmt.Sprintf(`{"subject":"%s"}`, escapeJSON(email.Subject))
		if len(meta) >= metaSize {
			meta = meta[:metaSize-1]
		}

		offset := headerSize + i*recordSize
		rec := data[offset : offset+recordSize]

		// id (uint64)
		le64(rec[0:8], uint64(i+1))
		// flags (uint32) — RC_RECORD_ACTIVE = 0x01
		le(rec[8:12], 1)
		// meta_len (uint32)
		le(rec[12:16], uint32(len(meta)))
		// vector (256 floats, little-endian)
		for j, v := range vec {
			bits := math.Float32bits(v)
			off := 16 + j*4
			le(rec[off:off+4], bits)
		}
		// metadata (null-terminated string in metaSize bytes)
		copy(rec[16+dimensions*4:], []byte(meta))
	}

	// Upload the .rct file directly to the sandbox
	catDataDir := activeDataDir + "/" + category
	dbDir := catDataDir + "/sentinel"
	tablePath := dbDir + "/emails_" + category + ".rct"

	// Ensure directory exists
	sandbox.Process.ExecuteCommand(ctx, "mkdir -p "+dbDir)

	if err := sandbox.FileSystem.UploadFile(ctx, data, tablePath); err != nil {
		return "", fmt.Errorf("upload .rct file: %w", err)
	}

	return fmt.Sprintf("wrote %d records (%d bytes)", count, len(data)), nil
}

// SearchCategory searches a single category and returns avg distance + match count.
// Passes the query vector directly as a command-line argument (256 floats ~ 2.5KB,
// well within OS limits). Uses --data-dir flag instead of env vars to avoid
// shell interpretation issues in Daytona's ExecuteCommand.
func (e *Engine) SearchCategory(ctx context.Context, category string, vecStr string, topK int) (float64, int, error) {
	sandbox, ok := e.sandboxes[category]
	if !ok {
		return 1e30, 0, fmt.Errorf("unknown category: %s", category)
	}

	catDataDir := activeDataDir + "/" + category

	searchCtx, cancel := context.WithTimeout(ctx, 30*time.Second)
	defer cancel()

	cmd := fmt.Sprintf("%s --data-dir %s search sentinel emails_%s %s %d",
		remoteBin, catDataDir, category, vecStr, topK)

	resp, err := e.execWithRetry(searchCtx, sandbox, cmd)
	if err != nil {
		return 1e30, 0, fmt.Errorf("search in [%s]: %w", category, err)
	}
	if resp.ExitCode != 0 {
		return 1e30, 0, fmt.Errorf("search failed in [%s] (exit %d): %s", category, resp.ExitCode, resp.Result)
	}

	return parseSearchOutput(resp.Result)
}

// ParallelSearch searches all 3 categories concurrently.
// Returns: category scores (avg distance) and match counts.
// If some but not all searches fail, returns results with warnings logged.
// If ALL searches fail, returns an error.
func (e *Engine) ParallelSearch(ctx context.Context, vecStr string, topK int) ([3]float64, [3]int, error) {
	var scores [3]float64
	var counts [3]int
	var mu sync.Mutex
	var wg sync.WaitGroup
	var errs [3]error

	for i, cat := range Categories {
		wg.Add(1)
		go func(idx int, category string) {
			defer wg.Done()
			avgDist, count, err := e.SearchCategory(ctx, category, vecStr, topK)
			mu.Lock()
			defer mu.Unlock()
			if err != nil {
				errs[idx] = err
				scores[idx] = 1e30
				counts[idx] = 0
			} else {
				scores[idx] = avgDist
				counts[idx] = count
			}
		}(i, cat)
	}
	wg.Wait()

	// Count failures
	failCount := 0
	var failedCats []string
	for i, err := range errs {
		if err != nil {
			failCount++
			failedCats = append(failedCats, Categories[i])
		}
	}

	if failCount == 3 {
		// All searches failed — return a combined error
		return scores, counts, fmt.Errorf("all searches failed: [safe] %v; [spam] %v; [attack] %v",
			errs[0], errs[1], errs[2])
	}

	if failCount > 0 {
		// Partial failure — log warnings but still return results for the categories that succeeded
		for i, err := range errs {
			if err != nil {
				log.Printf("[WARNING] Search failed for [%s]: %v (using score 1e30, will not be selected as best match)",
					Categories[i], err)
			}
		}
		return scores, counts, fmt.Errorf("search failed for %d/3 categories (%s); classification based on remaining results",
			failCount, strings.Join(failedCats, ", "))
	}

	return scores, counts, nil
}

// Destroy tears down all sandboxes (volume persists).
func (e *Engine) Destroy(ctx context.Context) {
	for cat, sandbox := range e.sandboxes {
		log.Printf("Destroying sandbox [%s]...", cat)
		sandbox.Delete(ctx)
	}
	clearState()
	log.Println("All sandboxes destroyed. Volume preserved.")
}

// Status returns sandbox IDs.
func (e *Engine) Status(ctx context.Context) map[string]string {
	result := make(map[string]string)
	for cat, sb := range e.sandboxes {
		result[cat] = sb.ID
	}
	return result
}

// GetRecordCounts gets the record count from each sandbox.
func (e *Engine) GetRecordCounts(ctx context.Context) map[string]string {
	result := make(map[string]string)
	var wg sync.WaitGroup
	var mu sync.Mutex
	for _, cat := range Categories {
		wg.Add(1)
		go func(category string) {
			defer wg.Done()
			output, err := e.Exec(ctx, category, fmt.Sprintf("table-info sentinel emails_%s", category))
			mu.Lock()
			result[category] = output
			if err != nil {
				result[category] = "error: " + err.Error()
			}
			mu.Unlock()
		}(cat)
	}
	wg.Wait()
	return result
}

// BuildIndexes builds HNSW indexes in all 3 sandboxes.
func (e *Engine) BuildIndexes(ctx context.Context) error {
	var wg sync.WaitGroup
	var mu sync.Mutex
	var firstErr error

	for _, cat := range Categories {
		wg.Add(1)
		go func(category string) {
			defer wg.Done()
			log.Printf("[%s] Building HNSW index...", category)
			_, err := e.Exec(ctx, category, fmt.Sprintf("index-build sentinel emails_%s", category))
			if err != nil {
				mu.Lock()
				if firstErr == nil {
					firstErr = fmt.Errorf("index build [%s]: %w", category, err)
				}
				mu.Unlock()
			} else {
				log.Printf("[%s] Index built", category)
			}
		}(cat)
	}
	wg.Wait()
	return firstErr
}

// ================================================================
// parseSearchOutput — parse racecar's search output
// ================================================================

// parseSearchOutput parses racecar's tabular search output and returns
// the average distance and number of results.
//
// Expected format:
//
//	Search completed in X.XXX ms
//
//	Rank    ID          Distance
//	1       42          0.1234
//	...
func parseSearchOutput(output string) (float64, int, error) {
	lines := strings.Split(strings.TrimSpace(output), "\n")
	var totalDist float64
	var count int

	for _, line := range lines {
		line = strings.TrimSpace(line)
		// Skip header and info lines
		if line == "" || strings.HasPrefix(line, "Search") || strings.HasPrefix(line, "Rank") {
			continue
		}
		// Parse: "1       42          0.123456"
		fields := strings.Fields(line)
		if len(fields) >= 3 {
			dist, err := strconv.ParseFloat(fields[2], 64)
			if err == nil {
				totalDist += dist
				count++
			}
		}
	}

	if count == 0 {
		return 1e30, 0, nil
	}
	return totalDist / float64(count), count, nil
}

// ================================================================
// escapeJSON — escape special characters for JSON string values
// ================================================================

func escapeJSON(s string) string {
	s = strings.ReplaceAll(s, `\`, `\\`)
	s = strings.ReplaceAll(s, `"`, `\"`)
	s = strings.ReplaceAll(s, "\n", `\n`)
	s = strings.ReplaceAll(s, "\r", `\r`)
	s = strings.ReplaceAll(s, "\t", `\t`)
	return s
}

// ================================================================
// Two-stage classification
// ================================================================

// ClassifyResult holds the full two-stage classification output
type ClassifyResult struct {
	// Stage 1: Vector classification
	Category   string     // "SAFE", "SPAM", "ATTACK"
	Confidence float64    // 0.0 to 1.0
	Scores     [3]float64 // avg distance per category
	Counts     [3]int     // match counts per category

	// Stage 2: Context analysis (only for ATTACK)
	HasStage2 bool
	Analysis  *HeaderAnalysis // from header_analysis.go
}

// ClassifyEmail performs Stage 1 vector classification.
// If the result is ATTACK, it also runs Stage 2 content analysis.
func (e *Engine) ClassifyEmail(ctx context.Context, subject, body string) (*ClassifyResult, error) {
	// Stage 1: Vector classification
	combined := fmt.Sprintf("subject: %s body: %s", subject, body)
	vec := vectorize(combined)
	vecStr := formatVector(vec)

	scores, counts, searchErr := e.ParallelSearch(ctx, vecStr, TopK)

	// If all searches failed, we cannot classify at all
	allFailed := true
	for _, s := range scores {
		if s < 1e30 {
			allFailed = false
			break
		}
	}
	if allFailed {
		return nil, fmt.Errorf("classification failed: %w", searchErr)
	}

	// If some searches failed, we can still classify using the successful ones.
	// The failed categories have score 1e30 so they won't be selected as best.
	if searchErr != nil {
		log.Printf("[WARNING] Partial search failure during classification: %v", searchErr)
	}

	// Determine winner (only among categories that succeeded, i.e. score < 1e30)
	best := -1
	for i := 0; i < 3; i++ {
		if scores[i] < 1e30 {
			if best == -1 || scores[i] < scores[best] {
				best = i
			}
		}
	}

	// Confidence: only compute among successful categories
	var validScores []float64
	for _, s := range scores {
		if s < 1e30 {
			validScores = append(validScores, s)
		}
	}

	confidence := 0.0
	if len(validScores) >= 2 {
		// Sort valid scores to get the two smallest
		for i := 0; i < len(validScores)-1; i++ {
			for j := i + 1; j < len(validScores); j++ {
				if validScores[j] < validScores[i] {
					validScores[i], validScores[j] = validScores[j], validScores[i]
				}
			}
		}
		if validScores[1] > 0.0001 {
			confidence = 1.0 - (validScores[0] / validScores[1])
		}
	}
	if confidence < 0 {
		confidence = 0
	}
	if confidence > 1 {
		confidence = 1
	}

	catNames := [3]string{"SAFE", "SPAM", "ATTACK"}

	result := &ClassifyResult{
		Category:   catNames[best],
		Confidence: confidence,
		Scores:     scores,
		Counts:     counts,
	}

	// Stage 2: If classified as ATTACK, run content analysis
	if best == 2 { // ATTACK
		analysis := AnalyzeFromText(subject, body)
		result.HasStage2 = true
		result.Analysis = analysis
	}

	return result, nil
}

// ClassifyRawEmail performs full two-stage classification on a raw email with headers.
func (e *Engine) ClassifyRawEmail(ctx context.Context, rawEmail string) (*ClassifyResult, error) {
	// Parse the raw email
	parsed := ParseRawEmail(rawEmail)

	// Stage 1: Vector classification on subject + body
	result, err := e.ClassifyEmail(ctx, parsed.Subject, parsed.Body)
	if err != nil {
		return nil, err
	}

	// Stage 2: Full header analysis (if ATTACK, OR if we have headers to analyze)
	// Always run Stage 2 for raw emails since we have headers
	analysis := AnalyzeEmail(parsed)
	result.HasStage2 = true
	result.Analysis = analysis

	// If Stage 2 shows high risk but Stage 1 didn't flag as attack,
	// add a note but don't override Stage 1

	return result, nil
}
