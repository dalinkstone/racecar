package main

import (
	"fmt"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestRCTFileFormat(t *testing.T) {
	// Create a temp directory simulating the racecar data structure
	tmpDir, err := os.MkdirTemp("", "racecar-test-*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpDir)

	dbDir := filepath.Join(tmpDir, "testdb")
	os.MkdirAll(dbDir, 0755)

	// Use the BatchInsert logic to write a .rct file
	// We need to replicate the binary format
	const (
		magic      = 0x52435242
		version    = 1
		dimensions = 256
		metric     = 0
		metaSize   = 1024
		headerSize = 64
		recordSize = 16 + dimensions*4 + metaSize
	)

	// Create 3 test records
	emails := []struct{ Subject, Body string }{
		{"Test email 1", "This is a safe email about a meeting"},
		{"WINNER PRIZE", "Congratulations you won the lottery"},
		{"Password reset", "Click here to reset your password immediately"},
	}

	count := uint64(len(emails))
	capacity := uint64(1024)

	fileSize := headerSize + int(count)*recordSize
	data := make([]byte, fileSize)

	le := func(b []byte, v uint32) {
		b[0] = byte(v)
		b[1] = byte(v >> 8)
		b[2] = byte(v >> 16)
		b[3] = byte(v >> 24)
	}
	le64 := func(b []byte, v uint64) {
		b[0] = byte(v)
		b[1] = byte(v >> 8)
		b[2] = byte(v >> 16)
		b[3] = byte(v >> 24)
		b[4] = byte(v >> 32)
		b[5] = byte(v >> 40)
		b[6] = byte(v >> 48)
		b[7] = byte(v >> 56)
	}

	// Header
	le(data[0:4], magic)
	le(data[4:8], version)
	le(data[8:12], dimensions)
	le(data[12:16], metric)
	le(data[16:20], metaSize)
	le(data[20:24], 0)
	le64(data[24:32], count)
	le64(data[32:40], count+1)
	le64(data[40:48], capacity)

	// Records
	for i, email := range emails {
		combined := fmt.Sprintf("subject: %s body: %s", email.Subject, email.Body)
		vec := vectorize(combined)
		meta := fmt.Sprintf(`{"subject":"%s"}`, email.Subject)
		if len(meta) >= metaSize {
			meta = meta[:metaSize-1]
		}

		offset := headerSize + i*recordSize
		rec := data[offset : offset+recordSize]

		le64(rec[0:8], uint64(i+1))
		le(rec[8:12], 1) // RC_RECORD_ACTIVE
		le(rec[12:16], uint32(len(meta)))

		for j, v := range vec {
			bits := math.Float32bits(v)
			off := 16 + j*4
			le(rec[off:off+4], bits)
		}
		copy(rec[16+dimensions*4:], []byte(meta))
	}

	// Write .rct file
	tablePath := filepath.Join(dbDir, "test.rct")
	if err := os.WriteFile(tablePath, data, 0644); err != nil {
		t.Fatal(err)
	}

	fmt.Printf("Wrote %d bytes to %s\n", len(data), tablePath)

	// Now test with racecar-local
	racecarBin, err := filepath.Abs("./racecar-local")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(racecarBin); os.IsNotExist(err) {
		t.Skip("racecar-local not built")
	}

	// Test table-info
	cmd := exec.Command(racecarBin, "table-info", "testdb", "test")
	cmd.Env = append(os.Environ(), "RACECAR_DATA="+tmpDir)
	out, err := cmd.CombinedOutput()
	fmt.Printf("table-info output:\n%s\n", string(out))
	if err != nil {
		t.Errorf("table-info failed: %v\nOutput: %s", err, string(out))
	}

	// Test search with a zero vector
	zeros := ""
	for i := 0; i < 256; i++ {
		if i > 0 {
			zeros += ","
		}
		zeros += "0"
	}
	cmd = exec.Command(racecarBin, "search", "testdb", "test", zeros, "3")
	cmd.Env = append(os.Environ(), "RACECAR_DATA="+tmpDir)
	out, err = cmd.CombinedOutput()
	fmt.Printf("search output (zero vector):\n%s\n", string(out))
	if err != nil {
		t.Errorf("search failed: %v\nOutput: %s", err, string(out))
	}

	// Test search with an actual vector
	queryVec := vectorize("subject: Password reset body: Click here to reset your password immediately")
	vecStr := formatVector(queryVec)
	cmd = exec.Command(racecarBin, "search", "testdb", "test", vecStr, "3")
	cmd.Env = append(os.Environ(), "RACECAR_DATA="+tmpDir)
	out, err = cmd.CombinedOutput()
	fmt.Printf("search with query vector:\n%s\n", string(out))
	if err != nil {
		t.Errorf("search with query failed: %v\nOutput: %s", err, string(out))
	}
}
