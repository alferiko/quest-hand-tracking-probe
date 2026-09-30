param(
    [string]$Sdk = $env:ANDROID_HOME,
    [string]$Jdk = $env:JAVA_HOME,
    [string]$NdkVersion = '27.0.12077973',
    [string]$BuildToolsVersion = '35.0.0',
    [string]$Tag = 'v0.1.0'
)
$ErrorActionPreference = 'Stop'
if (!$Sdk) { $Sdk = $env:ANDROID_SDK_ROOT }
if (!$Sdk -or !$Jdk) { throw 'Set ANDROID_HOME and JAVA_HOME, or pass -Sdk and -Jdk.' }
if ($Tag -notmatch '^[a-zA-Z0-9.-]+$') { throw 'Invalid build tag' }
$Sdk = (Resolve-Path -LiteralPath $Sdk).Path
$Jdk = (Resolve-Path -LiteralPath $Jdk).Path
$ndk = Join-Path $Sdk "ndk/$NdkVersion"
$tc = Join-Path $ndk 'toolchains/llvm/prebuilt/windows-x86_64'
$bt = Join-Path $Sdk "build-tools/$BuildToolsVersion"
$glue = Join-Path $ndk 'sources/android/native_app_glue'
foreach ($required in @("$tc/bin/clang++.exe", "$bt/aapt2.exe", "$Jdk/bin/java.exe", "$Jdk/bin/keytool.exe", "$Sdk/platforms/android-35/android.jar")) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing build dependency: $required" }
}
function Check([string]$Stage) { if ($LASTEXITCODE -ne 0) { throw "$Stage failed: $LASTEXITCODE" } }

# Official Khronos Android AAR contains matching headers and the runtime loader.
$version = '1.1.53'
$expected = '8495A9CB5F792D53EFFBEE25825557227DEE2192F15455FBD2214037D0D3B97C'
$deps = Join-Path $PSScriptRoot '.deps'
$aar = Join-Path $deps "openxr-$version.aar"
$xr = Join-Path $deps "openxr-$version"
New-Item -ItemType Directory -Force $deps | Out-Null
if (!(Test-Path -LiteralPath $aar)) {
    Invoke-WebRequest "https://repo.maven.apache.org/maven2/org/khronos/openxr/openxr_loader_for_android/$version/openxr_loader_for_android-$version.aar" -OutFile $aar
}
if ((Get-FileHash -LiteralPath $aar -Algorithm SHA256).Hash -ne $expected) { throw 'OpenXR AAR SHA256 mismatch' }
Expand-Archive -LiteralPath $aar -DestinationPath $xr -Force
$loader = Join-Path $xr 'jni/arm64-v8a/libopenxr_loader.so'
$headers = Join-Path $xr 'prefab/modules/headers/include'
$out = Join-Path $PSScriptRoot "build/$Tag"
$dist = Join-Path $PSScriptRoot 'dist'
$apk = Join-Path $dist "quest-hand-tracking-probe-$Tag.apk"
New-Item -ItemType Directory -Force "$out/lib/arm64-v8a", $dist | Out-Null
& "$tc/bin/clang.exe" --target=aarch64-linux-android32 -O2 -fPIC -c "$glue/android_native_app_glue.c" -I $glue -o "$out/glue.o"
Check 'NativeActivity glue'
& "$tc/bin/clang++.exe" --target=aarch64-linux-android32 -std=c++17 -O2 -fPIC -shared -static-libstdc++ -Wall -Wextra -Wno-missing-field-initializers -Werror=return-type -I $headers -I $glue "$PSScriptRoot/main.cpp" "$out/glue.o" $loader -landroid -llog -lEGL -lGLESv3 '-Wl,-z,max-page-size=16384' '-Wl,--no-undefined' '-Wl,-u,ANativeActivity_onCreate' -o "$out/lib/arm64-v8a/libhandprobe.so"
Check 'Native probe'
Copy-Item -LiteralPath $loader -Destination "$out/lib/arm64-v8a/libopenxr_loader.so"
& "$bt/aapt2.exe" link -o "$out/unsigned.apk" -I "$Sdk/platforms/android-35/android.jar" --manifest "$PSScriptRoot/AndroidManifest.xml"
Check 'APK manifest'
& "$Jdk/bin/jar.exe" uf "$out/unsigned.apk" -C $out lib
Check 'APK native libraries'
& "$bt/zipalign.exe" -f -P 16 4 "$out/unsigned.apk" "$out/aligned.apk"
Check 'APK alignment'
# Generate a project-local development key. Never share or commit this file.
$key = Join-Path $PSScriptRoot '.local/debug.keystore'
if (!(Test-Path -LiteralPath $key)) {
    New-Item -ItemType Directory -Force (Split-Path $key) | Out-Null
    & "$Jdk/bin/keytool.exe" -genkeypair -noprompt -keystore $key -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname 'CN=Android Debug,O=Android,C=US'
    Check 'Development key'
}
& "$Jdk/bin/java.exe" -jar "$bt/lib/apksigner.jar" sign --ks $key --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out $apk "$out/aligned.apk"
Check 'APK signing'
& "$Jdk/bin/java.exe" -jar "$bt/lib/apksigner.jar" verify --verbose $apk
Check 'APK verification'
& "$bt/aapt2.exe" dump badging $apk | Set-Content "$out/badging.txt"
Check 'APK inspection'
Copy-Item -LiteralPath "$xr/META-INF/LICENSE" -Destination "$dist/OpenXR-LICENSE.txt"
Get-Content -LiteralPath "$ndk/NOTICE", "$ndk/NOTICE.toolchain" | Set-Content "$dist/Android-NDK-NOTICE.txt" -Encoding utf8
$hash = (Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $(Split-Path $apk -Leaf)" | Set-Content "$apk.sha256" -Encoding ascii
Write-Output $apk
