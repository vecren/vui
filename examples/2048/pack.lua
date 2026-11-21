local apk = require("@lito.apk")

local abis = { "arm64-v8a", "x86_64" }

local function package(options)
    options.native = "cdylib:vui-2048-android"
    options.manifest = "AndroidManifest.xml"
    options.java_sources = { "../../host/src/platform/android/java" }
    options.min_api = 29
    options.version_code = 1
    options.version_name = "0.1.0"
    options.debug_keystore = lito.env("VUI_ANDROID_DEBUG_KEYSTORE") or "~/.android/vui-debug.keystore"
    apk.package(options)
end

package {
    name = "apk",
    output = "vui-2048.apk",
    debuggable = true,
}

package {
    name = "apk-all",
    output = "vui-2048-all.apk",
    abis = abis,
    debuggable = true,
}

local upload_key = lito.env("VUI_ANDROID_UPLOAD_KEYSTORE")
package {
    name = "aab",
    output = "vui-2048.aab",
    format = "aab",
    abis = abis,
    signing = upload_key and {
        keystore = upload_key,
        alias = lito.env("VUI_ANDROID_UPLOAD_KEY_ALIAS"),
        storepass_env = "VUI_ANDROID_UPLOAD_STOREPASS",
        keypass_env = lito.env("VUI_ANDROID_UPLOAD_KEYPASS") and "VUI_ANDROID_UPLOAD_KEYPASS" or nil,
    } or nil,
}
