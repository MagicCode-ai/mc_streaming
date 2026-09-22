# MagicCode Streaming Software Third-Party Notices

**Document Version:** v1.0.0. **Release Date:** September 19, 2026.

## 1. Purpose

This document identifies third-party platforms, SDKs, and system libraries that may be required to **build or link** MagicCode Streaming Software v1.0.0. It is not a grant of rights in those platforms.

The Software itself, including `libMagicCode_streaming.a` and the public headers, is proprietary to MagicCode Technology Co., Ltd. Use is governed by the MagicCode Streaming Software End User License Agreement (EULA).

## 2. MagicCode-authored software

MagicCode authors the session API (`include/mc_streaming.h`, `src/mc_streaming.c`), closed-loop encoder, rewrite, logging, native archives, and product documentation.

Copyright and proprietary notices in the Software must not be removed, altered, or obscured.

## 3. Platform SDKs (not shipped inside the `.a`)

| Component / platform | Typical use | Notice |
|----------------------|-------------|--------|
| Android NDK / SDK | Build and link the Android archive | Google / AOSP terms. Not redistributed inside `libMagicCode_streaming.a`. |
| Apple iOS / iPadOS SDKs | Build and link the iOS archive; CoreFoundation, CoreVideo, CoreMedia | Apple Developer terms. |
| CMake, Ninja, Xcode | Build tooling | Not part of the customer runtime archive. |

Customers remain responsible for Android, Apple, and store terms that apply to **their** application.

## 4. What this SDK does not include

- GPU runtimes, Vulkan, Metal, OpenGL, or super-resolution models (those belong to MagicCode Super-Resolution Software, a different product).
- Unity or Unreal Engine plugins.

## 5. Release package requirement

Before external distribution:

1. Include this Third-Party Notices document and the EULA.
2. Do not remove copyright or proprietary notices from the Software.
3. Confirm that linking and redistributing the Software is authorized under the EULA (personal or commercial authorization as applicable).

## 6. Customer responsibilities

- Comply with the EULA and with platform or store terms for your own application.
- Store listings and app privacy forms are the integrator’s responsibility; this SDK does not upload video. Declare the technical telemetry in the Data Reporting Notice.

## 7. Contact

Questions about these notices: legal@magiccode-ai.com  
Website: www.magiccode-ai.com
