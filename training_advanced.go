package main

// init appends advanced training emails to the base training data.
// These cover misspellings, leet speak, and targeted attack patterns
// that the base dataset doesn't include.
func init() {
	SafeEmails = append(SafeEmails, advancedSafeEmails...)
	SpamEmails = append(SpamEmails, advancedSpamEmails...)
	AttackEmails = append(AttackEmails, advancedAttackEmails...)
}

// advancedSafeEmails contains legitimate emails that use language similar to
// attack patterns (password resets, wire transfers, account verification)
// but are genuinely safe. These sharpen the classifier's decision boundary.
var advancedSafeEmails = []SampleEmail{
	// Password and security confirmations (could be confused with phishing)
	{
		"Password reset successful",
		"Your password has been successfully updated. If you did not make this change, please contact IT support at extension 4500 immediately.",
	},
	{
		"Security alert: New device login",
		"A new device was used to access your work account today at 2:34 PM from the office network. This is expected if you recently set up a new laptop.",
	},
	{
		"Two-factor authentication enabled",
		"Two-factor authentication has been successfully enabled on your account. You will now receive a verification code via the Authenticator app each time you sign in from a new device.",
	},

	// Legitimate financial communications (could be confused with BEC attacks)
	{
		"Wire transfer confirmation #8834",
		"The wire transfer of $12,500 to Vendor Corp has been processed successfully. Transaction reference: WR-2026-8834. Contact accounting with any questions.",
	},
	{
		"Invoice #2026-0394 attached",
		"Hi, please find attached the invoice for March consulting services. Payment is due within 30 days per our agreement. Thanks, Jennifer at Acme Consulting.",
	},
	{
		"Direct deposit updated successfully",
		"Your direct deposit information has been updated in the payroll system. The change will take effect for the next pay cycle on April 1st. If you did not request this change, contact HR at hr@company.com.",
	},

	// Action-required language (could be confused with urgency-based attacks)
	{
		"Action required: Complete your timesheet",
		"Reminder: Timesheets for the week ending March 20 are due by end of day Friday. Please log in to the HR portal and submit your hours.",
	},
	{
		"Action required: Review and sign updated NDA",
		"Legal has updated the mutual NDA template. All client-facing employees must review and sign the updated version via DocuSign by April 4th. The link was sent from our legal team's verified DocuSign account.",
	},
	{
		"Account verification complete",
		"Your identity verification has been completed successfully. Your account is now fully activated. Thank you for your patience during this process.",
	},

	// Legitimate shared documents (could be confused with document phishing)
	{
		"Shared: Q1 Budget Spreadsheet",
		"Hi team, I have shared the Q1 budget spreadsheet with you on Google Drive. Please review the numbers in your department's tab and flag any discrepancies before the Friday review meeting.",
	},
	{
		"OneDrive: Project proposal shared with you",
		"Rachel from marketing shared the project proposal document with you on OneDrive. The file is in the shared Marketing folder. Please add your comments directly in the document before the Wednesday deadline.",
	},

	// System notifications that resemble attack patterns
	{
		"Your VPN certificate has been renewed",
		"Your corporate VPN certificate was automatically renewed and is valid through September 30, 2026. No action is needed on your part. Contact the IT helpdesk if you experience any connectivity issues.",
	},
	{
		"Scheduled account maintenance completed",
		"The scheduled maintenance on your corporate account has been completed. All services have been restored. If you notice any issues with email or calendar access, please restart Outlook and try again.",
	},
	{
		"Voicemail transcription from +1-555-0142",
		"Voicemail received at 10:22 AM from +1-555-0142, duration 1 minute 15 seconds. Transcription: Hi, this is Dr. Peterson's office calling to confirm your appointment on Thursday at 3 PM. Please call us back to confirm.",
	},
}

// advancedSpamEmails contains unsolicited marketing emails that use urgency
// and account-related language similar to attacks but are really just
// aggressive commercial spam. These help distinguish spam from attacks.
var advancedSpamEmails = []SampleEmail{
	// Urgency-based spam (easily confused with attacks)
	{
		"FINAL WARNING: Account will be closed",
		"This is your final notice. Your account subscription has not been renewed. If you do not renew within 24 hours, all your data will be permanently deleted. Renew now for just $4.99.",
	},
	{
		"Action needed: Your subscription",
		"We noticed your payment method has expired. Update your billing information to avoid interruption of service. This is an automated notice.",
	},
	{
		"ALERT: Suspicious login attempt on your streaming account",
		"We noticed a login from an unrecognized device on your StreamFlix account. If this was you, no worries! If not, upgrade to our Premium Security Plan for just $2.99/month and protect your account today.",
	},
	{
		"Your cloud storage is 95% full",
		"You are running out of storage space! Upgrade to CloudMax Premium for 10x more storage at only $3.99/month. If you do not upgrade, you may lose access to new uploads. Act now and get your first month free!",
	},

	// VIP and membership spam
	{
		"Exclusive VIP membership - Act Fast!",
		"You've been pre-selected for our exclusive VIP club. Members enjoy unlimited access to premium content, early bird deals, and cashback rewards. Sign up today!",
	},
	{
		"Your free trial is about to expire",
		"Your 30-day free trial of PremiumSoft Pro ends tomorrow. Upgrade now to keep your data and unlock advanced features. Special price: $9.99/month!",
	},
	{
		"Re: Your recent purchase",
		"Thank you for your interest! As a valued customer, we'd like to offer you an exclusive 50% discount on your next order. Use code SAVE50 at checkout.",
	},

	// Financial spam (easily confused with financial attacks)
	{
		"Investment opportunity - 500% returns",
		"Our AI trading platform has generated over 500% returns for our investors this year. Start with as little as $250 and watch your money grow. Limited spots available.",
	},
	{
		"Passive income: Let your money work for you",
		"Join thousands of smart investors earning passive income with our automated portfolio management service. Average returns of 18% per year. No minimum balance. Start your free trial today!",
	},

	// Notification-style spam
	{
		"You have 1 unread notification",
		"You have a pending notification in your account. Log in to view your notification and claim your reward points. Over 50,000 members have already claimed theirs!",
	},
	{
		"Someone viewed your profile 3 times today",
		"Your profile is getting attention! Three people viewed your profile in the last 24 hours. Upgrade to PremiumConnect to see who is looking at you and unlock unlimited messaging. Only $7.99/month!",
	},
	{
		"Your digital wallet has a pending reward",
		"Great news! You have $15.00 in unclaimed cashback rewards in your ShopSmart wallet. Rewards expire in 48 hours. Log in to redeem your cashback or use it toward your next purchase.",
	},
	{
		"Limited time: Double your loyalty points",
		"For the next 72 hours only, earn DOUBLE loyalty points on every purchase! That means your morning coffee could earn you a free drink twice as fast. Open the app and start earning now.",
	},
}

// advancedAttackEmails contains sophisticated phishing and social engineering
// emails using leet speak, misspellings, targeted financial attacks, credential
// harvesting, fake notifications, and malware delivery techniques.
var advancedAttackEmails = []SampleEmail{
	// Leet speak and misspelling-based phishing (evading simple keyword filters)
	{
		"Your acc0unt has been susp3nded",
		"Dear cust0mer, your acc0unt has been temp0rarily susp3nded due to unusual l0gin activity. Pl3ase verify your ident1ty immediately to rest0re access.",
	},
	{
		"Micr0soft Security A1ert",
		"We detected an unauth0rized sign-in attempt on your Micr0soft acc0unt. If this wasn't y0u, please s3cure your acc0unt by clicking the l1nk below.",
	},
	{
		"PayPa1 Account Verificat1on",
		"Your PayPa1 acc0unt requires verificat1on. P1ease update your inf0rmation within 24 h0urs or your acc0unt will be perman3ntly l0cked.",
	},
	{
		"App1e ID Sign-in Attempt",
		"An unsuccessful s1gn-in att3mpt was made with your App1e ID. If you did n0t attempt to s1gn in, your acc0unt may be c0mpromised.",
	},
	{
		"Amaz0n Order Cancell3d - Verify N0w",
		"Your rec3nt Amaz0n order has been cancell3d due to a paym3nt issue. To r3instate your 0rder, please v3rify your payment d3tails immediately.",
	},
	{
		"Netfl1x: Paym3nt Decl1ned",
		"We were unab1e to pr0cess your m0nthly payment for Netfl1x. Your acc0unt will be susp3nded unless you upd4te your billing inf0rmation within 12 h0urs.",
	},
	{
		"W3lls Farg0 Acc0unt Susp3nded",
		"Your W3lls Farg0 0nline banking acc0unt has been t3mporarily susp3nded. Pl3ase l0g in through 0ur s3cure p0rtal to v3rify your id3ntity and r3store acc3ss.",
	},
	{
		"Imp0rtant: Upd4te Y0ur Passw0rd N0w",
		"For y0ur s3curity, we r3quire all us3rs to upd4te their passw0rds imm3diately. Cl1ck the l1nk b3low to ch4nge your passw0rd bef0re your acc0unt is l0cked.",
	},

	// Targeted financial attacks (BEC / business email compromise)
	{
		"URGENT: Process wire transfer before EOD",
		"I need you to handle a confidential wire transfer of $38,000 to the vendor account below. This cannot wait until Monday. Do not discuss with anyone until completed.",
	},
	{
		"Updated banking information for payment",
		"Please note that our banking details have changed. All future payments should be directed to our new account. Routing: 021000021, Account: 8765432109. This change is effective immediately.",
	},
	{
		"Payroll direct deposit change",
		"Please update my direct deposit information before the next pay cycle. New bank: Chase, Routing: 021000021, Account: 1234567890. This is urgent, thank you.",
	},
	{
		"CFO Request: Expedite vendor payment",
		"Hi, I spoke with the CFO and we need to expedite a payment of $52,000 to the vendor before close of business. The wire details are attached. Please process immediately and confirm. Do not reply to the CFO directly as he is traveling.",
	},
	{
		"Re: Overdue payment - new bank details",
		"We have switched banks and our previous account has been closed. Please redirect the overdue payment of $27,500 to our new account below. It is critical this is processed today to avoid late fees. New routing and account numbers attached.",
	},

	// Credential harvesting with social engineering
	{
		"IT Department: Mandatory password reset",
		"All employees must reset their passwords by end of day per the new security policy. Click here to reset: http://company-portal.com/reset. Have your current password ready.",
	},
	{
		"Action Required: Verify your employee email",
		"Our email system is being migrated to a new server. To maintain access, please verify your credentials at the link below within 4 hours.",
	},
	{
		"HR Portal: Benefits enrollment requires re-authentication",
		"The benefits enrollment portal requires you to re-enter your credentials due to a system update. Please log in within 48 hours to avoid losing your current benefits selections.",
	},
	{
		"Webmail upgrade: Confirm your login credentials",
		"We are upgrading the corporate webmail system to improve performance. To ensure uninterrupted service, please confirm your email address and password at the link below before the migration is complete.",
	},
	{
		"SSO Login: Session expired for all users",
		"A system-wide SSO session reset has occurred. All users must re-authenticate by entering their corporate username and password at the link below. Accounts not re-authenticated within 6 hours will be disabled.",
	},

	// Fake notification attacks
	{
		"Shared document: Annual_Budget_2026.xlsx",
		"A document has been shared with you. Click below to view. You may need to sign in with your corporate credentials to access the file.",
	},
	{
		"Voicemail from unknown caller",
		"You have a new voicemail from +1-555-0199. Duration: 2 minutes 34 seconds. Click here to listen to the message. Login required.",
	},
	{
		"Fax received: 3 pages from 555-0134",
		"You have received a fax. Please download the attached document to view. Open the file and enable content to see the fax.",
	},
	{
		"Microsoft Teams: Missed chat from your manager",
		"You have an unread message from your manager in Microsoft Teams. The message is marked as urgent. Click below to view the conversation. Sign in with your Microsoft 365 credentials to continue.",
	},
	{
		"Calendar invitation: Confidential HR review",
		"You have a new calendar invitation for a confidential HR performance review. Click the link below to view the meeting details and accept. Authentication required to view confidential content.",
	},

	// Malware delivery
	{
		"Scanned document from Xerox",
		"Please find the attached scanned document. Open the file and enable macros to view the content properly.",
	},
	{
		"Your package could not be delivered",
		"We attempted to deliver your package but nobody was home. Please download the shipping label from the link below and bring it to your nearest post office.",
	},
	{
		"Remittance advice attached - Payment #4892",
		"Please find the attached remittance advice for payment #4892. Open the document and enable editing to view the payment details. Contact accounts receivable if the amount does not match your records.",
	},
	{
		"Scanned image from HP printer",
		"You have received a scanned image from device HP-LaserJet-4200 in the third floor copy room. Please see the attached file. Note: you may need to enable content to view the scan properly.",
	},
}
