package main

import (
	"net/url"
	"strings"
)

// ================================================================
// Types
// ================================================================

// ParsedEmail holds a fully parsed raw email.
type ParsedEmail struct {
	// Core headers
	From       string
	To         string
	Subject    string
	Date       string
	ReplyTo    string
	ReturnPath string

	// Authentication headers
	SPFResult   string // "pass", "fail", "softfail", "none", etc.
	DKIMResult  string // "pass", "fail", "none", etc.
	DMARCResult string // "pass", "fail", "none", etc.
	AuthResults string // raw Authentication-Results header

	// Metadata
	ContentType     string
	XMailer         string
	ReceivedHeaders []string // all Received: headers
	MessageID       string

	// Extracted from headers
	SenderDomain     string // domain part of From
	ReplyToDomain    string // domain part of Reply-To
	ReturnPathDomain string // domain part of Return-Path

	// Body
	Body string // the email body text (after headers)

	// All headers as map
	Headers map[string][]string
}

// ================================================================
// Parser
// ================================================================

// ParseRawEmail parses a raw email string (headers + blank line + body).
// It handles folded headers, repeated headers, and various edge cases.
func ParseRawEmail(raw string) *ParsedEmail {
	p := &ParsedEmail{
		Headers: make(map[string][]string),
	}

	// Normalize line endings to \n
	raw = strings.ReplaceAll(raw, "\r\n", "\n")

	// Split on first blank line → headers section + body section
	headerSection, body := splitHeaderBody(raw)

	// If there are no headers (no colon in the first line), treat entire
	// input as body and use the first line as subject.
	if headerSection == "" {
		p.Body = raw
		if nl := strings.IndexByte(raw, '\n'); nl >= 0 {
			p.Subject = strings.TrimSpace(raw[:nl])
		} else {
			p.Subject = strings.TrimSpace(raw)
		}
		return p
	}

	p.Body = body

	// Parse headers: unfold continuation lines, then split into key: value pairs.
	parseHeaders(p, headerSection)

	// Populate named fields from the header map.
	p.From = firstHeader(p, "from")
	p.To = firstHeader(p, "to")
	p.Subject = firstHeader(p, "subject")
	p.Date = firstHeader(p, "date")
	p.ReplyTo = firstHeader(p, "reply-to")
	p.ReturnPath = firstHeader(p, "return-path")
	p.ContentType = firstHeader(p, "content-type")
	p.XMailer = firstHeader(p, "x-mailer")
	p.MessageID = firstHeader(p, "message-id")
	p.AuthResults = firstHeader(p, "authentication-results")

	// Collect all Received headers (there are typically many).
	if recv, ok := p.Headers["received"]; ok {
		p.ReceivedHeaders = recv
	}

	// Extract domains from email address headers.
	p.SenderDomain = extractDomain(extractEmailAddress(p.From))
	p.ReplyToDomain = extractDomain(extractEmailAddress(p.ReplyTo))
	p.ReturnPathDomain = extractDomain(extractEmailAddress(p.ReturnPath))

	// Parse Authentication-Results to get SPF/DKIM/DMARC verdicts.
	if p.AuthResults != "" {
		p.SPFResult, p.DKIMResult, p.DMARCResult = parseAuthResults(p.AuthResults)
	}

	return p
}

// splitHeaderBody splits a raw email into header and body sections.
// The split happens at the first blank line. If there's no blank line,
// we check whether the content looks like headers (first line has a colon).
func splitHeaderBody(raw string) (headers, body string) {
	// Look for the blank-line separator.
	idx := strings.Index(raw, "\n\n")
	if idx >= 0 {
		headers = raw[:idx]
		body = raw[idx+2:]
		// Verify this actually looks like a header section: first line
		// should contain a colon before any newline.
		firstLine := headers
		if nl := strings.IndexByte(firstLine, '\n'); nl >= 0 {
			firstLine = firstLine[:nl]
		}
		if !strings.Contains(firstLine, ":") {
			// Doesn't look like headers — treat everything as body.
			return "", raw
		}
		return headers, body
	}

	// No blank line. Check if content looks like headers only (no body).
	firstLine := raw
	if nl := strings.IndexByte(firstLine, '\n'); nl >= 0 {
		firstLine = firstLine[:nl]
	}
	if strings.Contains(firstLine, ":") {
		return raw, ""
	}

	// Doesn't look like headers — treat everything as body.
	return "", raw
}

// parseHeaders parses the header section into p.Headers.
// Handles folded (multi-line) headers per RFC 2822: continuation lines
// start with a space or tab.
func parseHeaders(p *ParsedEmail, section string) {
	lines := strings.Split(section, "\n")

	// Unfold: merge continuation lines into the preceding header line.
	var unfolded []string
	for _, line := range lines {
		if len(line) > 0 && (line[0] == ' ' || line[0] == '\t') {
			// Continuation of previous header.
			if len(unfolded) > 0 {
				unfolded[len(unfolded)-1] += " " + strings.TrimSpace(line)
			}
		} else {
			unfolded = append(unfolded, line)
		}
	}

	// Split each unfolded line into key: value.
	for _, line := range unfolded {
		colon := strings.IndexByte(line, ':')
		if colon < 0 {
			continue // skip malformed lines
		}
		key := strings.TrimSpace(line[:colon])
		value := strings.TrimSpace(line[colon+1:])
		lowerKey := strings.ToLower(key)
		p.Headers[lowerKey] = append(p.Headers[lowerKey], value)
	}
}

// firstHeader returns the first value for a header key (lowercased), or "".
func firstHeader(p *ParsedEmail, key string) string {
	if vals, ok := p.Headers[key]; ok && len(vals) > 0 {
		return vals[0]
	}
	return ""
}

// ================================================================
// Email address / domain helpers
// ================================================================

// extractEmailAddress extracts the email address from a header value.
//
//	"John Doe <john@example.com>" → "john@example.com"
//	"<john@example.com>"          → "john@example.com"
//	"john@example.com"            → "john@example.com"
func extractEmailAddress(header string) string {
	header = strings.TrimSpace(header)
	if header == "" {
		return ""
	}

	// Look for angle-bracket form: ... <addr>
	if start := strings.LastIndex(header, "<"); start >= 0 {
		if end := strings.Index(header[start:], ">"); end >= 0 {
			return strings.TrimSpace(header[start+1 : start+end])
		}
	}

	// Bare address — strip any surrounding whitespace and quotes.
	addr := strings.Trim(header, "\"' ")
	if strings.Contains(addr, "@") {
		return addr
	}

	return ""
}

// extractDomain extracts the domain from an email address.
//
//	"john@example.com" → "example.com"
//	""                 → ""
func extractDomain(emailAddr string) string {
	emailAddr = strings.TrimSpace(emailAddr)
	if at := strings.LastIndex(emailAddr, "@"); at >= 0 {
		domain := emailAddr[at+1:]
		domain = strings.TrimRight(domain, ">. ")
		return strings.ToLower(domain)
	}
	return ""
}

// ================================================================
// Authentication-Results parsing
// ================================================================

// parseAuthResults parses an Authentication-Results header value and
// extracts the SPF, DKIM, and DMARC result strings.
//
// Example header value:
//
//	mx.example.com; spf=fail smtp.mailfrom=paypa1.com; dkim=fail header.d=paypa1.com
//
// Returns ("fail", "fail", "").
func parseAuthResults(header string) (spf, dkim, dmarc string) {
	// Lowercase for matching; results are case-insensitive.
	lower := strings.ToLower(header)

	spf = findAuthResult(lower, "spf=")
	dkim = findAuthResult(lower, "dkim=")
	dmarc = findAuthResult(lower, "dmarc=")

	return spf, dkim, dmarc
}

// findAuthResult finds "prefix<value>" in s and returns <value>.
// The value is the token immediately after the prefix, terminated by
// whitespace, semicolon, or end of string.
func findAuthResult(s, prefix string) string {
	idx := strings.Index(s, prefix)
	if idx < 0 {
		return ""
	}
	rest := s[idx+len(prefix):]

	// Extract the result token (e.g. "pass", "fail", "softfail", "none").
	end := len(rest)
	for i, ch := range rest {
		if ch == ' ' || ch == '\t' || ch == ';' || ch == '\n' || ch == '\r' {
			end = i
			break
		}
	}
	return rest[:end]
}

// ================================================================
// URL extraction
// ================================================================

// ExtractURLs finds all URLs in the email body.
// Uses a simple prefix-scanning approach (no regex).
func ExtractURLs(body string) []string {
	var urls []string
	seen := make(map[string]bool)

	// Scan for http://, https://, and www. prefixes.
	prefixes := []string{"https://", "http://", "www."}

	for _, prefix := range prefixes {
		remaining := body
		for {
			idx := strings.Index(strings.ToLower(remaining), prefix)
			if idx < 0 {
				break
			}

			// For www. prefix, prepend http:// for consistency.
			start := idx
			u := extractURLAt(remaining, start)
			if u == "" {
				remaining = remaining[idx+len(prefix):]
				continue
			}

			// Normalize www. URLs.
			if strings.HasPrefix(strings.ToLower(u), "www.") {
				u = "http://" + u
			}

			if !seen[u] {
				seen[u] = true
				urls = append(urls, u)
			}

			remaining = remaining[idx+len(prefix):]
		}
	}

	return urls
}

// extractURLAt extracts a URL starting at position start in s.
// Returns the URL string or "" if nothing useful.
func extractURLAt(s string, start int) string {
	if start >= len(s) {
		return ""
	}

	end := len(s)
	for i := start; i < len(s); i++ {
		ch := s[i]
		// Terminate on whitespace or common delimiters.
		if ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
			ch == '>' || ch == '"' || ch == '\'' || ch == '<' {
			end = i
			break
		}
	}

	u := s[start:end]

	// Trim trailing punctuation that's likely not part of the URL.
	u = strings.TrimRight(u, ".,;!?()[]")

	if len(u) < 5 {
		return ""
	}

	return u
}

// ExtractURLDomains returns unique domains from URLs found in the body.
func ExtractURLDomains(body string) []string {
	urls := ExtractURLs(body)
	seen := make(map[string]bool)
	var domains []string

	for _, rawURL := range urls {
		// Ensure the URL has a scheme so url.Parse works.
		if !strings.Contains(rawURL, "://") {
			rawURL = "http://" + rawURL
		}
		parsed, err := url.Parse(rawURL)
		if err != nil || parsed.Host == "" {
			continue
		}
		host := strings.ToLower(parsed.Hostname())
		if host != "" && !seen[host] {
			seen[host] = true
			domains = append(domains, host)
		}
	}

	return domains
}
