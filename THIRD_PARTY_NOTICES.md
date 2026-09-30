# Third-party components

The build downloads the official Khronos OpenXR Android loader and headers,
version **1.1.53**, from Maven Central. They are not committed to this repository.

- Source: https://github.com/KhronosGroup/OpenXR-SDK/tree/release-1.1.53
- Artifact: `org.khronos.openxr:openxr_loader_for_android:1.1.53`
- License: Apache-2.0, as supplied in the artifact's `META-INF/LICENSE`.
- AAR SHA256: `8495a9cb5f792d53effbee25825557227dee2192f15455fbd2214037d0d3b97c`

The APK includes `libopenxr_loader.so`. The build copies its full license into
`dist/OpenXR-LICENSE.txt`; this notice accompanies the published APK.

Android NDK NativeActivity glue and the static C++ runtime are compiled into
the demo. Their license notices are distributed alongside the APK in
`Android-NDK-NOTICE.txt`. Android SDK/NDK and JDK tools must be installed
separately and are not included in this repository.
