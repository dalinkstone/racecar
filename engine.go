package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/daytonaio/daytona/libs/sdk-go/pkg/daytona"
	"github.com/daytonaio/daytona/libs/sdk-go/pkg/types"
)

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
	"Makefile",
}

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

func ConnectEngine(ctx context.Context) (*Engine, error) {
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
	for cat, id := range state.Sandboxes {
		sandbox, err := client.Get(ctx, id)
		if err != nil {
			return nil, fmt.Errorf("reconnect to sandbox [%s] (%s): %w", cat, id, err)
		}
		eng.sandboxes[cat] = sandbox
	}

	return eng, nil
}

// ================================================================
// setupSandbox — per-sandbox initialization
// ================================================================

func (e *Engine) setupSandbox(ctx context.Context, sandbox *daytona.Sandbox, category string) error {
	// Install gcc and make if needed
	resp, err := sandbox.Process.ExecuteCommand(ctx, "which gcc && which make")
	if err != nil {
		return fmt.Errorf("check build tools: %w", err)
	}
	if resp.ExitCode != 0 {
		log.Printf("[%s] Installing build tools...", category)
		resp, err = sandbox.Process.ExecuteCommand(ctx, "apt-get update -qq && apt-get install -y -qq gcc make >/dev/null 2>&1")
		if err != nil || resp.ExitCode != 0 {
			return fmt.Errorf("install build tools: exit=%d result=%s", resp.ExitCode, resp.Result)
		}
	}

	// Create directories
	catDataDir := remoteDataDir + "/" + category
	resp, err = sandbox.Process.ExecuteCommand(ctx, fmt.Sprintf("mkdir -p %s %s/src", remoteSrcDir, remoteSrcDir))
	if err != nil || resp.ExitCode != 0 {
		return fmt.Errorf("mkdir: %w", err)
	}
	resp, err = sandbox.Process.ExecuteCommand(ctx, "mkdir -p "+catDataDir)
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

	// Compile
	log.Printf("[%s] Compiling racecar...", category)
	resp, err = sandbox.Process.ExecuteCommand(ctx, "cd "+remoteSrcDir+" && make clean && make 2>&1")
	if err != nil {
		return fmt.Errorf("compile: %w", err)
	}
	if resp.ExitCode != 0 {
		return fmt.Errorf("compilation failed:\n%s", resp.Result)
	}

	// Create the database and table for this category
	dbName := "sentinel"
	tableName := "emails_" + category

	// Create db (ignore "exists" errors)
	sandbox.Process.ExecuteCommand(ctx, fmt.Sprintf("RACECAR_DATA=%s %s db-create %s", catDataDir, remoteBin, dbName))

	// Create table (ignore "exists" errors) — 256-dim cosine
	sandbox.Process.ExecuteCommand(ctx, fmt.Sprintf("RACECAR_DATA=%s %s table-create %s %s 256 cosine", catDataDir, remoteBin, dbName, tableName))

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
	catDataDir := remoteDataDir + "/" + category
	cmd := fmt.Sprintf("RACECAR_DATA=%s %s %s", catDataDir, remoteBin, args)
	resp, err := sandbox.Process.ExecuteCommand(ctx, cmd)
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
	combined := fmt.Sprintf("subject: %s body: %s", subject, body)
	vec := vectorize(combined)
	vecStr := formatVector(vec)

	meta := fmt.Sprintf(`{"subject":"%s"}`, escapeJSON(subject))
	args := fmt.Sprintf(`insert sentinel emails_%s "%s" '%s'`, category, vecStr, meta)

	return e.Exec(ctx, category, args)
}

// SearchCategory searches a single category and returns avg distance + match count.
func (e *Engine) SearchCategory(ctx context.Context, category string, vecStr string, topK int) (float64, int, error) {
	args := fmt.Sprintf("search sentinel emails_%s \"%s\" %d", category, vecStr, topK)
	output, err := e.Exec(ctx, category, args)
	if err != nil {
		return 1e30, 0, err
	}

	return parseSearchOutput(output)
}

// ParallelSearch searches all 3 categories concurrently.
// Returns: category scores (avg distance) and match counts.
func (e *Engine) ParallelSearch(ctx context.Context, vecStr string, topK int) ([3]float64, [3]int, error) {
	var scores [3]float64
	var counts [3]int
	var mu sync.Mutex
	var wg sync.WaitGroup
	var firstErr error

	for i, cat := range Categories {
		wg.Add(1)
		go func(idx int, category string) {
			defer wg.Done()
			avgDist, count, err := e.SearchCategory(ctx, category, vecStr, topK)
			mu.Lock()
			defer mu.Unlock()
			if err != nil {
				if firstErr == nil {
					firstErr = err
				}
				scores[idx] = 1e30
				counts[idx] = 0
			} else {
				scores[idx] = avgDist
				counts[idx] = count
			}
		}(i, cat)
	}
	wg.Wait()

	return scores, counts, firstErr
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
		// Parse: "1       42          0.1234"
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

	scores, counts, err := e.ParallelSearch(ctx, vecStr, TopK)
	if err != nil {
		return nil, err
	}

	// Determine winner
	best := 0
	for i := 1; i < 3; i++ {
		if scores[i] < scores[best] {
			best = i
		}
	}

	// Confidence
	sorted := make([]float64, 3)
	copy(sorted, scores[:])
	for i := 0; i < 2; i++ {
		for j := i + 1; j < 3; j++ {
			if sorted[j] < sorted[i] {
				sorted[i], sorted[j] = sorted[j], sorted[i]
			}
		}
	}
	confidence := 0.0
	if sorted[1] > 0.0001 {
		confidence = 1.0 - (sorted[0] / sorted[1])
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
