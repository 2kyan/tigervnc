plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.tigervnc.mobile.viewer"
    // Matches ANDROID_PLATFORM=android-24 used to cross-compile
    // third_party/android's dependencies and core/ -- see DESIGN.md's
    // Android spike findings for why.
    compileSdk = 35
    // Pinned to the same NDK used to prebuild core/ and third_party/android's
    // deps (see third_party/android/build-deps.sh) -- AGP's own default NDK
    // would otherwise auto-download a different version to compile this
    // module's JNI glue against those prebuilt .a files.
    ndkVersion = "29.0.13846066"

    defaultConfig {
        applicationId = "com.tigervnc.mobile.viewer"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"

        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_static"
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "4.1.0"
        }
    }

    packaging {
        // tigervnc_core and its dependencies are linked statically (see
        // src/main/cpp/CMakeLists.txt) -- no .so files to exclude here,
        // just the single libvncjni.so this module builds.
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation(platform("androidx.compose:compose-bom:2024.10.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.foundation:foundation")
}
