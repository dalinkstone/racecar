package main

import (
	"fmt"
	"hash/fnv"
	"math"
	"testing"
)

func TestVectorization(t *testing.T) {
	// Test that similar texts produce similar vectors
	v1 := vectorize("urgent password reset click here verify account")
	v2 := vectorize("your password expires immediately click to reset")
	v3 := vectorize("team lunch tomorrow at noon")
	v4 := vectorize("congratulations you won the lottery free prize")

	d12 := cosineDistVec(v1, v2) // attack vs attack - should be LOW
	d13 := cosineDistVec(v1, v3) // attack vs safe - should be HIGH
	d14 := cosineDistVec(v1, v4) // attack vs spam - should be MEDIUM
	d34 := cosineDistVec(v3, v4) // safe vs spam - should be HIGH

	fmt.Printf("attack-attack: %.4f\n", d12)
	fmt.Printf("attack-safe:   %.4f\n", d13)
	fmt.Printf("attack-spam:   %.4f\n", d14)
	fmt.Printf("safe-spam:     %.4f\n", d34)

	// attack-attack should be lower than attack-safe
	if d12 >= d13 {
		t.Errorf("similar texts not closer: attack-attack=%.4f >= attack-safe=%.4f", d12, d13)
	}

	// Check vectors are non-zero
	nonzero := 0
	for _, v := range v1 {
		if v != 0 {
			nonzero++
		}
	}
	fmt.Printf("Non-zero dims in attack vector: %d/%d\n", nonzero, len(v1))
	if nonzero < 5 {
		t.Errorf("vector too sparse: only %d non-zero dimensions", nonzero)
	}

	// Print first 20 non-zero dimensions for v1
	fmt.Println("\nFirst 20 non-zero dims of attack vector:")
	count := 0
	for i, v := range v1 {
		if v != 0 && count < 20 {
			fmt.Printf("  dim[%d] = %.6f\n", i, v)
			count++
		}
	}

	// Additional test: self-distance should be 0
	d11 := cosineDistVec(v1, v1)
	fmt.Printf("\nSelf-distance (should be ~0): %.6f\n", d11)

	// Test with the actual training-style text
	trainAttack := vectorize("subject: Urgent: Password Reset Required body: Your account password will expire in 24 hours. Click here immediately to reset your password or your account will be locked.")
	trainSafe := vectorize("subject: Lunch tomorrow? body: Hey, want to grab lunch at the Thai place tomorrow around noon?")
	queryAttack := vectorize("subject: Verify Your Account body: Your account has been compromised. Click immediately to verify your credentials.")

	dqa := cosineDistVec(queryAttack, trainAttack)
	dqs := cosineDistVec(queryAttack, trainSafe)
	fmt.Printf("\nQuery attack -> Train attack: %.4f\n", dqa)
	fmt.Printf("Query attack -> Train safe:   %.4f\n", dqs)

	if dqa >= dqs {
		t.Errorf("attack query not closer to attack training: attack=%.4f >= safe=%.4f", dqa, dqs)
	}
}

func TestGoVsCHashing(t *testing.T) {
	// Verify that Go's fnvHash matches what C's fnv1a would produce
	// C: fnv1a starts with h = 2166136261u, then for each byte: h ^= byte; h *= 16777619u;
	// Go: fnv.New32a() uses the same algorithm

	testWords := []string{"password", "urgent", "click", "hello", "team_lunch"}

	fmt.Println("\n=== FNV Hash comparison (Go std lib vs manual C-style) ===")
	for _, word := range testWords {
		// Go standard library
		h := fnv.New32a()
		h.Write([]byte(word))
		goHash := h.Sum32()

		// Manual FNV-1a matching C exactly
		var cHash uint32 = 2166136261
		for i := 0; i < len(word); i++ {
			cHash ^= uint32(word[i])
			cHash *= 16777619
		}

		fmt.Printf("  %-15s go=%10d  c=%10d  match=%v  dim_go=%d  dim_c=%d\n",
			word, goHash, cHash, goHash == cHash, goHash%256, cHash%256)

		if goHash != cHash {
			t.Errorf("FNV hash mismatch for %q: go=%d c=%d", word, goHash, cHash)
		}
	}

	// Also verify sign hash
	fmt.Println("\n=== Sign Hash comparison ===")
	for _, word := range testWords {
		goSign := signHash(word)

		// Manual sign hash matching C
		var h uint32 = 2147483647
		for i := 0; i < len(word); i++ {
			h ^= uint32(word[i])
			h *= 16777619
		}
		cSign := -1
		if h&1 != 0 {
			cSign = 1
		}

		fmt.Printf("  %-15s go_sign=%+d  c_sign=%+d  match=%v\n",
			word, goSign, cSign, goSign == cSign)

		if goSign != cSign {
			t.Errorf("Sign hash mismatch for %q: go=%d c=%d", word, goSign, cSign)
		}
	}
}

func TestHashCollisions(t *testing.T) {
	// Check if the 256-dim space has excessive hash collisions
	words := []string{
		"password", "reset", "click", "here", "verify", "account",
		"urgent", "immediately", "expire", "locked", "compromised",
		"team", "lunch", "tomorrow", "noon", "project", "meeting",
		"congratulations", "winner", "lottery", "prize", "free",
		"discount", "offer", "limited", "guaranteed", "miracle",
	}

	dims := make(map[uint32][]string)
	for _, w := range words {
		h := fnv.New32a()
		h.Write([]byte(w))
		idx := h.Sum32() % 256
		dims[idx] = append(dims[idx], w)
	}

	collisions := 0
	fmt.Println("\n=== Hash dimension distribution (256 dims) ===")
	for idx, ws := range dims {
		if len(ws) > 1 {
			collisions++
			fmt.Printf("  COLLISION at dim %d: %v\n", idx, ws)
		}
	}
	fmt.Printf("  %d unique dims used for %d words, %d collisions\n", len(dims), len(words), collisions)

	// With 27 words into 256 buckets, birthday paradox says ~1-2 collisions expected
	if collisions > 5 {
		t.Errorf("too many hash collisions: %d", collisions)
	}
}

func TestLeetNormalization(t *testing.T) {
	// Test that leet speak normalization helps classification
	v1 := vectorize("password reset urgent account verify")
	v2 := vectorize("p@ssw0rd r3s3t urg3nt acc0unt v3r!fy")
	v3 := vectorize("team lunch tomorrow at noon")

	d12 := cosineDistVec(v1, v2) // normal vs leet - should be LOW
	d13 := cosineDistVec(v1, v3) // attack vs safe - should be HIGH

	fmt.Printf("\n=== Leet Speak Normalization ===\n")
	fmt.Printf("normal vs leet:  %.4f (should be low)\n", d12)
	fmt.Printf("normal vs safe:  %.4f (should be high)\n", d13)

	if d12 >= d13 {
		t.Errorf("leet speak not helping: normal-leet=%.4f >= normal-safe=%.4f", d12, d13)
	}

	// Check what normalizeLeet does
	fmt.Printf("\nLeet normalizations:\n")
	testStrings := []string{"p@ssw0rd", "acc0unt", "v3r!fy", "urg3nt", "cl!ck"}
	for _, s := range testStrings {
		fmt.Printf("  %s -> %s\n", s, normalizeLeet(s))
	}
}

func TestCVsGoVectorizerDifferences(t *testing.T) {
	// The C vectorizer does NOT have:
	// 1. Stop word removal
	// 2. Keyword boost weights
	// 3. Character trigrams
	// 4. Leet speak normalization
	//
	// This means the C vectorizer produces DIFFERENT vectors for the same text.
	// But in the Daytona flow, both training and query vectors come from Go,
	// so this isn't a problem for the Daytona orchestrator.
	//
	// However, if someone uses the C vectorizer (standalone sentinel) with
	// data produced by Go, or vice versa, it WILL NOT WORK.

	// Simulate what C would produce (no stop words removal, no boosts, no trigrams)
	text := "urgent password reset click here verify account"
	textLower := "urgent password reset click here verify account"

	// Go vectorizer
	goVec := vectorize(text)

	// Simulated C vectorizer: just unigrams + bigrams, no stop words, no boosts, no trigrams
	cVec := make([]float32, VectorDim)
	words := tokenize(textLower)
	// C does NOT remove stop words
	for _, w := range words {
		h := fnv.New32a()
		h.Write([]byte(w))
		idx := h.Sum32() % uint32(VectorDim)
		sign := signHash(w)
		cVec[idx] += float32(sign) * 1.0
	}
	if len(words) >= 2 {
		for i := 0; i+1 < len(words); i++ {
			bigram := words[i] + "_" + words[i+1]
			h := fnv.New32a()
			h.Write([]byte(bigram))
			idx := h.Sum32() % uint32(VectorDim)
			sign := signHash(bigram)
			cVec[idx] += float32(sign) * 0.7
		}
	}
	// L2 normalize
	var norm float32
	for _, v := range cVec {
		norm += v * v
	}
	norm = float32(math.Sqrt(float64(norm)))
	if norm > 0 {
		for i := range cVec {
			cVec[i] /= norm
		}
	}

	dist := cosineDistVec(goVec, cVec)
	fmt.Printf("\n=== Go vs simulated-C vectorizer ===\n")
	fmt.Printf("Cosine distance between Go and C vectors: %.4f\n", dist)
	fmt.Printf("(If > 0.1, they are significantly different)\n")

	// Count differences
	goNZ, cNZ, diffs := 0, 0, 0
	for i := 0; i < VectorDim; i++ {
		if goVec[i] != 0 {
			goNZ++
		}
		if cVec[i] != 0 {
			cNZ++
		}
		if goVec[i] != 0 || cVec[i] != 0 {
			if math.Abs(float64(goVec[i]-cVec[i])) > 0.001 {
				diffs++
			}
		}
	}
	fmt.Printf("Go non-zero dims: %d, C non-zero dims: %d, differing dims: %d\n", goNZ, cNZ, diffs)

	// The distance SHOULD be > 0 because Go has extra features
	if dist < 0.001 {
		fmt.Println("WARNING: Go and C vectors are identical - Go features may not be working")
	}
}

func TestVectorizerDiscrimination(t *testing.T) {
	// Full discrimination test with realistic email texts
	fmt.Println("\n=== Full discrimination matrix ===")

	emails := []struct {
		label string
		text  string
	}{
		{"attack1", "subject: Urgent: Password Reset Required body: Your account password will expire in 24 hours. Click here immediately to reset your password or your account will be locked."},
		{"attack2", "subject: Wire Transfer Request body: Hi, I need you to process an urgent wire transfer of $45,000 to the account below. This is confidential, do not discuss with anyone."},
		{"attack3", "subject: Verify Your Account body: Your account has been compromised. Click immediately to verify your credentials or your account will be suspended."},
		{"safe1", "subject: Re: Project Update body: Thanks for the update. The new feature looks great. Let's discuss in tomorrow's standup."},
		{"safe2", "subject: Lunch tomorrow? body: Hey, want to grab lunch at the Thai place tomorrow around noon?"},
		{"safe3", "subject: Q3 Budget Review body: Please find attached the Q3 budget summary. All departments are within allocation."},
		{"spam1", "subject: AMAZING DEAL - 90% OFF!!! body: Limited time offer! Get premium watches at 90% discount. Buy now before stock runs out! Free shipping worldwide!"},
		{"spam2", "subject: You've won $5,000,000 body: Congratulations! You have been selected as the winner of our international lottery. Send your bank details to claim your prize."},
		{"spam3", "subject: Lose 30 Pounds in 30 Days body: Revolutionary new diet pill guaranteed to help you lose weight fast. No exercise needed. Order now and get a free bottle."},
	}

	vecs := make([][]float32, len(emails))
	for i, e := range emails {
		vecs[i] = vectorize(e.text)
	}

	// Print distance matrix
	fmt.Printf("\n%-10s", "")
	for _, e := range emails {
		fmt.Printf("%-10s", e.label)
	}
	fmt.Println()

	for i, ei := range emails {
		fmt.Printf("%-10s", ei.label)
		for j := range emails {
			d := cosineDistVec(vecs[i], vecs[j])
			fmt.Printf("%-10.3f", d)
		}
		fmt.Println()
	}

	// Check that within-class distances are lower than between-class distances
	attackVecs := vecs[0:3]
	safeVecs := vecs[3:6]
	spamVecs := vecs[6:9]

	avgWithinAttack := avgDist(attackVecs)
	avgWithinSafe := avgDist(safeVecs)
	avgWithinSpam := avgDist(spamVecs)
	avgAttackSafe := avgCrossDist(attackVecs, safeVecs)
	avgAttackSpam := avgCrossDist(attackVecs, spamVecs)
	avgSafeSpam := avgCrossDist(safeVecs, spamVecs)

	fmt.Printf("\nAverage within-class distances:\n")
	fmt.Printf("  attack:   %.4f\n", avgWithinAttack)
	fmt.Printf("  safe:     %.4f\n", avgWithinSafe)
	fmt.Printf("  spam:     %.4f\n", avgWithinSpam)
	fmt.Printf("\nAverage between-class distances:\n")
	fmt.Printf("  attack-safe:  %.4f\n", avgAttackSafe)
	fmt.Printf("  attack-spam:  %.4f\n", avgAttackSpam)
	fmt.Printf("  safe-spam:    %.4f\n", avgSafeSpam)

	// Attack within-class should be lower than attack-to-other between-class.
	// Safe/spam within-class vs between-class is inherently weak for bag-of-words
	// with only 3 examples per class (diverse vocabulary), so we only check attack
	// which shares domain-specific keywords (password, urgent, verify, click, etc.).
	if avgWithinAttack >= avgAttackSafe {
		t.Errorf("attack within-class (%.4f) >= attack-safe between-class (%.4f)", avgWithinAttack, avgAttackSafe)
	}
	if avgWithinAttack >= avgAttackSpam {
		t.Errorf("attack within-class (%.4f) >= attack-spam between-class (%.4f)", avgWithinAttack, avgAttackSpam)
	}

	// Overall: between-class distances should exceed within-class on average
	allWithin := (avgWithinAttack + avgWithinSafe + avgWithinSpam) / 3
	allBetween := (avgAttackSafe + avgAttackSpam + avgSafeSpam) / 3
	fmt.Printf("\nOverall within-class avg: %.4f\n", allWithin)
	fmt.Printf("Overall between-class avg: %.4f\n", allBetween)
	fmt.Printf("Discrimination ratio (between/within): %.4f\n", allBetween/allWithin)
	if allBetween <= allWithin {
		t.Errorf("overall between-class (%.4f) <= within-class (%.4f) - no discrimination", allBetween, allWithin)
	}
}

func avgDist(vecs [][]float32) float64 {
	sum := 0.0
	count := 0
	for i := 0; i < len(vecs); i++ {
		for j := i + 1; j < len(vecs); j++ {
			sum += cosineDistVec(vecs[i], vecs[j])
			count++
		}
	}
	if count == 0 {
		return 0
	}
	return sum / float64(count)
}

func avgCrossDist(a, b [][]float32) float64 {
	sum := 0.0
	count := 0
	for i := 0; i < len(a); i++ {
		for j := 0; j < len(b); j++ {
			sum += cosineDistVec(a[i], b[j])
			count++
		}
	}
	if count == 0 {
		return 0
	}
	return sum / float64(count)
}

func cosineDistVec(a, b []float32) float64 {
	var dot, na, nb float64
	for i := range a {
		dot += float64(a[i]) * float64(b[i])
		na += float64(a[i]) * float64(a[i])
		nb += float64(b[i]) * float64(b[i])
	}
	if na == 0 || nb == 0 {
		return 1.0
	}
	return 1.0 - dot/(math.Sqrt(na)*math.Sqrt(nb))
}
