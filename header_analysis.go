package main

import (
	"fmt"
	"strings"
	"unicode"
)

// ================================================================
// Types
// ================================================================

// AttackSubtype categorizes the specific kind of attack.
type AttackSubtype string

const (
	SubtypePhishing          AttackSubtype = "PHISHING"
	SubtypeCEOFraud          AttackSubtype = "CEO_FRAUD"
	SubtypeMalware           AttackSubtype = "MALWARE"
	SubtypeCredentialHarvest AttackSubtype = "CREDENTIAL_HARVEST"
	SubtypeGenericAttack     AttackSubtype = "GENERIC_ATTACK"
)

// HeaderAnalysis contains the results of Stage 2 analysis.
type HeaderAnalysis struct {
	Subtype        AttackSubtype
	RiskScore      float32  // 0.0 to 1.0
	Signals        []string // human-readable risk signals (for display)
	DomainMismatch bool
	SPFFail        bool
	DKIMFail       bool
	UrgencyScore   float32 // 0.0 to 1.0 — density of urgency language
	CredentialAsk  bool    // asks for credentials/personal info
	FinancialAsk   bool    // asks for money/wire transfer
	HasSuspiciousURLs bool
}

// ================================================================
// Main analysis functions
// ================================================================

// AnalyzeEmail performs Stage 2 context-aware analysis on a parsed email.
// Returns detailed header analysis with attack subtype and risk score.
func AnalyzeEmail(email *ParsedEmail) *HeaderAnalysis {
	result := &HeaderAnalysis{}

	checkDomainMismatch(email, result)
	checkAuthentication(email, result)
	checkTyposquat(email, result)
	checkUrgency(email, result)
	checkCredentialAsk(email, result)
	checkSuspiciousURLs(email, result)
	checkMalware(email, result)

	// Cap risk score
	if result.RiskScore > 1.0 {
		result.RiskScore = 1.0
	}

	// Classify subtype
	classifyAttackSubtype(result)

	return result
}

// AnalyzeFromText runs Stage 2 analysis on plain text (no headers).
// Only checks content patterns, not authentication.
func AnalyzeFromText(subject, body string) *HeaderAnalysis {
	email := &ParsedEmail{
		Subject: subject,
		Body:    body,
	}
	result := &HeaderAnalysis{}

	checkUrgency(email, result)
	checkCredentialAsk(email, result)
	checkSuspiciousURLs(email, result)
	checkMalware(email, result)

	// Cap risk score
	if result.RiskScore > 1.0 {
		result.RiskScore = 1.0
	}

	classifyAttackSubtype(result)

	return result
}

// ================================================================
// Analysis checks
// ================================================================

// checkDomainMismatch detects when Reply-To or Return-Path domains
// differ from the sender domain — a common spoofing indicator.
func checkDomainMismatch(email *ParsedEmail, result *HeaderAnalysis) {
	if email.ReplyToDomain != "" && email.SenderDomain != "" {
		if email.ReplyToDomain != email.SenderDomain {
			result.DomainMismatch = true
			result.Signals = append(result.Signals,
				fmt.Sprintf("Reply-To domain '%s' differs from sender domain '%s'",
					email.ReplyToDomain, email.SenderDomain))
			result.RiskScore += 0.2
		}
	}
	// Also check Return-Path
	if email.ReturnPathDomain != "" && email.SenderDomain != "" {
		if email.ReturnPathDomain != email.SenderDomain {
			result.Signals = append(result.Signals,
				fmt.Sprintf("Return-Path domain '%s' differs from sender domain '%s'",
					email.ReturnPathDomain, email.SenderDomain))
			result.RiskScore += 0.1
		}
	}
}

// checkAuthentication checks SPF, DKIM, and DMARC results for failures.
func checkAuthentication(email *ParsedEmail, result *HeaderAnalysis) {
	if strings.EqualFold(email.SPFResult, "fail") || strings.EqualFold(email.SPFResult, "softfail") {
		result.SPFFail = true
		result.Signals = append(result.Signals, "SPF check: FAIL")
		result.RiskScore += 0.15
	}
	if strings.EqualFold(email.DKIMResult, "fail") {
		result.DKIMFail = true
		result.Signals = append(result.Signals, "DKIM check: FAIL")
		result.RiskScore += 0.15
	}
	if strings.EqualFold(email.DMARCResult, "fail") {
		result.Signals = append(result.Signals, "DMARC check: FAIL")
		result.RiskScore += 0.1
	}
}

// knownDomains is a list of well-known domains used for typosquat detection.
var knownDomains = []string{
	"paypal.com", "google.com", "microsoft.com", "apple.com",
	"amazon.com", "facebook.com", "netflix.com", "linkedin.com",
	"chase.com", "wellsfargo.com", "bankofamerica.com", "citibank.com",
	"dropbox.com", "docusign.com", "adobe.com", "zoom.us",
}

// checkTyposquat detects sender domains that look like typosquats of
// well-known domains (e.g. "paypa1.com" vs "paypal.com").
func checkTyposquat(email *ParsedEmail, result *HeaderAnalysis) {
	domain := strings.ToLower(email.SenderDomain)
	if domain == "" {
		return
	}

	for _, known := range knownDomains {
		if domain == known {
			continue // exact match is fine
		}
		similarity := domainSimilarity(domain, known)
		if similarity > 0.7 && similarity < 1.0 {
			result.Signals = append(result.Signals,
				fmt.Sprintf("Sender domain '%s' looks like typosquat of '%s'", domain, known))
			result.RiskScore += 0.25
			result.HasSuspiciousURLs = true
			break
		}
	}
}

// urgencyWords contains words and phrases that indicate urgency — a
// common social-engineering tactic in phishing and CEO fraud.
var urgencyWords = map[string]bool{
	"urgent": true, "immediately": true, "asap": true, "right away": true,
	"expire": true, "expires": true, "expiring": true, "expired": true,
	"suspend": true, "suspended": true, "terminate": true, "terminated": true,
	"lock": true, "locked": true, "block": true, "blocked": true,
	"restricted": true, "limit": true, "deadline": true,
	"within 24 hours": true, "within 48 hours": true, "act now": true,
	"final notice": true, "last chance": true, "warning": true,
	"critical": true, "alert": true,
}

// checkUrgency scores the density of urgency language in the email.
func checkUrgency(email *ParsedEmail, result *HeaderAnalysis) {
	text := strings.ToLower(email.Subject + " " + email.Body)
	words := strings.Fields(text)
	urgentCount := 0

	for _, w := range words {
		w = strings.Trim(w, ".,!?;:\"'")
		if urgencyWords[w] {
			urgentCount++
		}
	}
	// Also check multi-word phrases
	for phrase := range urgencyWords {
		if strings.Contains(phrase, " ") && strings.Contains(text, phrase) {
			urgentCount++
		}
	}

	totalWords := len(words)
	if totalWords > 0 {
		result.UrgencyScore = float32(urgentCount) / float32(totalWords) * 10 // scale up
		if result.UrgencyScore > 1.0 {
			result.UrgencyScore = 1.0
		}
	}
	if result.UrgencyScore > 0.3 {
		result.Signals = append(result.Signals,
			fmt.Sprintf("High urgency language (score: %.2f)", result.UrgencyScore))
		result.RiskScore += result.UrgencyScore * 0.15
	}
}

// credentialPatterns are phrases indicating a request for credentials
// or personal information.
var credentialPatterns = []string{
	"password", "passwd", "credential", "login",
	"verify your", "confirm your", "update your",
	"social security", "ssn", "date of birth",
	"credit card", "card number", "cvv", "expiry",
	"bank account", "routing number", "account number",
	"sign in", "log in", "reset your password",
}

// financialPatterns are phrases indicating a financial/payment request.
var financialPatterns = []string{
	"wire transfer", "bank transfer", "send money",
	"payment", "invoice attached", "overdue payment",
	"process this transfer", "routing number",
	"bitcoin", "cryptocurrency", "gift card",
}

// checkCredentialAsk detects requests for credentials, personal
// information, or financial transactions.
func checkCredentialAsk(email *ParsedEmail, result *HeaderAnalysis) {
	text := strings.ToLower(email.Subject + " " + email.Body)
	for _, pattern := range credentialPatterns {
		if strings.Contains(text, pattern) {
			result.CredentialAsk = true
			result.Signals = append(result.Signals, "Requests credential/personal information")
			result.RiskScore += 0.15
			break
		}
	}
	for _, pattern := range financialPatterns {
		if strings.Contains(text, pattern) {
			result.FinancialAsk = true
			result.Signals = append(result.Signals, "Contains financial/payment request")
			result.RiskScore += 0.15
			break
		}
	}
}

// checkSuspiciousURLs looks for common suspicious link patterns in the body.
func checkSuspiciousURLs(email *ParsedEmail, result *HeaderAnalysis) {
	body := strings.ToLower(email.Body)

	suspiciousPatterns := []string{
		"click here", "click below", "click the link",
		"http://", // non-HTTPS links
	}

	for _, p := range suspiciousPatterns {
		if strings.Contains(body, p) {
			result.HasSuspiciousURLs = true
			break
		}
	}

	if result.HasSuspiciousURLs {
		result.Signals = append(result.Signals, "Contains suspicious link patterns")
		result.RiskScore += 0.1
	}
}

// malwarePatterns are phrases indicating malware delivery attempts.
var malwarePatterns = []string{
	"attachment", "attached document", "attached file",
	"download", "open the document", "enable macros",
	"enable content", ".exe", ".zip", ".scr", ".bat",
}

// checkMalware detects language commonly associated with malware delivery.
func checkMalware(email *ParsedEmail, result *HeaderAnalysis) {
	text := strings.ToLower(email.Subject + " " + email.Body)
	for _, pattern := range malwarePatterns {
		if strings.Contains(text, pattern) {
			result.Signals = append(result.Signals,
				fmt.Sprintf("Malware indicator: contains '%s'", pattern))
			result.RiskScore += 0.1
			break
		}
	}
}

// ================================================================
// Attack subtype classification
// ================================================================

// classifyAttackSubtype determines the specific attack category
// based on the signals collected during analysis.
func classifyAttackSubtype(result *HeaderAnalysis) {
	// CEO Fraud: financial ask + urgency + often no auth failures
	if result.FinancialAsk && result.UrgencyScore > 0.2 {
		result.Subtype = SubtypeCEOFraud
		return
	}
	// Credential Harvest: asks for credentials
	if result.CredentialAsk {
		result.Subtype = SubtypeCredentialHarvest
		return
	}
	// Phishing: domain mismatch or typosquat + auth failures
	if result.DomainMismatch || result.SPFFail || result.HasSuspiciousURLs {
		result.Subtype = SubtypePhishing
		return
	}
	// Malware: check body for attachment/download keywords
	if hasMalwareLanguage(result) {
		result.Subtype = SubtypeMalware
		return
	}
	result.Subtype = SubtypeGenericAttack
}

// hasMalwareLanguage returns true if the analysis found malware indicators.
func hasMalwareLanguage(result *HeaderAnalysis) bool {
	for _, sig := range result.Signals {
		if strings.HasPrefix(sig, "Malware indicator:") {
			return true
		}
	}
	return false
}

// ================================================================
// String similarity (Levenshtein)
// ================================================================

// domainSimilarity returns a similarity ratio (0.0 to 1.0) between
// two domain strings using Levenshtein distance.
func domainSimilarity(a, b string) float32 {
	dist := levenshteinDistance(a, b)
	maxLen := len(a)
	if len(b) > maxLen {
		maxLen = len(b)
	}
	if maxLen == 0 {
		return 1.0
	}
	return 1.0 - float32(dist)/float32(maxLen)
}

// levenshteinDistance computes the edit distance between two strings
// using standard dynamic programming.
func levenshteinDistance(a, b string) int {
	la := []rune(a)
	lb := []rune(b)
	m := len(la)
	n := len(lb)

	if m == 0 {
		return n
	}
	if n == 0 {
		return m
	}

	// Allocate two rows for the DP table.
	prev := make([]int, n+1)
	curr := make([]int, n+1)

	for j := 0; j <= n; j++ {
		prev[j] = j
	}

	for i := 1; i <= m; i++ {
		curr[0] = i
		for j := 1; j <= n; j++ {
			cost := 1
			if unicode.ToLower(la[i-1]) == unicode.ToLower(lb[j-1]) {
				cost = 0
			}
			del := prev[j] + 1
			ins := curr[j-1] + 1
			sub := prev[j-1] + cost

			min := del
			if ins < min {
				min = ins
			}
			if sub < min {
				min = sub
			}
			curr[j] = min
		}
		prev, curr = curr, prev
	}

	return prev[n]
}
