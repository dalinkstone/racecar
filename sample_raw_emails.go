package main

// RawTestEmail holds a single raw email with full headers for testing
// the two-stage classification pipeline (classify-raw).
type RawTestEmail struct {
	Description string // what this tests
	Expected    string // "safe", "spam", "attack"
	Subtype     string // expected attack subtype (for attacks): "PHISHING", "CEO_FRAUD", etc.
	Raw         string // the full raw email text with headers
}

// RawTestEmails is a curated set of raw emails that exercise header analysis:
// SPF/DKIM failures, domain mismatches, typosquatting, missing auth, etc.
var RawTestEmails = []RawTestEmail{
	// ----------------------------------------------------------------
	// 1. Phishing — PayPal typosquat with SPF fail
	// ----------------------------------------------------------------
	{
		Description: "Phishing: PayPal typosquat (paypa1.com) with SPF and DKIM fail",
		Expected:    "attack",
		Subtype:     "PHISHING",
		Raw: `From: "PayPal Security" <security@paypa1.com>
To: user@example.com
Subject: Urgent: Account Verification Required
Date: Mon, 23 Mar 2026 10:00:00 -0500
MIME-Version: 1.0
Content-Type: text/plain; charset=UTF-8
Reply-To: verify@paypa1-secure.com
Authentication-Results: mx.example.com;
 spf=fail smtp.mailfrom=paypa1.com;
 dkim=fail header.d=paypa1.com

Dear Customer,

We have detected unusual activity on your PayPal account. Your account has been temporarily limited until you verify your identity.

Please click the link below to verify your account within 24 hours or your account will be permanently suspended.

Verify Now: http://paypa1-secure.com/verify?id=839281

Thank you,
PayPal Security Team
`,
	},

	// ----------------------------------------------------------------
	// 2. CEO Fraud — wire transfer request, domain mismatch
	// ----------------------------------------------------------------
	{
		Description: "CEO Fraud: wire transfer from impersonated CEO, domain mismatch, Reply-To gmail",
		Expected:    "attack",
		Subtype:     "CEO_FRAUD",
		Raw: `From: "Michael Thompson - CEO" <mthompson@company-secure.net>
To: accounts@acmecorp.com
Subject: Confidential - Urgent Wire Transfer
Date: Tue, 24 Mar 2026 08:15:00 -0500
Reply-To: mthompson.ceo@gmail.com
Authentication-Results: mx.acmecorp.com;
 spf=softfail smtp.mailfrom=company-secure.net;
 dkim=none

Hi,

I need you to process an urgent wire transfer today. This is time-sensitive and confidential — please do not discuss with anyone else until the transfer is complete.

Amount: $47,500
Bank: First National
Routing: 021000021
Account: 1234567890

Please confirm once processed. I'm in back-to-back meetings and can only communicate via email today.

Thanks,
Michael Thompson
CEO
`,
	},

	// ----------------------------------------------------------------
	// 3. Malware — fake invoice with attachment reference
	// ----------------------------------------------------------------
	{
		Description: "Malware: fake invoice referencing macro-laden attachment, SPF and DKIM fail",
		Expected:    "attack",
		Subtype:     "MALWARE",
		Raw: `From: "Accounting Dept" <billing@invoice-services.net>
To: admin@targetcompany.com
Subject: Invoice #INV-2026-4891 — Payment Overdue
Date: Wed, 25 Mar 2026 14:30:00 -0400
Content-Type: multipart/mixed; boundary="boundary123"
Authentication-Results: mx.targetcompany.com;
 spf=fail smtp.mailfrom=invoice-services.net;
 dkim=fail header.d=invoice-services.net

Please find attached the invoice for services rendered in February. The payment is now 30 days overdue.

Open the attached document to review the charges. Please enable macros to view the full invoice details.

If payment is not received within 48 hours, we will be forced to escalate to our collections department.

Regards,
Billing Department
`,
	},

	// ----------------------------------------------------------------
	// 4. Credential Harvest — Microsoft 365 fake, leet-speak domain
	// ----------------------------------------------------------------
	{
		Description: "Credential Harvest: fake Microsoft 365 password reset, micr0soft domain, PHPMailer X-Mailer",
		Expected:    "attack",
		Subtype:     "CREDENTIAL_HARVEST",
		Raw: `From: "Microsoft 365 Team" <no-reply@micr0soft-365.com>
To: employee@company.com
Subject: Your password expires in 24 hours
Date: Thu, 26 Mar 2026 09:00:00 -0500
Reply-To: support@micr0soft-365.com
X-Mailer: PHPMailer 6.5.0
Authentication-Results: mx.company.com;
 spf=fail smtp.mailfrom=micr0soft-365.com;
 dkim=none

Your Microsoft 365 password will expire in 24 hours.

To avoid losing access to your email, calendar, and files, please update your password immediately.

Click here to update your password: http://micr0soft-365.com/reset

If you did not request this change, please contact IT support immediately.

Microsoft 365 Support Team
`,
	},

	// ----------------------------------------------------------------
	// 5. Safe — legitimate IT notification
	// ----------------------------------------------------------------
	{
		Description: "Safe: legitimate internal IT maintenance notification, SPF and DKIM pass",
		Expected:    "safe",
		Subtype:     "",
		Raw: `From: "IT Department" <it-support@acmecorp.com>
To: all-staff@acmecorp.com
Subject: Scheduled Maintenance - Saturday 2am-6am
Date: Fri, 27 Mar 2026 16:00:00 -0500
MIME-Version: 1.0
Content-Type: text/plain; charset=UTF-8
Authentication-Results: mx.acmecorp.com;
 spf=pass smtp.mailfrom=acmecorp.com;
 dkim=pass header.d=acmecorp.com

Hi everyone,

We will be performing scheduled maintenance on our email and VPN systems this Saturday from 2am to 6am EST.

During this window, email delivery may be delayed and VPN access will be unavailable. Please plan accordingly.

If you have questions, contact the IT help desk at ext. 4500.

Thanks,
IT Infrastructure Team
`,
	},

	// ----------------------------------------------------------------
	// 6. Safe — colleague email with all auth passing
	// ----------------------------------------------------------------
	{
		Description: "Safe: internal colleague code review email, SPF/DKIM/DMARC all pass",
		Expected:    "safe",
		Subtype:     "",
		Raw: `From: "Sarah Chen" <sarah.chen@acmecorp.com>
To: team-engineering@acmecorp.com
Subject: Re: Code Review - PR #847
Date: Mon, 23 Mar 2026 11:30:00 -0500
Authentication-Results: mx.acmecorp.com;
 spf=pass smtp.mailfrom=acmecorp.com;
 dkim=pass header.d=acmecorp.com;
 dmarc=pass

Hey team,

I've reviewed PR #847 and left a few comments. The main concern is the error handling in the retry logic — we should add exponential backoff instead of the fixed 1-second delay.

Otherwise looks good. Let's discuss in standup tomorrow.

Sarah
`,
	},

	// ----------------------------------------------------------------
	// 7. Spam — lottery scam
	// ----------------------------------------------------------------
	{
		Description: "Spam: classic lottery scam with no SPF/DKIM, mass mailer X-Mailer",
		Expected:    "spam",
		Subtype:     "",
		Raw: `From: "International Lottery Commission" <winner@lottery-intl.xyz>
To: lucky-winner@example.com
Subject: CONGRATULATIONS!!! You've Won $2,500,000!!!
Date: Sun, 22 Mar 2026 03:00:00 +0000
X-Mailer: Mass Mailer Pro 4.2
Authentication-Results: mx.example.com;
 spf=none smtp.mailfrom=lottery-intl.xyz;
 dkim=none

CONGRATULATIONS!!!

Your email address was randomly selected in our International Online Lottery Program. You have won the grand prize of TWO MILLION FIVE HUNDRED THOUSAND US DOLLARS ($2,500,000.00)!!!

To claim your prize, please reply with the following information:
- Full Name
- Address
- Phone Number
- Bank Account Details

ACT NOW — prizes must be claimed within 7 days or they will be forfeited!

Dr. James Williams
Director, International Lottery Commission
`,
	},

	// ----------------------------------------------------------------
	// 8. Phishing — Google Docs share, leet speak in domain
	// ----------------------------------------------------------------
	{
		Description: "Phishing: fake Google Docs share notification, g00gle-docs.com typosquat, SPF/DKIM fail",
		Expected:    "attack",
		Subtype:     "PHISHING",
		Raw: `From: "Google Drive" <noreply@g00gle-docs.com>
To: user@example.com
Subject: Document shared with you: Q1_Financial_Report.pdf
Date: Mon, 23 Mar 2026 07:45:00 -0700
Reply-To: share-notification@g00gle-docs.com
Authentication-Results: mx.example.com;
 spf=fail smtp.mailfrom=g00gle-docs.com;
 dkim=fail header.d=g00gle-docs.com

A document has been shared with you via Google Docs.

Document: Q1_Financial_Report.pdf
Shared by: CFO Office

Click below to view the document:
http://g00gle-docs.com/view/doc/Q1_Financial_Report

You need to sign in with your Google account to access this file.

This is an automated notification from Google Drive.
`,
	},

	// ----------------------------------------------------------------
	// 9. Safe — order confirmation from real company
	// ----------------------------------------------------------------
	{
		Description: "Safe: legitimate Amazon shipping confirmation, SPF and DKIM pass",
		Expected:    "safe",
		Subtype:     "",
		Raw: `From: "Amazon.com" <ship-confirm@amazon.com>
To: customer@example.com
Subject: Your order has shipped! #112-4839281-7738492
Date: Sat, 21 Mar 2026 18:00:00 -0700
Authentication-Results: mx.example.com;
 spf=pass smtp.mailfrom=amazon.com;
 dkim=pass header.d=amazon.com

Your package is on its way!

Order #112-4839281-7738492
Item: USB-C Hub, 7 ports
Estimated delivery: March 25, 2026

Track your package at amazon.com/orders

Thank you for shopping with us!
Amazon.com
`,
	},

	// ----------------------------------------------------------------
	// 10. CEO Fraud — no auth headers at all, urgent tone
	// ----------------------------------------------------------------
	{
		Description: "CEO Fraud: payroll redirect with no Authentication-Results header at all",
		Expected:    "attack",
		Subtype:     "CEO_FRAUD",
		Raw: `From: "CEO Office" <ceo@executive-mail.biz>
To: payroll@targetcorp.com
Subject: URGENT - Payroll Update Required
Date: Mon, 23 Mar 2026 06:30:00 -0500

This is urgent. I need you to update the direct deposit for my paycheck before end of day.

New routing number: 021000021
New account number: 9876543210

Do not discuss this with anyone. I am traveling and only reachable by email.

Sent from my iPhone
`,
	},

	// ----------------------------------------------------------------
	// 11. Credential Harvest — Apple ID with leet-speak misspellings
	// ----------------------------------------------------------------
	{
		Description: "Credential Harvest: fake Apple ID lock notice, app1e-id.com typosquat, leet-speak body",
		Expected:    "attack",
		Subtype:     "CREDENTIAL_HARVEST",
		Raw: `From: "App1e Support" <support@app1e-id.com>
To: victim@example.com
Subject: Your App1e ID has been 1ocked
Date: Tue, 24 Mar 2026 12:00:00 +0000
Reply-To: unlock@app1e-id.com
Authentication-Results: mx.example.com;
 spf=fail smtp.mailfrom=app1e-id.com;
 dkim=fail header.d=app1e-id.com

Dear Customer,

Your App1e ID has been 1ocked due to susp1cious activity.

To un1ock your acc0unt, p1ease verify your identity by clicking the link below:

http://app1e-id.com/verify

You will need to provide your App1e ID password and answer your secur1ty questions.

Failure to verify within 24 hours will result in permanent acc0unt susp3nsion.

App1e Support
`,
	},

	// ----------------------------------------------------------------
	// 12. Spam — diet pill email
	// ----------------------------------------------------------------
	{
		Description: "Spam: diet pill advertisement with bulk mailer, no SPF",
		Expected:    "spam",
		Subtype:     "",
		Raw: `From: "Health Solutions" <deals@amazinghealth99.com>
To: subscriber@example.com
Subject: Lose 30 Pounds in 30 Days - GUARANTEED!!!
Date: Sun, 22 Mar 2026 05:00:00 +0000
X-Mailer: BulkMailer 2.1
Authentication-Results: mx.example.com;
 spf=none smtp.mailfrom=amazinghealth99.com

AMAZING BREAKTHROUGH IN WEIGHT LOSS!!!

Scientists have discovered a revolutionary new formula that MELTS FAT while you sleep!

No diet! No exercise! No effort!

Order NOW and get a FREE 30-day supply!

LIMITED TIME OFFER - 90% OFF regular price!

Click here to order: http://amazinghealth99.com/order

Results may vary. This statement has not been evaluated by the FDA.

To unsubscribe, reply with STOP.
`,
	},
}
