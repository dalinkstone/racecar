package main

import (
	"fmt"
	"hash/fnv"
	"math"
	"strings"
	"unicode"
)

// VectorDim is the dimensionality of the feature-hashed vectors.
const VectorDim = 256

// ================================================================
// Boost word maps — strong signals for attack/spam classification
// ================================================================

var attackBoostWords = map[string]float32{
	// Urgency
	"urgent": 2.0, "immediately": 2.0, "expire": 2.0, "suspend": 2.0,
	"locked": 2.0, "unauthorized": 2.0, "compromised": 2.0,
	// Credential requests
	"password": 1.8, "credential": 1.8, "verify": 1.8, "confirm": 1.8,
	"ssn": 2.0, "social": 1.5, "security": 1.5,
	// Financial
	"wire": 2.0, "transfer": 1.8, "payment": 1.5, "invoice": 1.5,
	"bank": 1.5, "account": 1.3,
	// Action words
	"click": 1.5, "download": 1.5, "attachment": 1.5, "open": 1.3,
}

var spamBoostWords = map[string]float32{
	"congratulations": 1.8, "winner": 2.0, "lottery": 2.0, "prize": 2.0,
	"free": 1.8, "discount": 1.5, "offer": 1.3, "limited": 1.5,
	"guaranteed": 1.8, "miracle": 2.0, "amazing": 1.5,
	"unsubscribe": 1.5, "buy": 1.3, "cheap": 1.5, "deal": 1.3,
}

// ================================================================
// Stop words
// ================================================================

var stopWords = map[string]bool{
	"the": true, "a": true, "an": true, "is": true, "are": true,
	"was": true, "were": true, "be": true, "been": true, "being": true,
	"have": true, "has": true, "had": true, "do": true, "does": true,
	"did": true, "will": true, "would": true, "could": true, "should": true,
	"may": true, "might": true, "shall": true, "can": true,
	"it": true, "its": true, "this": true, "that": true, "these": true, "those": true,
	"i": true, "me": true, "my": true, "we": true, "our": true, "you": true, "your": true,
	"he": true, "she": true, "they": true, "them": true, "their": true, "his": true, "her": true,
	"of": true, "in": true, "to": true, "for": true, "on": true, "with": true,
	"at": true, "by": true, "from": true, "as": true, "into": true,
	"and": true, "or": true, "but": true, "not": true, "no": true,
	"if": true, "then": true, "than": true, "so": true, "very": true,
	"just": true, "about": true, "up": true, "out": true, "all": true, "also": true,
}

// ================================================================
// Hash functions — must match the C tokenizer in tokenizer.c
// ================================================================

// fnvHash computes FNV-1a hash matching the C implementation's offset basis.
func fnvHash(s string) uint32 {
	h := fnv.New32a()
	h.Write([]byte(s))
	return h.Sum32()
}

// signHash computes a sign (+1 or -1) using an independent FNV-1a variant
// with a different offset basis (2147483647) to decorrelate from the
// dimension hash, matching the C tokenizer's sign_hash function.
func signHash(s string) int {
	var h uint32 = 2147483647 // different offset basis from standard FNV
	for i := 0; i < len(s); i++ {
		h ^= uint32(s[i])
		h *= 16777619
	}
	if h&1 != 0 {
		return 1
	}
	return -1
}

// ================================================================
// Leet speak normalization
// ================================================================

// normalizeLeet converts common leet speak substitutions to their
// alphabetic equivalents (e.g., "acc0unt" -> "account", "p@ssw0rd" -> "password").
func normalizeLeet(s string) string {
	r := strings.NewReplacer(
		"0", "o", "1", "l", "3", "e", "4", "a", "5", "s",
		"@", "a", "$", "s", "!", "i",
	)
	return r.Replace(s)
}

// ================================================================
// Tokenization
// ================================================================

// tokenize splits text into word tokens, splitting on any character that is
// not a letter or digit.
func tokenize(text string) []string {
	return strings.FieldsFunc(text, func(r rune) bool {
		return !unicode.IsLetter(r) && !unicode.IsDigit(r)
	})
}

// removeStopWords filters common English stop words from a token list.
func removeStopWords(words []string) []string {
	result := make([]string, 0, len(words))
	for _, w := range words {
		if !stopWords[w] {
			result = append(result, w)
		}
	}
	return result
}

// ================================================================
// processWords — core feature hashing with unigrams, bigrams, trigrams
// ================================================================

// processWords hashes words into the vector using three feature types:
//   - Unigrams with keyword boost weights (baseWeight scaled)
//   - Bigrams ("word1_word2") with weight 0.7 * baseWeight
//   - Character trigrams for misspelling/leet-speak resilience (0.3 * baseWeight)
func processWords(words []string, vec []float32, baseWeight float32) {
	if len(words) == 0 {
		return
	}

	// Unigrams with keyword boosting
	for _, word := range words {
		weight := float32(1.0)
		if boost, ok := attackBoostWords[word]; ok {
			weight = boost
		} else if boost, ok := spamBoostWords[word]; ok {
			weight = boost
		}
		idx := fnvHash(word) % uint32(VectorDim)
		sign := signHash(word)
		vec[idx] += float32(sign) * weight * baseWeight
	}

	// Bigrams (weight 0.7)
	if len(words) >= 2 {
		for i := 0; i+1 < len(words); i++ {
			bigram := words[i] + "_" + words[i+1]
			idx := fnvHash(bigram) % uint32(VectorDim)
			sign := signHash(bigram)
			vec[idx] += float32(sign) * 0.7 * baseWeight
		}
	}

	// Character trigrams (catch misspellings, leet speak)
	for _, word := range words {
		if len(word) >= 3 {
			for i := 0; i <= len(word)-3; i++ {
				trigram := word[i : i+3]
				idx := fnvHash(trigram) % uint32(VectorDim)
				sign := signHash(trigram)
				vec[idx] += float32(sign) * 0.3 * baseWeight
			}
		}
	}
}

// ================================================================
// vectorize — the main entry point
// ================================================================

// vectorize converts text into a 256-dimensional vector using enhanced
// feature hashing. It processes both the original text and a leet-speak
// normalized version to catch obfuscated words. Features include unigrams
// with keyword boosting, bigrams, and character trigrams. The output is
// L2-normalized for cosine similarity.
func vectorize(text string) []float32 {
	vec := make([]float32, VectorDim)
	text = strings.ToLower(text)

	// Process original text
	words := tokenize(text)
	words = removeStopWords(words)
	processWords(words, vec, 1.0)

	// Process leet-normalized version (additional signal, lower weight)
	normalized := normalizeLeet(text)
	if normalized != text {
		normWords := tokenize(normalized)
		normWords = removeStopWords(normWords)
		processWords(normWords, vec, 0.5)
	}

	// L2 normalize so cosine similarity works well
	var norm float32
	for _, v := range vec {
		norm += v * v
	}
	norm = float32(math.Sqrt(float64(norm)))
	if norm > 0 {
		for i := range vec {
			vec[i] /= norm
		}
	}
	return vec
}

// ================================================================
// formatVector — output helper
// ================================================================

// formatVector formats a float vector as a comma-separated string
// suitable for passing to the racecar CLI.
func formatVector(vec []float32) string {
	parts := make([]string, len(vec))
	for i, v := range vec {
		parts[i] = fmt.Sprintf("%.6f", v)
	}
	return strings.Join(parts, ",")
}
