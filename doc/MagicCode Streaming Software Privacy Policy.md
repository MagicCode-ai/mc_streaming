# MagicCode Streaming Software Privacy Policy

**Effective Date:** September 19, 2026  
**Applies to:** MagicCode Streaming Software v1.0.0

This Privacy Policy ("Policy") explains how MagicCode Technology Co., Ltd. ("MagicCode", "we", "us", or "our") handles information when you use MagicCode Streaming Software ("Software"): the native session API `mc_streaming_enable` / `mc_streaming_disable` and `libMagicCode_streaming.a`.

This Policy describes the standard SDK archives. It is not an end-user consent form for your application. Integrators remain responsible for notices, store listings, and any consent their own product requires.

This product is **not** MagicCode Super-Resolution Software. Super-Resolution and Streaming each send technical telemetry to MagicCode over TCP on separate endpoints. Neither product uploads your video content.

---

## 1. Information we collect

**MagicCode Streaming Software v1.0.0 does not upload your video, YUV, or Annex-B bitstream.**

The standard SDK sends a short technical telemetry packet to MagicCode over TCP. Telemetry is a condition of the standard license. There is no public switch or environment variable to turn it off. MagicCode may provide a **non-reporting archive under separate written authorization** (for example where a jurisdiction or product category requires it).

The Software processes video **locally** in the calling process:

- Input I420 planes and the Annex-B access unit you pass to `mc_streaming_enable`.
- Internal parse, reconstruction, and encode buffers.

Those buffers are not uploaded by the SDK. MagicCode does not receive your YUV, bitstream, face images, or audio.

Telemetry fields (see the Data Reporting Notice):

- Report time
- Device / app-instance UUID (not an advertising ID)
- Device model
- SDK version
- Average per-frame processing time (milliseconds)
- Average input bitstream size
- Average output bitstream size
- Error code
- Report count

The SDK does **not** put a device or local-network IP address in the telemetry payload.

We do not collect through this SDK:

- Name, address, account, or contact details
- GPS or other precise location
- Image or video content
- Advertising identifiers (IDFA / GAID)

### 1.1 Local hardware detection

On Android, the Software may read CPU properties in-process for performance. Device model and UUID used in telemetry are described in the Data Reporting Notice.

### 1.2 Logs (stay on device or host)

If you raise `mc_streaming_set_log_level` above `MCS_LOG_NONE`, messages may be written to:

- Android: logcat, tag `MagicStreaming`
- iOS / host: stderr via the log backend

Default is `MCS_LOG_ERROR`. Logs can include encoder init flags, error codes, and similar diagnostics. Whoever can read logcat or process stderr on that device can see them. MagicCode cannot see them unless **you** capture and send logs through your own support channel, except for the separate telemetry packet in §1.

---

## 2. Purpose of use

Telemetry is used to:

- **Bill and meter licensed use** of the Software (whether a given integration is active, on which device classes, and at what volume of `mc_streaming_enable` activity)
- **Diagnose issues** (error codes, processing time, input/output size averages, SDK version, device model)
- Measure SDK quality and adoption (device mix, bitrate reduction, stability)

It is not used to reconstruct your video, show ads, build advertising profiles, or sell data.

You (the integrator) use the in-memory input and output solely to transcode H.264 as described in the Specification.

---

## 3. How we protect information

Telemetry is a fixed-size binary packet with an XOR checksum. Protection of video in your app is your responsibility (process isolation, OS permissions, whether you write bitstreams to disk, and whether you send RTP/SRTP to a third party).

---

## 4. Retention

- Billing and license-metering records: retained for the applicable license / billing period and as long as accounting or tax rules require.
- Diagnostic records: retained up to 24 months, or longer while a support or billing dispute is open.

After those periods, MagicCode deletes or aggregates the records so they no longer identify a device UUID.

---

## 5. Information disclosure

MagicCode does not sell telemetry and does not use it for advertising. We may disclose information if required by law for data we actually hold, to service providers who process telemetry for us under contract, or if you separately send logs or media to us for support.

---

## 6. Your rights

For access or deletion of telemetry tied to a device UUID, email contact@magiccode-ai.com. We will respond within 15 working days.

The standard SDK does not offer an in-app or environment-variable opt-out. If applicable law or your product category requires a non-reporting build, contact MagicCode for written authorization.

If your **application** collects personal data (accounts, tokens, call records), that processing is governed by **your** privacy policy, not this one. You must still disclose this SDK’s telemetry in your privacy materials and store listings.

---

## 7. Children

The Software is **not** licensed for integration into child-directed products (including apps that are primarily for children or that otherwise fall under COPPA or similar laws), unless MagicCode has provided a non-reporting archive under written authorization.

---

## 8. International transfers

Telemetry is sent to MagicCode’s report server over TCP. The connection’s network-level source address may appear in ordinary server logs used to operate the service; it is not a field the SDK writes into the packet.

If your users are in other regions, you must assess transfer and disclosure rules for **your** app. MagicCode can discuss a non-reporting archive under written authorization where that is required.

If you host media on your own servers, those transfers are yours.

---

## 9. Changes to this Policy

If a later SDK version changes reporting, we will update this Policy, the Data Reporting Notice, and the version number before that behavior ships.

---

## 10. Contact

Email: contact@magiccode-ai.com  
Website: www.magiccode-ai.com

We will respond within 15 working days.

---

© 2026 MagicCode Technology Co., Ltd.
