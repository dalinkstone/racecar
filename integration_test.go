package main

import (
	"fmt"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

func TestFullClassificationLocally(t *testing.T) {
	// Check racecar-local exists
	if _, err := os.Stat("./racecar-local"); os.IsNotExist(err) {
		t.Skip("racecar-local not built — run: gcc -O2 -Wall -std=gnu11 -o racecar-local src/*.c -lm")
	}

	// Create temp directories for 3 "sandboxes"
	tmpDir, err := os.MkdirTemp("", "racecar-integ-*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpDir)

	categories := []string{"safe", "spam", "attack"}
	trainingData := map[string][]struct{ Subject, Body string }{
		"safe":   toSubjectBody(SafeEmails),
		"spam":   toSubjectBody(SpamEmails),
		"attack": toSubjectBody(AttackEmails),
	}

	// Write .rct files for each category
	for _, cat := range categories {
		dbDir := filepath.Join(tmpDir, cat, "sentinel")
		os.MkdirAll(dbDir, 0755)

		emails := trainingData[cat]
		data := buildRCTFile(emails)

		tablePath := filepath.Join(dbDir, "emails_"+cat+".rct")
		if err := os.WriteFile(tablePath, data, 0644); err != nil {
			t.Fatalf("write %s: %v", tablePath, err)
		}

		// Verify with table-info
		cmd := exec.Command("./racecar-local", "table-info", "sentinel", "emails_"+cat)
		cmd.Env = append(os.Environ(), "RACECAR_DATA="+filepath.Join(tmpDir, cat))
		out, err := cmd.CombinedOutput()
		if err != nil {
			t.Fatalf("[%s] table-info failed: %v\n%s", cat, err, out)
		}
		t.Logf("[%s] table-info: %s", cat, strings.TrimSpace(string(out)))
	}

	// Test classification on 12 test cases
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

	passed := 0
	for _, tc := range testCases {
		// Vectorize the query
		combined := fmt.Sprintf("subject: %s body: %s", tc.Subject, tc.Body)
		queryVec := vectorize(combined)
		vecStr := formatVector(queryVec)

		// Search all 3 categories
		var scores [3]float64
		for i, cat := range categories {
			cmd := exec.Command("./racecar-local", "search", "sentinel", "emails_"+cat, vecStr, "5")
			cmd.Env = append(os.Environ(), "RACECAR_DATA="+filepath.Join(tmpDir, cat))
			out, err := cmd.CombinedOutput()
			if err != nil {
				t.Logf("[%s] search failed for %q: %v\n%s", cat, tc.Subject, err, string(out))
				scores[i] = 1e30
				continue
			}
			scores[i] = parseAvgDist(string(out))
		}

		// Pick winner (lowest score)
		best := 0
		for i := 1; i < 3; i++ {
			if scores[i] < scores[best] {
				best = i
			}
		}

		got := categories[best]
		status := "FAIL"
		if got == tc.Expected {
			status = "PASS"
			passed++
		}
		t.Logf("  [%s] expected=%s got=%s scores=[%.4f, %.4f, %.4f]  %q",
			status, tc.Expected, got, scores[0], scores[1], scores[2], tc.Subject)
	}

	accuracy := float64(passed) / float64(len(testCases)) * 100
	t.Logf("\nAccuracy: %d/%d (%.1f%%)", passed, len(testCases), accuracy)
	if accuracy < 80 {
		t.Errorf("accuracy too low: %.1f%% (need >80%%)", accuracy)
	}
}

func toSubjectBody(emails []SampleEmail) []struct{ Subject, Body string } {
	result := make([]struct{ Subject, Body string }, len(emails))
	for i, e := range emails {
		result[i] = struct{ Subject, Body string }{e.Subject, e.Body}
	}
	return result
}

func buildRCTFile(emails []struct{ Subject, Body string }) []byte {
	const (
		magicVal    = 0x52435242
		versionVal  = 1
		dimensions  = 256
		metricVal   = 0
		metaSizeVal = 1024
		headerSize  = 64
		recordSize  = 16 + dimensions*4 + metaSizeVal
	)

	count := uint64(len(emails))
	capacity := count
	if capacity < 1024 {
		capacity = 1024
	}

	fileSize := headerSize + int(count)*recordSize
	data := make([]byte, fileSize)

	le := func(b []byte, v uint32) {
		b[0] = byte(v); b[1] = byte(v >> 8); b[2] = byte(v >> 16); b[3] = byte(v >> 24)
	}
	le64 := func(b []byte, v uint64) {
		b[0] = byte(v); b[1] = byte(v >> 8); b[2] = byte(v >> 16); b[3] = byte(v >> 24)
		b[4] = byte(v >> 32); b[5] = byte(v >> 40); b[6] = byte(v >> 48); b[7] = byte(v >> 56)
	}

	le(data[0:4], magicVal)
	le(data[4:8], versionVal)
	le(data[8:12], dimensions)
	le(data[12:16], metricVal)
	le(data[16:20], metaSizeVal)
	le64(data[24:32], count)
	le64(data[32:40], count+1)
	le64(data[40:48], capacity)

	for i, email := range emails {
		combined := fmt.Sprintf("subject: %s body: %s", email.Subject, email.Body)
		vec := vectorize(combined)
		meta := fmt.Sprintf(`{"subject":"%s"}`, escapeJSON(email.Subject))
		if len(meta) >= metaSizeVal {
			meta = meta[:metaSizeVal-1]
		}

		offset := headerSize + i*recordSize
		rec := data[offset : offset+recordSize]
		le64(rec[0:8], uint64(i+1))
		le(rec[8:12], 1)
		le(rec[12:16], uint32(len(meta)))
		for j, v := range vec {
			bits := math.Float32bits(v)
			off := 16 + j*4
			le(rec[off:off+4], bits)
		}
		copy(rec[16+dimensions*4:], []byte(meta))
	}

	return data
}

func parseAvgDist(output string) float64 {
	lines := strings.Split(strings.TrimSpace(output), "\n")
	var total float64
	var count int
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "Search") || strings.HasPrefix(line, "Rank") {
			continue
		}
		fields := strings.Fields(line)
		if len(fields) >= 3 {
			if d, err := strconv.ParseFloat(fields[2], 64); err == nil {
				total += d
				count++
			}
		}
	}
	if count == 0 {
		return 1e30
	}
	return total / float64(count)
}
