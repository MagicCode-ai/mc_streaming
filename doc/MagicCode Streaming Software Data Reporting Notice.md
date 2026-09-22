# MagicCode Streaming Software Data Reporting Notice

**Document Version:** v1.0.0. **Release Date:** September 19, 2026.

## 1. Purpose

This notice is a customer-facing summary of technical data reporting in MagicCode Streaming Software v1.0.0. Read it with the Privacy Policy. The Privacy Policy controls if there is any conflict.

Use it to fill your app privacy policy, Apple Privacy Nutrition Label, Google Play Data safety form, and (where applicable) a China SDK disclosure list.

## 2. Reporting in v1.0.0

The native SDK does **not** upload YUV, Annex-B, RTP, faces, or audio. The **standard** archive always sends a short technical telemetry packet over TCP (background thread, short-lived connection, XOR checksum), on the same pattern as MagicCode Super-Resolution.

Telemetry in the standard SDK is a **license condition**. It is used for **billing / license metering** and **issue diagnosis**, and also for SDK quality. There is no public API or environment variable to turn it off.

MagicCode may provide a **non-reporting archive under separate written authorization** (for example EU/UK products, child-directed products, or government tenders that require it). That archive is not the default SDK.

| Item | v1.0.0 standard SDK |
|------|---------------------|
| Image / video / YUV upload | No |
| Annex-B / RTP upload | No |
| Advertising ID (IDFA / GAID) | No |
| Device / local IP in the packet | No |
| Report time (client unix ms) | Yes |
| Device / app-instance UUID | Yes |
| Device model | Yes |
| SDK version | Yes |
| Average per-frame processing time (ms) | Yes |
| Average input bitstream size | Yes |
| Average output bitstream size | Yes |
| Error code | Yes |
| Report count | Yes |
| License-check ping (separate) | No |

Triggers: session init (`mc_streaming_enable` first alloc), every 1024 `mc_streaming_enable` calls, and `mc_streaming_disable`. Video content is never included.

UUID is an app-instance identifier (Android `ANDROID_ID` or a locally stored ID; iOS identifierForVendor or a locally stored ID). It is not IDFA/GAID.

`mc_streaming_get_version()` returns the local string `"1.0.0"`. Version lookup itself does not contact a server.

## 3. What may appear on the device only

- **Logs** if you set a log level: stderr or Android logcat (`MagicStreaming`). Not the telemetry channel.
- Failed report sends may log at info level.

## 4. Network requirement

Transcode still runs if the report server is unreachable (send is retried three times, then dropped). Offline devices can call `mc_streaming_enable`. Ordinary TCP server logs may show the connection’s source address; that is not a payload field.

## 5. Integrator disclosure table

Copy or adapt this into your privacy materials and store forms. Values are for **this SDK only**.

| Topic | What to declare |
|-------|-----------------|
| Data types | Device identifiers (app-instance UUID), device model, diagnostics (processing time, bitstream-size averages, error codes, SDK version, report time, report count) |
| Linked to user identity by this SDK | No (MagicCode does not receive your account) |
| Used for tracking / ads | No |
| Used for analytics | Yes (SDK quality) |
| Used for app functionality | Yes (billing / license metering, issue diagnosis) |
| Collected from children | Do not integrate the standard SDK into child-directed apps |
| Can the end user turn it off in the standard SDK | No |
| Optional / required | Required in the standard archive; a non-reporting archive exists only with written authorization from MagicCode |

If you wrap the SDK in an application that has accounts, analytics, or crash reporting (Firebase, Sentry, your Token server, LiveKit, and similar), **you** must disclose that separately.

## 6. Future versions

If a later release changes fields or triggers, the Data Reporting Notice, Privacy Policy, and Release Notes will be updated.

## 7. Relationship to other documents

| Document | Role |
|----------|------|
| Privacy Policy | Controlling description of collection, purpose, retention, and rights |
| This notice | Implementation / store-disclosure summary |
| Third-Party Notices | Platform SDKs at build/link time, not telemetry |
| EULA | Contract; §5 describes technical telemetry as a license condition |
