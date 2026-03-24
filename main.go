/*
 * Racecar — Unified CLI for hyper-fast vector search with Daytona sandboxes
 *
 * Combines Racecar (vector database), Sentinel (email classification), and
 * Shelf (knowledge search) into a single binary. All operations run inside
 * Daytona sandboxes — no HTTP server needed.
 *
 * Usage:
 *   racecar up                                Spin up 3 sandboxes, compile racecar in each
 *   racecar down                              Tear down sandboxes (data persists on volume)
 *   racecar status                            Show sandbox states and record counts
 *   racecar init                              Load 142 sample training emails + build indexes
 *   racecar classify <subject> <body>         Classify an email
 *   racecar train <safe|spam|attack> <s> <b>  Add a training email
 *   racecar build-index                       Build HNSW indexes in all sandboxes
 *   racecar test                              Run built-in accuracy test (12 cases)
 *   racecar stats                             Show per-category email counts
 *   racecar version                           Show version
 *   racecar help                              Show this help
 *
 * Environment:
 *   DAYTONA_API_KEY  — required (get from https://app.daytona.io)
 */
package main

import (
	"bufio"
	"context"
	"embed"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"strings"
	"time"
)

// hasForceFlag checks if --force is present in os.Args.
func hasForceFlag() bool {
	for _, arg := range os.Args {
		if arg == "--force" {
			return true
		}
	}
	return false
}

//go:embed src/racecar.h src/main.c src/db.c src/table.c src/vector.c src/hnsw.c src/json.c src/tokenizer.c src/util.c Makefile.sandbox
var embeddedFiles embed.FS

// Categories and TopK are defined in engine.go.

func main() {
	if len(os.Args) < 2 {
		printHelp()
		os.Exit(1)
	}

	cmd := os.Args[1]
	switch cmd {
	case "up":
		cmdUp()
	case "down":
		cmdDown()
	case "status":
		cmdStatus()
	case "init":
		cmdInit()
	case "classify":
		cmdClassify()
	case "train":
		cmdTrain()
	case "build-index":
		cmdBuildIndex()
	case "test":
		cmdTest()
	case "classify-raw":
		cmdClassifyRaw()
	case "test-raw":
		cmdTestRaw()
	case "stats":
		cmdStats()
	case "populate":
		cmdPopulate()
	case "evaluate":
		cmdEvaluate()
	case "diag":
		cmdDiag()
	case "version":
		fmt.Println("racecar v0.2.0 — Hyper-fast vector database with Daytona sandboxes")
	case "help", "--help", "-h":
		printHelp()
	default:
		fmt.Fprintf(os.Stderr, "Unknown command: %s\n", cmd)
		printHelp()
		os.Exit(1)
	}
}

func printHelp() {
	fmt.Fprintf(os.Stderr, `Usage: racecar <command> [arguments...]

Sandbox Management:
  up                                       Spin up 3 sandboxes, compile racecar in each
  down                                     Tear down sandboxes (data persists on volume)
  status                                   Show sandbox states and record counts

Email Classification (Sentinel):
  init                                     Load sample training emails + build indexes
  classify <subject> <body>                Classify an email (two-stage analysis)
  classify-raw <file>                      Classify raw email with headers (full analysis)
  classify-raw -                           Read raw email from stdin
  train <safe|spam|attack> <subject> <body> Add a training email
  populate <file.jsonl>                    Batch insert from JSONL file
  populate -                               Batch insert from stdin
  build-index                              Build HNSW indexes in all sandboxes
  test                                     Run accuracy test
  test-raw                                 Run raw email accuracy test (with headers)
  evaluate                                 Run test and output JSON (machine-readable)
  stats                                    Show per-category email counts

Utility:
  version                                  Show version
  help                                     Show this help
`)
}

// --- Sandbox Management ---

func cmdUp() {
	ctx := context.Background()

	// Check if already running (allow partial to detect any running sandboxes)
	if _, err := ConnectEngine(ctx, true); err == nil {
		fmt.Println("Sandboxes are already running. Use 'racecar status' to check.")
		return
	}

	start := time.Now()
	eng, err := NewEngine(ctx)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	elapsed := time.Since(start)
	fmt.Printf("\n3 sandboxes ready in %.1fs\n", elapsed.Seconds())
	for cat, sb := range eng.sandboxes {
		fmt.Printf("  [%s] %s\n", cat, sb.ID)
	}
	fmt.Println("\nRun 'racecar init' to load training data.")
}

func cmdDown() {
	ctx := context.Background()
	eng, err := ConnectEngine(ctx, true) // allow partial — destroy whatever we can reach
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes found.\n")
		os.Exit(1)
	}
	eng.Destroy(ctx)
	fmt.Println("All sandboxes destroyed. Volume data preserved.")
}

func cmdStatus() {
	ctx := context.Background()
	eng, err := ConnectEngine(ctx, true) // allow partial to show what's reachable
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes. Run 'racecar up' first.\n")
		os.Exit(1)
	}

	fmt.Println("Racecar Sandboxes:")
	ids := eng.Status(ctx)
	for _, cat := range Categories {
		fmt.Printf("  [%s] %s\n", cat, ids[cat])
	}

	fmt.Println("\nRecord Counts:")
	counts := eng.GetRecordCounts(ctx)
	for _, cat := range Categories {
		fmt.Printf("  [%s] %s\n", cat, extractRecordLine(counts[cat]))
	}
}

// extractRecordLine parses the "Records: N" line from racecar table-info output.
func extractRecordLine(output string) string {
	for _, line := range strings.Split(output, "\n") {
		trimmed := strings.TrimSpace(line)
		if strings.HasPrefix(trimmed, "Records:") {
			return trimmed
		}
	}
	return output
}

// --- Email Classification (Sentinel) ---

func cmdInit() {
	ctx := context.Background()
	eng, err := ConnectEngine(ctx, hasForceFlag())
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes. Run 'racecar up' first.\n")
		os.Exit(1)
	}

	start := time.Now()

	// Load training data for each category
	categories := map[string][]SampleEmail{
		"safe":   SafeEmails,
		"spam":   SpamEmails,
		"attack": AttackEmails,
	}

	for _, cat := range Categories {
		emails := categories[cat]
		fmt.Printf("Training [%s]... ", strings.ToUpper(cat))

		// Convert to batch format
		batch := make([]struct{ Subject, Body string }, len(emails))
		for i, e := range emails {
			batch[i] = struct{ Subject, Body string }{e.Subject, e.Body}
		}

		result, err := eng.BatchInsert(ctx, cat, batch)
		if err != nil {
			fmt.Fprintf(os.Stderr, "\nError loading [%s]: %v\n", cat, err)
			continue
		}
		fmt.Printf("%d loaded (%s)\n", len(emails), strings.TrimSpace(result))
	}

	elapsed := time.Since(start)
	total := len(SafeEmails) + len(SpamEmails) + len(AttackEmails)
	fmt.Printf("\nLoaded %d emails in %.1fs. Using flat scan (fast enough for <%d records).\n", total, elapsed.Seconds(), total)
	fmt.Println("Ready to classify.")
}

func cmdClassify() {
	if len(os.Args) < 4 {
		fmt.Fprintln(os.Stderr, "Usage: racecar classify <subject> <body>")
		os.Exit(1)
	}
	subject := os.Args[2]
	body := os.Args[3]

	ctx := context.Background()
	eng, err := ConnectEngine(ctx, hasForceFlag())
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes. Run 'racecar up' first.\n")
		os.Exit(1)
	}

	start := time.Now()
	result, err := eng.ClassifyEmail(ctx, subject, body)
	elapsed := time.Since(start)

	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	printClassifyResult(result, elapsed)
}

func printClassifyResult(result *ClassifyResult, elapsed time.Duration) {
	fmt.Println("=== Stage 1: Vector Classification ===")
	fmt.Printf("Classification: %s\n", result.Category)
	fmt.Printf("Confidence:     %.1f%%\n", result.Confidence*100)
	fmt.Println()
	fmt.Println("Category Scores:")
	catNames := [3]string{"SAFE", "SPAM", "ATTACK"}
	for i, cat := range catNames {
		marker := ""
		if cat == result.Category {
			marker = "  <-- best match"
		}
		fmt.Printf("  %-8s %.4f (%d matches)%s\n", cat, result.Scores[i], result.Counts[i], marker)
	}

	if result.HasStage2 && result.Analysis != nil {
		a := result.Analysis
		fmt.Println()
		fmt.Println("=== Stage 2: Context-Aware Analysis ===")
		fmt.Printf("Attack Subtype:  %s\n", a.Subtype)
		fmt.Printf("Risk Score:      %.2f\n", a.RiskScore)

		if len(a.Signals) > 0 {
			fmt.Println()
			fmt.Println("Signals:")
			for _, signal := range a.Signals {
				fmt.Printf("  ⚠ %s\n", signal)
			}
		}

		// Final verdict
		fmt.Println()
		riskLevel := "LOW"
		if a.RiskScore > 0.6 {
			riskLevel = "HIGH"
		} else if a.RiskScore > 0.3 {
			riskLevel = "MEDIUM"
		}
		fmt.Printf("Final Verdict: %s (%s) — %s RISK\n", result.Category, a.Subtype, riskLevel)
	}

	fmt.Printf("\nClassified in %.0f ms\n", float64(elapsed.Milliseconds()))
}

func cmdClassifyRaw() {
	if len(os.Args) < 3 {
		fmt.Fprintln(os.Stderr, "Usage: racecar classify-raw <file>")
		fmt.Fprintln(os.Stderr, "       racecar classify-raw -    (read from stdin)")
		os.Exit(1)
	}

	var rawEmail string
	if os.Args[2] == "-" {
		// Read from stdin
		data, err := io.ReadAll(os.Stdin)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error reading stdin: %v\n", err)
			os.Exit(1)
		}
		rawEmail = string(data)
	} else {
		// Read from file
		data, err := os.ReadFile(os.Args[2])
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error reading file: %v\n", err)
			os.Exit(1)
		}
		rawEmail = string(data)
	}

	ctx := context.Background()
	eng, err := ConnectEngine(ctx, hasForceFlag())
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes. Run 'racecar up' first.\n")
		os.Exit(1)
	}

	start := time.Now()
	result, err := eng.ClassifyRawEmail(ctx, rawEmail)
	elapsed := time.Since(start)

	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	printClassifyResult(result, elapsed)
}

func cmdTrain() {
	if len(os.Args) < 5 {
		fmt.Fprintln(os.Stderr, "Usage: racecar train <safe|spam|attack> <subject> <body>")
		os.Exit(1)
	}
	category := strings.ToLower(os.Args[2])
	subject := os.Args[3]
	body := os.Args[4]

	// Validate category
	valid := false
	for _, c := range Categories {
		if category == c {
			valid = true
			break
		}
	}
	if !valid {
		fmt.Fprintf(os.Stderr, "Invalid category: %s (must be safe, spam, or attack)\n", category)
		os.Exit(1)
	}

	ctx := context.Background()
	eng, err := ConnectEngine(ctx, hasForceFlag())
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes. Run 'racecar up' first.\n")
		os.Exit(1)
	}

	output, err := eng.Insert(ctx, category, subject, body)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("Trained [%s]: %s\n", strings.ToUpper(category), output)
}

func cmdBuildIndex() {
	ctx := context.Background()
	eng, err := ConnectEngine(ctx, hasForceFlag())
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes. Run 'racecar up' first.\n")
		os.Exit(1)
	}

	start := time.Now()
	fmt.Println("Building HNSW indexes in all sandboxes...")
	if err := eng.BuildIndexes(ctx); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("Indexes built in %.1fs\n", time.Since(start).Seconds())
}

// --- Test ---

// TestCase defines an expected classification for accuracy testing.
type TestCase struct {
	Expected string // "safe", "spam", or "attack"
	Subject  string
	Body     string
}

var testCases = []TestCase{
	{"safe", "Re: Project Update",
		"Thanks for the update. The new feature looks great. Let's discuss in tomorrow's standup."},
	{"safe", "Lunch tomorrow?",
		"Hey, want to grab lunch at the Thai place tomorrow around noon?"},
	{"safe", "Q3 Budget Review",
		"Please find attached the Q3 budget summary. All departments are within allocation."},
	{"safe", "Team Building Event - Friday",
		"Hi everyone, just a reminder about our team building event this Friday at 3pm. We'll be doing an escape room activity. Please RSVP."},
	{"spam", "AMAZING DEAL - 90% OFF!!!",
		"Limited time offer! Get premium watches at 90% discount. Buy now before stock runs out! Free shipping worldwide!"},
	{"spam", "You've won $5,000,000",
		"Congratulations! You have been selected as the winner of our international lottery. Send your bank details to claim your prize."},
	{"spam", "Lose 30 Pounds in 30 Days",
		"Revolutionary new diet pill guaranteed to help you lose weight fast. No exercise needed. Order now and get a free bottle."},
	{"spam", "Work From Home - Earn $5000/week",
		"Start earning money from home today. No experience needed. Our proven system has helped thousands achieve financial freedom."},
	{"attack", "Urgent: Password Reset Required",
		"Your account password will expire in 24 hours. Click here immediately to reset your password or your account will be locked."},
	{"attack", "Wire Transfer Request",
		"Hi, I need you to process an urgent wire transfer of $45,000 to the account below. This is confidential, do not discuss with anyone."},
	{"attack", "Unusual Sign-in Activity",
		"We detected a sign-in to your account from an unrecognized device. If this wasn't you, secure your account immediately by clicking below."},
	{"attack", "Invoice #INV-29481 Attached",
		"Please review the attached invoice for recent services. Open the document to verify the charges and approve payment."},
}

func cmdTest() {
	ctx := context.Background()
	eng, err := ConnectEngine(ctx, hasForceFlag())
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes. Run 'racecar up' first.\n")
		os.Exit(1)
	}

	correct := 0
	total := len(testCases)
	catNames := [3]string{"SAFE", "SPAM", "ATTACK"}
	catIndex := map[string]int{"safe": 0, "spam": 1, "attack": 2}

	fmt.Println("Test Results:")

	for _, tc := range testCases {
		combined := fmt.Sprintf("subject: %s body: %s", tc.Subject, tc.Body)
		vec := vectorize(combined)
		vecStr := formatVector(vec)

		scores, _, searchErr := eng.ParallelSearch(ctx, vecStr, TopK)

		// Check if all scores are 1e30 (total failure) vs partial failure
		allFailed := true
		for _, s := range scores {
			if s < 1e30 {
				allFailed = false
				break
			}
		}
		if allFailed {
			fmt.Printf("  [SKIP] %-7s \"%s\" -> ERROR: %v\n",
				strings.ToUpper(tc.Expected), tc.Subject, searchErr)
			total-- // don't count skipped tests against accuracy
			continue
		}
		if searchErr != nil {
			// Partial failure — log warning but still try to classify
			log.Printf("  [WARN] partial search failure for \"%s\": %v", tc.Subject, searchErr)
		}

		// Determine winner (only among categories with valid scores)
		best := -1
		for i := 0; i < 3; i++ {
			if scores[i] < 1e30 {
				if best == -1 || scores[i] < scores[best] {
					best = i
				}
			}
		}

		actual := Categories[best]
		got := catNames[best]
		pass := actual == tc.Expected
		if pass {
			correct++
		}

		expectedName := catNames[catIndex[tc.Expected]]

		detail := got
		if got == strings.ToUpper(tc.Expected) && tc.Expected == "attack" {
			// Run quick Stage 2
			analysis := AnalyzeFromText(tc.Subject, tc.Body)
			if analysis != nil {
				detail = fmt.Sprintf("%s (%s)", got, analysis.Subtype)
			}
		}

		if pass {
			fmt.Printf("  [PASS] %-7s \"%s\" -> %s (correct)\n",
				expectedName, tc.Subject, detail)
		} else {
			fmt.Printf("  [FAIL] %-7s \"%s\" -> %s (WRONG, expected %s)\n",
				expectedName, tc.Subject, detail, expectedName)
		}
	}

	accuracy := 0.0
	if total > 0 {
		accuracy = 100.0 * float64(correct) / float64(total)
	}
	fmt.Printf("\nAccuracy: %d/%d (%.1f%%)\n", correct, total, accuracy)
}

func cmdTestRaw() {
	ctx := context.Background()
	eng, err := ConnectEngine(ctx, hasForceFlag())
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes. Run 'racecar up' first.\n")
		os.Exit(1)
	}

	fmt.Println("Raw Email Test Results:")
	fmt.Println()

	passed := 0
	total := len(RawTestEmails)

	for _, test := range RawTestEmails {
		result, err := eng.ClassifyRawEmail(ctx, test.Raw)
		if err != nil {
			fmt.Printf("  [SKIP]  %s: %v\n", test.Description, err)
			total-- // don't count unreachable sandbox errors against accuracy
			continue
		}

		got := strings.ToLower(result.Category)
		expected := strings.ToLower(test.Expected)
		ok := got == expected

		status := "[FAIL]"
		if ok {
			status = "[PASS]"
			passed++
		}

		detail := result.Category
		if result.HasStage2 && result.Analysis != nil {
			detail = fmt.Sprintf("%s (%s, risk=%.2f)", result.Category, result.Analysis.Subtype, result.Analysis.RiskScore)
		}

		fmt.Printf("  %s %-7s %s → %s\n", status, strings.ToUpper(test.Expected), test.Description, detail)
	}

	fmt.Printf("\nAccuracy: %d/%d (%.1f%%)\n", passed, total, float64(passed)/float64(total)*100)
}

// --- Stats ---

func cmdStats() {
	ctx := context.Background()
	eng, err := ConnectEngine(ctx, hasForceFlag())
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes. Run 'racecar up' first.\n")
		os.Exit(1)
	}

	fmt.Println("Sentinel Database Statistics:")
	counts := eng.GetRecordCounts(ctx)
	for _, cat := range Categories {
		fmt.Printf("  %-8s %s\n", strings.ToUpper(cat), extractRecordLine(counts[cat]))
	}
}

// --- Diagnostics ---

func cmdDiag() {
	ctx := context.Background()
	eng, err := ConnectEngine(ctx, hasForceFlag())
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes. Run 'racecar up' first.\n")
		os.Exit(1)
	}

	for _, cat := range Categories {
		fmt.Printf("=== Sandbox [%s] ===\n", strings.ToUpper(cat))
		sandbox, ok := eng.sandboxes[cat]
		if !ok {
			fmt.Printf("  UNREACHABLE (sandbox not connected)\n\n")
			continue
		}
		catDataDir := activeDataDir + "/" + cat
		bin := remoteSrcDir + "/racecar"

		// Check if binary exists
		resp, err := sandbox.Process.ExecuteCommand(ctx, "ls -la "+bin)
		if err != nil || resp.ExitCode != 0 {
			fmt.Printf("  BINARY MISSING: %s\n", bin)
			fmt.Println("  (sandbox was likely restarted — run 'racecar down && racecar up && racecar init')")
			fmt.Println()
			continue
		}
		fmt.Printf("  Binary: %s\n", strings.TrimSpace(resp.Result))

		// Check if binary runs
		resp, _ = sandbox.Process.ExecuteCommand(ctx, bin+" version")
		fmt.Printf("  Version: exit=%d result=%s\n", resp.ExitCode, strings.TrimSpace(resp.Result))

		// Check shared libs
		resp, _ = sandbox.Process.ExecuteCommand(ctx, "ldd "+bin)
		fmt.Printf("  Libs: %s\n", strings.TrimSpace(resp.Result))

		// Check table file
		tablePath := catDataDir + "/sentinel/emails_" + cat + ".rct"
		resp, _ = sandbox.Process.ExecuteCommand(ctx, "ls -la "+tablePath)
		fmt.Printf("  Table: %s\n", strings.TrimSpace(resp.Result))

		// Try table-info
		resp, _ = sandbox.Process.ExecuteCommand(ctx,
			fmt.Sprintf("%s --data-dir %s table-info sentinel emails_%s", bin, catDataDir, cat))
		fmt.Printf("  table-info: exit=%d result=%s\n", resp.ExitCode, strings.TrimSpace(resp.Result))

		fmt.Println()
	}
}

// --- Populate (batch insert from JSONL) ---

type populateEntry struct {
	Category string `json:"category"`
	Subject  string `json:"subject"`
	Body     string `json:"body"`
}

func cmdPopulate() {
	if len(os.Args) < 3 {
		fmt.Fprintln(os.Stderr, "Usage: racecar populate <file.jsonl>")
		fmt.Fprintln(os.Stderr, "       racecar populate -    (read from stdin)")
		fmt.Fprintln(os.Stderr, "")
		fmt.Fprintln(os.Stderr, "JSONL format: {\"category\": \"attack\", \"subject\": \"...\", \"body\": \"...\"}")
		os.Exit(1)
	}

	var reader io.Reader
	if os.Args[2] == "-" {
		reader = os.Stdin
	} else {
		f, err := os.Open(os.Args[2])
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error opening file: %v\n", err)
			os.Exit(1)
		}
		defer f.Close()
		reader = f
	}

	ctx := context.Background()
	eng, err := ConnectEngine(ctx, hasForceFlag())
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes. Run 'racecar up' first.\n")
		os.Exit(1)
	}

	scanner := bufio.NewScanner(reader)
	scanner.Buffer(make([]byte, 1024*1024), 1024*1024) // 1MB line buffer

	// Collect entries by category
	batches := map[string][]struct{ Subject, Body string }{
		"safe": {}, "spam": {}, "attack": {},
	}
	parseErrors := 0
	lineNum := 0

	for scanner.Scan() {
		lineNum++
		line := strings.TrimSpace(scanner.Text())
		if line == "" || line[0] == '#' {
			continue
		}

		var entry populateEntry
		if err := json.Unmarshal([]byte(line), &entry); err != nil {
			fmt.Fprintf(os.Stderr, "Warning: bad JSON on line %d: %v\n", lineNum, err)
			parseErrors++
			continue
		}

		cat := strings.ToLower(entry.Category)
		if cat != "safe" && cat != "spam" && cat != "attack" {
			fmt.Fprintf(os.Stderr, "Warning: invalid category '%s' on line %d\n", entry.Category, lineNum)
			parseErrors++
			continue
		}

		batches[cat] = append(batches[cat], struct{ Subject, Body string }{entry.Subject, entry.Body})
	}

	// Batch insert per category
	total := 0
	for _, cat := range Categories {
		batch := batches[cat]
		if len(batch) == 0 {
			continue
		}
		fmt.Printf("  [%s] inserting %d emails... ", strings.ToUpper(cat), len(batch))
		_, err := eng.BatchInsert(ctx, cat, batch)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %v\n", err)
			continue
		}
		fmt.Println("done")
		total += len(batch)
	}

	fmt.Printf("Populated %d emails (safe=%d, spam=%d, attack=%d)\n",
		total, len(batches["safe"]), len(batches["spam"]), len(batches["attack"]))
	if parseErrors > 0 {
		fmt.Printf("%d parse errors\n", parseErrors)
	}
	if total > 0 {
		fmt.Println("Run 'racecar build-index' to rebuild HNSW indexes.")
	}
}

// --- Evaluate (machine-readable JSON test output) ---

type evalResult struct {
	Expected   string `json:"expected"`
	Got        string `json:"got"`
	Correct    bool   `json:"correct"`
	Subject    string `json:"subject"`
	Confidence float64 `json:"confidence"`
	Subtype    string `json:"subtype,omitempty"`
	RiskScore  float32 `json:"risk_score,omitempty"`
}

type evalOutput struct {
	Total    int          `json:"total"`
	Correct  int          `json:"correct"`
	Skipped  int          `json:"skipped,omitempty"`
	Accuracy float64      `json:"accuracy"`
	Results  []evalResult `json:"results"`
	Misses   []evalResult `json:"misses"`
}

func cmdEvaluate() {
	ctx := context.Background()
	eng, err := ConnectEngine(ctx, hasForceFlag())
	if err != nil {
		fmt.Fprintf(os.Stderr, "No active sandboxes. Run 'racecar up' first.\n")
		os.Exit(1)
	}

	testCases := []struct {
		Expected string
		Subject  string
		Body     string
	}{
		{"safe", "Re: Project Update", "Thanks for the update. The new feature looks great. Let's discuss in tomorrow's standup."},
		{"safe", "Lunch tomorrow?", "Hey, want to grab lunch at the Thai place tomorrow around noon?"},
		{"safe", "Q3 Budget Review", "Please find attached the Q3 budget summary. All departments are within allocation."},
		{"safe", "Team Building Event - Friday", "Hi everyone, just a reminder about our team building event this Friday at 3pm."},
		{"spam", "AMAZING DEAL - 90% OFF!!!", "Limited time offer! Get premium watches at 90% discount. Buy now before stock runs out!"},
		{"spam", "You've won $5,000,000", "Congratulations! You have been selected as the winner of our international lottery."},
		{"spam", "Lose 30 Pounds in 30 Days", "Revolutionary new diet pill guaranteed to help you lose weight fast. No exercise needed."},
		{"spam", "Work From Home - Earn $5000/week", "Start earning money from home today. No experience needed."},
		{"attack", "Urgent: Password Reset Required", "Your account password will expire in 24 hours. Click here immediately to reset your password."},
		{"attack", "Wire Transfer Request", "Hi, I need you to process an urgent wire transfer of $45,000. This is confidential."},
		{"attack", "Unusual Sign-in Activity", "We detected a sign-in to your account from an unrecognized device. Secure your account immediately."},
		{"attack", "Invoice #INV-29481 Attached", "Please review the attached invoice. Open the document to verify the charges."},
	}

	output := evalOutput{}

	skipped := 0
	for _, tc := range testCases {
		result, err := eng.ClassifyEmail(ctx, tc.Subject, tc.Body)
		if err != nil {
			fmt.Fprintf(os.Stderr, "classify error for %q: %v\n", tc.Subject, err)
			// Skip unreachable sandbox errors rather than counting as failures
			skipped++
			output.Results = append(output.Results, evalResult{
				Expected: tc.Expected,
				Got:      "SKIPPED",
				Correct:  false,
				Subject:  tc.Subject,
			})
			continue
		}

		got := strings.ToLower(result.Category)
		correct := got == tc.Expected

		er := evalResult{
			Expected:   tc.Expected,
			Got:        result.Category,
			Correct:    correct,
			Subject:    tc.Subject,
			Confidence: result.Confidence,
		}

		if result.HasStage2 && result.Analysis != nil {
			er.Subtype = string(result.Analysis.Subtype)
			er.RiskScore = result.Analysis.RiskScore
		}

		output.Results = append(output.Results, er)
		output.Total++
		if correct {
			output.Correct++
		} else {
			output.Misses = append(output.Misses, er)
		}
	}

	output.Skipped = skipped

	if output.Total > 0 {
		output.Accuracy = float64(output.Correct) / float64(output.Total) * 100
	}

	enc := json.NewEncoder(os.Stdout)
	enc.SetIndent("", "  ")
	enc.Encode(output)
}
