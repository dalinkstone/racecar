#include "data.h"

/* ---------------------------------------------------------------------------
 * SAFE EMAILS - Legitimate business and personal correspondence
 * ---------------------------------------------------------------------------*/
const sample_email_t SAFE_EMAILS[] = {
    /* Meeting invitations and calendar updates */
    {
        "Weekly Team Standup - Monday 9am",
        "Hi team, Just a reminder that our weekly standup is tomorrow at 9am in Conference Room B. Please have your sprint updates ready. Thanks, Sarah"
    },
    {
        "Re: Q3 Planning Meeting Reschedule",
        "Hi everyone, The Q3 planning session has been moved to Thursday at 2pm due to a conflict with the all-hands. Same conference room, agenda unchanged. Please update your calendars."
    },
    {
        "Calendar Update: Design Review moved to 3pm",
        "The design review for the new dashboard has been shifted to 3pm today. We will still meet in the Maple Room on the 4th floor. Apologies for the short notice."
    },
    {
        "Invitation: Architecture Deep Dive - Friday 11am",
        "You are invited to an architecture deep dive session this Friday at 11am. We will review the proposed microservices migration plan and discuss trade-offs. Dial-in link attached for remote attendees."
    },
    {
        "Monthly All-Hands - March 28 at 10am",
        "Please join us for the March all-hands meeting. The CEO will share quarterly results and the product team has exciting updates on the roadmap. Light refreshments will be served in the lobby."
    },

    /* Project status updates and sprint reviews */
    {
        "Sprint 14 Review - Summary and Action Items",
        "Hi team, Sprint 14 is now complete. We delivered 34 out of 38 story points. The remaining items have been carried over to Sprint 15. Full velocity chart and burndown are in Jira."
    },
    {
        "Project Atlas: Status Update - Week 12",
        "Development is on track for the beta milestone. The API layer is feature-complete and QA has begun regression testing. We identified two blockers related to database connection pooling; fixes are in progress."
    },
    {
        "Re: Backend Migration Progress",
        "Good news. The data migration scripts ran successfully on staging last night with zero errors. We are planning the production cutover for next weekend. I will send a detailed runbook by Thursday."
    },
    {
        "Feature Freeze Reminder - Release 4.2",
        "A reminder that feature freeze for Release 4.2 is this Friday at 5pm. Any unmerged feature branches will be deferred to the next cycle. Please coordinate with your leads if you need an exception."
    },
    {
        "Quarterly Roadmap Update - Engineering",
        "Attached is the updated engineering roadmap for Q2. Key priorities include the authentication overhaul, performance improvements to the search service, and the mobile SDK release. Please review and bring questions to the planning session."
    },

    /* Code review requests and PR notifications */
    {
        "PR #1247: Refactor user authentication module",
        "Hey, I have opened a pull request to refactor the authentication module. The main changes simplify token refresh logic and add unit tests for edge cases. Could you take a look when you have a chance?"
    },
    {
        "Code Review Request: Payment Gateway Integration",
        "I have submitted the payment gateway integration for review. The PR includes Stripe webhook handling and retry logic for failed charges. Tests are passing in CI. Let me know if the error handling approach looks reasonable."
    },
    {
        "Re: PR #892 - Feedback Addressed",
        "Thanks for the review comments. I have addressed all the feedback: renamed the variables for clarity, added the missing null check, and updated the documentation. Ready for another look whenever you are free."
    },
    {
        "Merged: Database indexing improvements",
        "PR #1103 has been merged to main. The new composite indexes on the orders table should reduce query times for the reporting dashboard. Monitor Datadog for any unexpected changes in DB load."
    },

    /* Team lunch and social plans */
    {
        "Team Lunch This Friday - Voting on Restaurant",
        "Hi everyone, We are planning a team lunch this Friday to celebrate the product launch. Please vote on the restaurant options in the Slack poll by end of day Wednesday. Lunch is on the company."
    },
    {
        "Happy Hour Tomorrow at 5pm",
        "Join us for a casual happy hour tomorrow at The Brass Tap, just a few blocks from the office. Drinks and appetizers covered for the first round. Partners and friends welcome."
    },
    {
        "Volunteering Day Sign-Up",
        "Our annual volunteering day is April 10th. We are partnering with the local food bank and Habitat for Humanity. Please sign up on the intranet by March 30th so we can plan transportation."
    },

    /* IT system maintenance notifications */
    {
        "Scheduled Maintenance: VPN Service - Saturday 2am-6am",
        "The IT team will perform scheduled maintenance on the VPN gateway this Saturday from 2am to 6am EST. Remote access will be intermittently unavailable during this window. No action required on your part."
    },
    {
        "Email System Upgrade - Brief Downtime Expected",
        "We are upgrading the email server to improve performance and security. Expect a 15-minute outage around 11pm tonight. Emails sent during the window will be queued and delivered once the upgrade completes."
    },
    {
        "New Software Deployment: Endpoint Protection Update",
        "The IT security team will push an update to the endpoint protection agent on all workstations this week. Your machine may restart automatically. Please save your work before leaving for the day."
    },

    /* Order confirmations and shipping updates */
    {
        "Order Confirmed: Office Supplies #ORD-88421",
        "Your order for 3 boxes of printer paper, a toner cartridge, and 2 packs of sticky notes has been confirmed. Estimated delivery is March 26th. You can track your order on the procurement portal."
    },
    {
        "Shipping Update: Your Package is Out for Delivery",
        "Good news! Your package containing the ergonomic keyboard you ordered is out for delivery today. Expected arrival between 10am and 2pm. Someone will need to sign for it at the front desk."
    },
    {
        "Receipt: AWS Monthly Invoice - February 2026",
        "Your AWS invoice for February 2026 is available. Total charges: $4,328.17. This reflects a 12% decrease from January due to the reserved instance savings. The invoice is attached for your records."
    },

    /* Customer support responses */
    {
        "Re: Support Ticket #44219 - Export Feature Issue",
        "Hi Mark, Thank you for reporting the CSV export issue. Our engineering team has identified the root cause and a fix will be included in next week's release. As a workaround, you can use the JSON export option in the meantime."
    },
    {
        "Your Support Request Has Been Resolved",
        "Hi, This is to confirm that your support request regarding login issues has been resolved. The problem was caused by a cached session token. Clearing your browser cache should prevent recurrence. Please reopen the ticket if the issue persists."
    },

    /* Internal memos and announcements */
    {
        "Announcement: New Parental Leave Policy",
        "We are pleased to announce an updated parental leave policy effective April 1st. All full-time employees are now eligible for 16 weeks of paid leave. Details are available on the HR portal."
    },
    {
        "Office Move Update - New Floor Plan Available",
        "The office relocation to the 8th floor is scheduled for the weekend of April 15th. The new floor plan is posted on the intranet. Please pack your personal items in the provided boxes by Friday afternoon."
    },
    {
        "Reminder: Expense Report Deadline is March 31",
        "All expense reports for Q1 must be submitted through Concur by March 31st. Receipts over $25 must be attached. Late submissions may be deferred to the next reimbursement cycle."
    },

    /* Onboarding welcome messages */
    {
        "Welcome to the Team, Alex!",
        "Hi Alex, Welcome aboard! Your laptop and badge are ready at the front desk. Your onboarding buddy is Jamie from the platform team. Check your calendar for orientation sessions this week. We are excited to have you."
    },
    {
        "Onboarding Checklist - Your First Week",
        "Hi, Here is your first-week checklist: complete the HR paperwork on BambooHR, set up your development environment using the wiki guide, and schedule 1:1s with your manager and skip-level. Reach out if you need any help."
    },

    /* Vacation and PTO notifications */
    {
        "Out of Office: March 25 - March 29",
        "Hi team, I will be out of office next week for a family vacation. Jake will cover any urgent items. I will have limited email access but will respond to critical issues. Thanks for your understanding."
    },
    {
        "PTO Approval Confirmation",
        "Your PTO request for April 3rd through April 7th has been approved. Please make sure to update your status in Slack and set up an out-of-office auto-reply before you leave. Enjoy the time off."
    },

    /* Bug reports and issue assignments */
    {
        "Bug Report: Dashboard Charts Not Loading on Safari",
        "We have received reports that the analytics dashboard charts fail to render on Safari 17.2. The issue appears related to a WebGL context creation failure. Assigning to the front-end team for investigation. Priority: Medium."
    },
    {
        "Issue Assigned: Memory Leak in Background Worker",
        "A memory leak has been detected in the background job worker process. Heap usage grows by approximately 50MB per hour under normal load. This has been assigned to you. Heap dumps from production are attached."
    },
    {
        "Re: Flaky Integration Test in CI Pipeline",
        "I traced the flaky test to a race condition in the database setup fixture. The fix is to use a transaction-scoped connection instead of a shared pool. PR incoming shortly."
    },

    /* Documentation updates */
    {
        "API Documentation Updated for v3.1 Endpoints",
        "The API docs have been updated to reflect the new v3.1 endpoints, including the batch processing API and the revised rate limiting headers. Please review and let me know if anything is unclear or missing."
    },
    {
        "Wiki Update: Deployment Runbook Revised",
        "The deployment runbook on Confluence has been revised to include the new canary deployment steps and rollback procedures. All on-call engineers should review the updated version before their next rotation."
    },

    /* Performance review scheduling */
    {
        "Performance Review: Self-Assessment Due April 5",
        "Hi, Performance review season is here. Please complete your self-assessment in Workday by April 5th. Your manager review meeting is tentatively scheduled for the week of April 14th. Reach out to HR with questions."
    },
    {
        "Reminder: 360 Feedback Requests Due This Friday",
        "This is a reminder to complete any outstanding 360 feedback requests by Friday. Your input is valuable and helps your colleagues grow. The process takes about 10 minutes per person."
    },

    /* Travel itinerary confirmations */
    {
        "Travel Confirmation: NYC Client Visit - April 8-10",
        "Your travel has been booked. Flight: UA 442 departing SFO at 7:15am on April 8th. Hotel: Marriott Marquis, Times Square, 2 nights. Ground transportation arranged from JFK. Itinerary details are in Concur."
    },
    {
        "Conference Registration Confirmed: KubeCon EU 2026",
        "Your registration for KubeCon EU in Paris has been confirmed. The conference runs April 21-24. Your hotel and flights are booked through the corporate travel portal. Please submit any additional expenses for pre-approval."
    },

    /* Invoice and expense report submissions */
    {
        "Invoice Submitted: Freelance Design Work - March",
        "Hi, I have submitted my invoice for March design work totaling $3,200. It covers the landing page redesign, icon set, and brand guideline updates. Payment terms are net 30 as per our agreement."
    },
    {
        "Expense Report Approved: Client Dinner - $247.50",
        "Your expense report for the client dinner on March 15th has been approved. The reimbursement of $247.50 will be included in your next paycheck. Thank you for entertaining our partners."
    },
    {
        "Re: Purchase Order for New Monitors",
        "The purchase order for 12 new 27-inch monitors has been approved by finance. Expected delivery is next Wednesday. IT will coordinate the setup with each recipient. Please confirm your desk location on the shared spreadsheet."
    },
    {
        "Board Meeting Minutes - March 2026",
        "Attached are the minutes from the March board meeting. Key decisions include approval of the Series C funding timeline and the new VP of Engineering hire. Please treat this as confidential until the official announcement."
    },
    {
        "Re: Updated CI/CD Pipeline Configuration",
        "I have updated the CI/CD pipeline to include the new static analysis step. Build times increased by about 90 seconds but we are now catching type errors before they hit staging. The config changes are in the infra repo."
    },
    {
        "Lunch and Learn: Intro to Rust - Thursday 12pm",
        "This week's lunch and learn session covers an introduction to Rust for systems programming. Bring your laptop if you want to follow along with the exercises. Pizza will be provided."
    },
};
const int SAFE_EMAIL_COUNT = sizeof(SAFE_EMAILS) / sizeof(SAFE_EMAILS[0]);

/* ---------------------------------------------------------------------------
 * SPAM EMAILS - Unsolicited marketing, scams, and junk mail
 * ---------------------------------------------------------------------------*/
const sample_email_t SPAM_EMAILS[] = {
    /* Weight loss and diet pills */
    {
        "Lose 30 Pounds in 30 Days - GUARANTEED!!!",
        "Scientists HATE this one weird trick! Our revolutionary SlimMax formula melts belly fat while you sleep. No diet, no exercise needed! Order now and get 60% OFF plus FREE shipping. Limited time only!!!"
    },
    {
        "NEW Keto Breakthrough - Drop 5 Sizes FAST",
        "Are you tired of stubborn fat? Our KETO ULTRA pills have helped over 2 MILLION people shed unwanted weight. Take 2 capsules a day and watch the pounds DISAPPEAR. Click here for your FREE trial bottle!"
    },
    {
        "Dr. Oz Recommended Fat Burner - 80% OFF TODAY",
        "This miracle garcinia cambogia extract was featured on national TV! Burns fat 300% faster than diet and exercise alone. Supplies are running out - ORDER NOW before we sell out again!"
    },

    /* Get rich quick schemes */
    {
        "Make $5,000/Day From Your Couch!",
        "I was broke and desperate until I discovered this SECRET SYSTEM. Now I make $5,000 EVERY SINGLE DAY from my laptop. No experience needed! Watch my FREE video to learn how you can too. Only 50 spots left!"
    },
    {
        "MILLIONAIRE REVEALS His Secret Formula",
        "Self-made millionaire is GIVING AWAY his wealth-building blueprint for FREE. This system has created over 300 new millionaires this year alone. Don't miss your chance - download the guide NOW before it's taken down!"
    },
    {
        "Earn $500/hour with this SIMPLE trick",
        "Top earners don't want you to know about this income loophole. Our members are banking $500+ per hour with just a smartphone. Start today with ZERO investment. Your first check could arrive this week!"
    },

    /* Lottery and sweepstakes */
    {
        "CONGRATULATIONS! You've Won $1,000,000!!!",
        "Dear Lucky Winner, Your email address was selected in our INTERNATIONAL LOTTERY DRAW. You have won ONE MILLION DOLLARS! To claim your prize, reply with your full name, address, and bank details. Act within 48 hours or your prize will be forfeited!"
    },
    {
        "You Have Been Selected - $250,000 Cash Prize",
        "You are one of 5 lucky winners chosen from 3 million entries in our annual sweepstakes! Your cash prize of $250,000 is waiting. Simply pay the $49.99 processing fee to release your funds. Claim now!"
    },
    {
        "RE: Your Pending Prize Claim - URGENT",
        "This is your FINAL NOTICE. You have an unclaimed prize of $500,000 from our promotional draw. If you do not respond within 24 hours, your winnings will be transferred to an alternate winner. Reply IMMEDIATELY."
    },

    /* Amazing product deals */
    {
        "UNBELIEVABLE! iPhone 16 Pro for only $29.99!!!",
        "WAREHOUSE CLEARANCE SALE! Brand new iPhone 16 Pro available for just $29.99! We bought surplus stock and are passing the savings to YOU. Only 100 units left. Order before midnight tonight!!!"
    },
    {
        "Designer Watches - 95% OFF Retail Price!",
        "Get AUTHENTIC Rolex, Omega, and Breitling watches at 95% OFF retail prices! Our wholesale connections make this possible. FREE shipping worldwide. Browse our exclusive collection now - sale ends Sunday!"
    },
    {
        "Ray-Ban Sunglasses - ALL STYLES only $19.99",
        "HUGE BLOWOUT SALE on Ray-Ban sunglasses! Every style, every color - just $19.99. That's up to 90% OFF the retail price. Perfect for summer! Buy 2 get 1 FREE. Shop now before stock runs out!"
    },

    /* Fake invoice/receipt */
    {
        "Your Order #8847231 Has Been Confirmed - $499.99",
        "Thank you for your purchase! Your order for a Samsung 65-inch Smart TV ($499.99) has been confirmed. If you did NOT place this order, click here immediately to cancel and request a refund. Your card will be charged within 24 hours."
    },
    {
        "Receipt for Your Recent Purchase - $299.00",
        "Your payment of $299.00 to DIGITAL GOODS STORE has been processed. Transaction ID: TXN-99281. If this was not you, click the button below to dispute this charge and secure your account immediately."
    },

    /* Adult content solicitation */
    {
        "Hot Singles in Your Area Want to Meet YOU",
        "Thousands of attractive singles are waiting to chat with you RIGHT NOW. Create your FREE profile in 30 seconds and start browsing. No credit card required to sign up. Join the fun tonight!"
    },
    {
        "You Have 5 New Messages from Local Matches!",
        "Five people near your location viewed your profile and want to connect! Don't keep them waiting. Upgrade to Premium for unlimited messaging. Special intro rate: only $9.99/month. See who's interested!"
    },

    /* Cryptocurrency investment */
    {
        "NEW Crypto Coin Set to EXPLODE 10,000% !!!",
        "INSIDER ALERT: This new cryptocurrency is about to skyrocket! Early investors in Bitcoin made millions and now it's YOUR turn. Invest just $250 and watch it grow to $100K+. Don't miss the next big thing!"
    },
    {
        "Bitcoin Trading Bot - 97% Win Rate GUARANTEED",
        "Our AI-powered trading bot has a VERIFIED 97% win rate! Set it up in 5 minutes and start earning passive income from Bitcoin trading. Minimum deposit just $250. Join 50,000+ profitable traders today!"
    },
    {
        "Elon Musk's SECRET Crypto Investment Revealed!",
        "Breaking news: Elon Musk has reportedly invested $1.5 BILLION in a new cryptocurrency platform. Smart investors are rushing to get in before the public announcement. Start with as little as $100. Act NOW!"
    },

    /* Work from home schemes */
    {
        "Work From Home - Earn $3,000/Week Stuffing Envelopes!",
        "Legitimate work-from-home opportunity! Earn up to $3,000 per week stuffing envelopes for major corporations. No experience necessary. We provide all materials. Send $29.95 for your starter kit today!"
    },
    {
        "Hiring NOW: Data Entry Workers - $45/Hour!",
        "Major companies desperately need data entry workers. Work from home, set your own hours, earn $45/hour! No interview required. Just pay the $19.95 registration fee and start earning tomorrow. Positions filling fast!"
    },
    {
        "MOM Makes $8,000/Month From Home - Here's How",
        "This stay-at-home mom discovered a simple online system that pays her $8,000 every month! She works just 2 hours a day. The SECRET is finally revealed. Watch the free presentation before it expires!"
    },

    /* Insurance and loan offers */
    {
        "YOU QUALIFY! Refinance at 1.9% - Lowest Rates EVER",
        "Mortgage rates just hit HISTORIC LOWS! You could save $400/month by refinancing now. No income verification, no appraisal needed. Bad credit OK! Get your FREE quote in 60 seconds - no obligation."
    },
    {
        "Pre-Approved: $50,000 Personal Loan at 0% Interest!",
        "Great news! Based on your profile, you have been PRE-APPROVED for a $50,000 personal loan at 0% interest for 12 months! No collateral needed. Funds deposited in 24 hours. Apply now - offer expires Friday!"
    },
    {
        "Life Insurance - Only $5/Month! No Medical Exam!",
        "Protect your family for just $5/month! Get up to $500,000 in life insurance coverage with NO medical exam required. Guaranteed acceptance for ages 18-80. Get your FREE quote in under 2 minutes!"
    },

    /* Fake survey rewards */
    {
        "Complete This 2-Minute Survey and Win a $500 Gift Card!",
        "You've been selected to participate in our customer satisfaction survey! It only takes 2 minutes and you'll receive a $500 Amazon gift card as a thank you. Limited to the first 1,000 respondents. Start now!"
    },
    {
        "Your Opinion is Worth $100 - Take Our Quick Survey",
        "We value your feedback! Complete our 5-question survey about your shopping habits and receive a $100 Visa prepaid card. Over $1 million in rewards given out this month. Claim yours today!"
    },

    /* Miracle health cures */
    {
        "CURE Diabetes Naturally in Just 14 Days!",
        "Big Pharma doesn't want you to see this! A simple kitchen ingredient can REVERSE Type 2 diabetes in just 14 days. Over 47,000 people have already been cured. Watch the banned video before it's removed!"
    },
    {
        "Joint Pain GONE in 7 Days - Doctors Are Shocked!",
        "A revolutionary new supplement eliminates joint pain, stiffness, and inflammation in just 7 days. Made from a rare Amazonian plant extract. Clinical trials show 98% effectiveness! Order risk-free today."
    },
    {
        "This Ancient Remedy Restores 20/20 Vision!",
        "Throw away your glasses! This ancient remedy used by Himalayan monks restores perfect vision naturally. Optometrists are furious! Over 127,000 people have already improved their eyesight. Try it FREE for 30 days."
    },

    /* You've been selected offers */
    {
        "EXCLUSIVE: You've Been Selected for a VIP Membership!",
        "Congratulations! Out of millions of candidates, YOU have been hand-picked for our exclusive VIP rewards program. Enjoy luxury travel deals, cashback offers, and celebrity event access. Activate your membership now for just $1!"
    },
    {
        "You Are Eligible for a FREE Government Grant - $25,000!",
        "The US Government is giving away billions in FREE grant money and you may qualify for up to $25,000! No repayment required. Use it for anything - bills, education, home improvement. Check your eligibility now!"
    },

    /* Discount pharmaceutical spam */
    {
        "PHARMACY SALE: 80% OFF All Medications - No Rx Needed!",
        "Canadian pharmacy blowout sale! Get 80% OFF Viagra, Cialis, Xanax, Ambien, and more. No prescription required! Discreet shipping worldwide. Same FDA-approved medications at a fraction of the cost. Order now!"
    },
    {
        "Save 90% on Prescription Medications - Order Online TODAY",
        "Why pay full price? Our licensed online pharmacy offers ALL medications at 90% below US prices. Fast, discreet delivery. No embarrassing doctor visits. Satisfaction guaranteed or your money back."
    },

    /* Binary options trading */
    {
        "Binary Options: Turn $200 into $5,000 in ONE WEEK!",
        "Professional traders are making THOUSANDS daily with our binary options platform. Our signals have an 89% accuracy rate! Start with just $200 and watch your account grow. Free training for new members!"
    },
    {
        "FOREX Trading SECRET - $10K/Day on Autopilot!",
        "Our proprietary forex algorithm generates $10,000+ per day on complete autopilot. Just set it and forget it! Join our exclusive trading group and start profiting today. Limited spots available!"
    },

    /* Cable/streaming offers */
    {
        "Get 500+ Premium Channels for ONLY $9.99/Month!!!",
        "Cut the cord and save HUNDREDS! Get over 500 premium channels including HBO, Showtime, and all sports packages for just $9.99/month. No contract, no hidden fees! Activate your account instantly!"
    },
    {
        "FREE Netflix for LIFE - Limited Time Promotion!",
        "Netflix is giving away FREE LIFETIME subscriptions to celebrate 30 million subscribers! You have been randomly selected. Click below to activate your free account. Only 5,000 codes remaining!"
    },

    /* Additional spam variety */
    {
        "URGENT: Your Unclaimed Package from Amazon",
        "We attempted to deliver your package but could not reach you. As compensation, you are entitled to a FREE mystery gift box worth up to $500. Claim your box by completing a short verification. Offer expires tonight!"
    },
    {
        "Congratulations! Your Email Won Our Daily Drawing!",
        "Your email address was randomly selected as today's winner of $10,000! This is NOT a joke. To receive your cash prize via wire transfer, simply confirm your identity and pay the small $39.95 handling fee."
    },
    {
        "LAST CHANCE: Claim Your Free Solar Panel Installation",
        "Homeowners in your area are getting solar panels installed for FREE thanks to a new government rebate program! Zero out-of-pocket cost, save $200/month on electricity. Only 15 installations left in your zip code!"
    },
    {
        "YOU WON a Brand New Tesla Model 3!!!",
        "Your entry in our holiday giveaway was drawn as the GRAND PRIZE WINNER! You've won a 2026 Tesla Model 3. To schedule your vehicle pickup, verify your identity and pay the $199 title transfer fee. Claim now!"
    },
    {
        "Anti-Aging Breakthrough: Look 20 Years Younger!",
        "Hollywood celebrities are OBSESSED with this new anti-aging serum! One application erases wrinkles, dark spots, and sagging skin. Dermatologists call it a miracle. Get your FREE sample - just pay $4.95 shipping!"
    },
    {
        "ATTENTION: You Have a Tax Refund of $3,478.00 Waiting!",
        "Our records indicate you have an unclaimed tax refund of $3,478.00 from the previous filing year. To deposit the refund directly to your bank account, complete the secure refund claim form. Expires in 72 hours!"
    },
};
const int SPAM_EMAIL_COUNT = sizeof(SPAM_EMAILS) / sizeof(SPAM_EMAILS[0]);

/* ---------------------------------------------------------------------------
 * ATTACK EMAILS - Phishing, social engineering, and malware delivery
 * ---------------------------------------------------------------------------*/
const sample_email_t ATTACK_EMAILS[] = {
    /* Fake password reset requests */
    {
        "URGENT: Your Password Expires in 24 Hours",
        "Your corporate email password will expire in 24 hours. To avoid losing access to your account, you must reset your password immediately by clicking the link below. Failure to act will result in account lockout. [malicious-link]"
    },
    {
        "Action Required: Unusual Sign-In Activity Detected",
        "We detected a sign-in attempt from an unrecognized device in Moscow, Russia. If this was not you, secure your account immediately by resetting your password at the link below. [malicious-link]"
    },
    {
        "Microsoft 365: Password Reset Required",
        "Your Microsoft 365 password has been compromised in a recent data breach. For your security, please reset your password within 12 hours using the secure link below. Your account will be suspended if no action is taken. [malicious-link]"
    },
    {
        "Your Google Account Password Was Changed",
        "Someone recently changed the password for your Google Account. If this was you, no action is needed. If you did NOT change your password, your account may be compromised. Click here to secure your account now. [malicious-link]"
    },

    /* CEO fraud and wire transfer requests */
    {
        "Urgent: Wire Transfer Needed Today",
        "Hi, I need you to process a wire transfer of $47,500 to a new vendor immediately. This is for a confidential acquisition and should not be discussed with others. I am in a board meeting and cannot take calls. Please confirm when done. - CEO"
    },
    {
        "Confidential: Immediate Action Required",
        "I need your help with an urgent and confidential matter. We are closing a deal today and need to wire $85,000 to the following account. Time-sensitive, so please process this within the hour. I will explain more later. Do not discuss with anyone."
    },
    {
        "Re: Payment for Consulting Services",
        "Hi, Following up on our conversation, please wire $32,000 to the account below for the consulting engagement. This has been approved at the executive level. Please do not delay as the vendor is waiting. Bank details: [attacker-controlled account info]"
    },

    /* Fake bank and PayPal security alerts */
    {
        "PayPal: Your Account Has Been Limited",
        "We have noticed suspicious activity on your PayPal account. Your ability to send and receive payments has been temporarily limited. To restore full access, please verify your identity by logging in through the secure link below. [malicious-link]"
    },
    {
        "Bank of America: Unauthorized Transaction Alert",
        "A transaction of $2,847.00 was attempted on your Bank of America account from an unrecognized device. If you did not authorize this transaction, click below immediately to block further activity and secure your account. [malicious-link]"
    },
    {
        "Chase: Verify Your Account Information",
        "Dear Chase customer, We were unable to verify your account information during our routine security update. Your account will be suspended within 48 hours unless you verify your details through our secure portal. [malicious-link]"
    },
    {
        "Wells Fargo: Important Security Update Required",
        "Your Wells Fargo Online Banking access has been temporarily locked due to failed login attempts. To unlock your account, please verify your identity using the secure link below. If you do not respond within 24 hours, your account will be permanently locked. [malicious-link]"
    },

    /* IRS and tax scams */
    {
        "IRS Notice: Unpaid Tax Balance - Immediate Action Required",
        "According to our records, you have an outstanding tax liability of $14,328.00 for the 2025 fiscal year. Failure to remit payment within 5 business days will result in wage garnishment and legal proceedings. Submit payment via the secure portal below. [malicious-link]"
    },
    {
        "URGENT: IRS Criminal Investigation Division - Case #2026-44812",
        "This is a formal notice that a federal tax lien has been filed against you for unpaid taxes totaling $23,450.00. A warrant for your arrest will be issued if payment is not received by March 28, 2026. Call our office immediately or pay online at [malicious-link]."
    },
    {
        "Tax Refund Notification - eFile Confirmation Required",
        "The IRS has approved your federal tax refund of $4,218.00. To receive your refund via direct deposit, please confirm your banking information and Social Security number through our secure eFile portal. [malicious-link]"
    },

    /* Package delivery failed */
    {
        "FedEx: Delivery Attempt Failed - Action Required",
        "We attempted to deliver your package (Tracking #7291-8834-4421) but no one was available to sign. To reschedule delivery or pick up at a nearby location, please click the link below. [malicious-link]"
    },
    {
        "UPS: Your Package Could Not Be Delivered",
        "Your UPS shipment (1Z999AA10123456784) could not be delivered due to an incomplete address. Please review and update your shipping details to avoid return to sender. Download the shipping label correction form attached. [malicious-attachment]"
    },
    {
        "USPS: Package Held at Distribution Center",
        "A package addressed to you is being held at our distribution center due to unpaid customs fees of $3.95. To release your shipment and schedule delivery, please submit payment through our online portal. [malicious-link]"
    },

    /* Account verification urgency */
    {
        "Verify Your Account Within 24 Hours or It Will Be Deleted",
        "Due to our updated Terms of Service, all accounts must be re-verified. If your account is not verified within 24 hours, it will be permanently deleted along with all associated data. Verify now: [malicious-link]"
    },
    {
        "Your Apple ID Has Been Locked",
        "Your Apple ID has been locked for security reasons. Someone attempted to access your account from an unrecognized device. To unlock your account and prevent unauthorized access, verify your identity immediately. [malicious-link]"
    },
    {
        "LinkedIn: Unusual Activity on Your Account",
        "We have detected multiple failed login attempts on your LinkedIn account from different geographic locations. For your protection, we have temporarily restricted your account. Verify your identity to restore access. [malicious-link]"
    },

    /* Fake IT department requests */
    {
        "IT Department: Mandatory Security Verification",
        "As part of our quarterly security audit, all employees must verify their network credentials by end of day today. Please click the link below and enter your username and password to confirm your account. Non-compliance will result in access revocation. [malicious-link]"
    },
    {
        "IT Helpdesk: Email Migration - Credentials Required",
        "We are migrating all email accounts to a new server. To ensure a seamless transition, please reply to this email with your current username and password. Your credentials are needed to transfer your mailbox and settings."
    },
    {
        "VPN Configuration Update - Action Needed",
        "The IT security team requires all remote workers to re-authenticate their VPN access due to a configuration change. Please visit the link below and enter your corporate credentials to update your VPN profile. Failure to do so by 5pm will result in VPN access being revoked. [malicious-link]"
    },

    /* Shared document phishing */
    {
        "John Smith shared a document with you",
        "John Smith has shared a Google Docs document with you: 'Q1 Revenue Report - Confidential'. Click the link below to view the document. You may need to sign in with your Google account to access it. [malicious-link]"
    },
    {
        "New Document Shared via SharePoint: 'Employee Salary Data 2026'",
        "A new document has been shared with you through Microsoft SharePoint. Document: 'Employee Salary Data 2026.xlsx'. Click below to access the file. You will need to authenticate with your Microsoft 365 credentials. [malicious-link]"
    },
    {
        "Dropbox: Someone Shared a File With You",
        "Sarah Johnson has shared the file 'Project_Financials_Confidential.pdf' with you on Dropbox. To view and download this file, click the link below and sign in to your Dropbox account. [malicious-link]"
    },

    /* Compromised account warnings */
    {
        "YOUR ACCOUNT WAS ACCESSED FROM NORTH KOREA",
        "SECURITY ALERT: Your email account was accessed from Pyongyang, North Korea at 3:42 AM today. If this was not you, someone has your password. Immediately change your password and verify your recovery email using the link below. [malicious-link]"
    },
    {
        "Data Breach Alert: Your Credentials Have Been Leaked",
        "Your email and password were found in a data breach of 3.2 million accounts. Your credentials are being sold on the dark web. Change your password immediately through our secure password reset tool to protect your accounts. [malicious-link]"
    },

    /* Tech support scams */
    {
        "Microsoft Support: Critical Security Vulnerability Detected",
        "Our automated scan detected a critical vulnerability on your Windows PC that exposes your personal data. Call our certified Microsoft technicians immediately at 1-800-XXX-XXXX or download the security patch below. Your computer may already be infected. [malicious-link]"
    },
    {
        "Your Computer Has Been Infected - Immediate Action Required",
        "WARNING: Our network monitoring system has detected malware on your corporate workstation. To avoid data loss and network contamination, download and run the attached security tool immediately. Your IT administrator has been notified. [malicious-attachment]"
    },

    /* Fake invoice with malware attachment */
    {
        "Invoice #INV-29481 Attached - Payment Due",
        "Please find attached the invoice for services rendered in February. Total amount due: $12,750.00. Payment is due within 15 business days. If you have questions about this invoice, please contact our billing department. [malicious-attachment: Invoice_29481.pdf.exe]"
    },
    {
        "Purchase Order #PO-7841 - Please Review and Approve",
        "Hi, Attached is the purchase order for the equipment discussed last week. Please review the details and sign the document to proceed. The vendor requires confirmation by end of day. [malicious-attachment: PO-7841.docm]"
    },
    {
        "Overdue Invoice - Final Notice Before Collections",
        "This is a final notice regarding your overdue invoice of $8,295.00 (Invoice #38291). If payment is not received within 48 hours, your account will be referred to collections. Review the attached invoice and remit payment immediately. [malicious-attachment: Invoice_38291.zip]"
    },

    /* Employment offer scams */
    {
        "Job Offer: Remote Position - $95/hour - Immediate Start",
        "Congratulations! Based on your resume on Indeed, we would like to offer you a remote position paying $95/hour. To proceed with onboarding, please fill out the attached employment form with your Social Security number, bank details for direct deposit, and a copy of your ID. [malicious-link]"
    },
    {
        "You've Been Recruited by Google!",
        "The Google Talent Acquisition team has identified you as a top candidate for a Senior Engineer role. Salary range: $280,000-$350,000 + equity. To schedule your interview, please complete the pre-screening form at the link below, including your current compensation details. [malicious-link]"
    },

    /* Legal threat and lawsuit scams */
    {
        "Legal Notice: Copyright Infringement Complaint Filed Against You",
        "A formal copyright infringement complaint has been filed against you under the DMCA. You have 48 hours to review the complaint and respond before legal proceedings begin. Review the full complaint and evidence at the link below. [malicious-link]"
    },
    {
        "NOTICE OF LEGAL ACTION - Case #2026-CR-88412",
        "You are hereby notified that a civil lawsuit has been filed against you in federal court. Failure to respond within 5 days will result in a default judgment. Download the court summons and complaint documents from the secure portal below. [malicious-link]"
    },
    {
        "Cease and Desist: Trademark Violation",
        "Our legal department has identified unauthorized use of our trademarked content on your website. You must cease all infringing activity within 72 hours or face legal action including damages up to $150,000. Review the full cease and desist notice attached. [malicious-attachment]"
    },

    /* Fake vendor payment redirects */
    {
        "Updated Banking Information for Future Payments",
        "Hi, This is to inform you that our company has changed banking providers. Effective immediately, please direct all future payments to our new account. Updated wire instructions are below. Please update your records and confirm receipt of this email."
    },
    {
        "Vendor Payment Details Change - Action Required",
        "Due to our recent corporate restructuring, our payment details have been updated. Please update your accounts payable records with the new banking information attached. All outstanding and future invoices should be paid to the new account."
    },
    {
        "Re: Invoice Payment - Updated ACH Information",
        "Following up on the pending invoice payment. Our accounting department has transitioned to a new bank. Please cancel any pending payments to the old account and redirect them using the updated ACH details below. This is effective immediately."
    },

    /* Domain and SSL expiration scams */
    {
        "URGENT: Your Domain Name Expires Tomorrow!",
        "Your domain registration for yourcompany.com expires on March 24, 2026. If you do not renew immediately, your domain will be released and could be registered by someone else. Renew now for just $29.99 at [malicious-link]."
    },
    {
        "SSL Certificate Expired - Website at Risk!",
        "Your SSL certificate for yourcompany.com has expired. Your website is now showing security warnings to all visitors, which will destroy customer trust and tank your search rankings. Renew your SSL certificate immediately: [malicious-link]"
    },
    {
        "Domain Suspension Notice - Action Required Within 24 Hours",
        "Due to a WHOIS verification failure, your domain yourcompany.com is scheduled for suspension within 24 hours. To prevent suspension, verify your domain ownership by logging in to your registrar account at the link below. [malicious-link]"
    },

    /* Additional attack variety */
    {
        "Zoom: Meeting Recording Available - Confidential HR Discussion",
        "A Zoom meeting recording has been shared with you: 'HR Confidential - Layoff Planning Q2 2026'. Click below to view the recording. You will need to sign in with your corporate credentials to access this sensitive content. [malicious-link]"
    },
    {
        "Voicemail Notification: 1 New Message from Unknown Caller",
        "You have a new voicemail message (duration: 0:47). The caller did not leave a name. To listen to your voicemail, click the play button below or download the audio file attached. [malicious-attachment: voicemail_msg_03232026.html]"
    },
    {
        "DocuSign: Complete Your Signature - NDA Agreement",
        "You have a new document to review and sign via DocuSign. Document: 'Non-Disclosure Agreement - Acquisition Target'. The sender has marked this as urgent. Click below to review and sign. [malicious-link]"
    },
    {
        "Multi-Factor Authentication Update Required",
        "Our security team is upgrading the MFA system. All employees must re-enroll their authentication devices by end of day. Click the link below to complete the re-enrollment process using your current credentials. Accounts not re-enrolled will be locked. [malicious-link]"
    },
    {
        "Slack Workspace: Suspicious Login Detected",
        "A login to your Slack workspace was detected from an unrecognized IP address (185.234.xx.xx) in Ukraine. If this was not you, immediately revoke the session and reset your password using the link below. [malicious-link]"
    },
    {
        "HR Department: Updated W-2 Form Available",
        "Your corrected W-2 form for tax year 2025 is now available. The updated form reflects adjustments to your reported compensation. Please download and review the form from the secure HR portal. You will need to verify your SSN and date of birth to access the document. [malicious-link]"
    },
    {
        "Board of Directors: Emergency Meeting Materials",
        "Please find the confidential materials for tomorrow's emergency board meeting regarding the proposed merger. These documents are highly sensitive. Access the materials using your executive portal credentials at [malicious-link]. Do not forward this email."
    },
};
const int ATTACK_EMAIL_COUNT = sizeof(ATTACK_EMAILS) / sizeof(ATTACK_EMAILS[0]);
