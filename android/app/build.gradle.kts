plugins {
    id("com.android.application")
}

// Read version dynamically from root CMakeLists.txt
val cmakeListsFile = file("../../CMakeLists.txt")
val versionMatch = Regex("""project\(xcloud_ps5 VERSION ([0-9]+)\.([0-9]+)\.([0-9]+)""").find(cmakeListsFile.readText())
val verMajor = versionMatch?.groupValues?.getOrNull(1) ?: "1"
val verMinor = versionMatch?.groupValues?.getOrNull(2) ?: "7"
val verPatch = versionMatch?.groupValues?.getOrNull(3) ?: "2"
val computedVersionCode = verMajor.toInt() * 10000 + verMinor.toInt() * 100 + verPatch.toInt()
val computedVersionName = "$verMajor.$verMinor.$verPatch"

android {
    namespace = "com.rafaelngp.psbox"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.rafaelngp.psbox"
        minSdk = 26
        targetSdk = 35
        versionCode = computedVersionCode
        versionName = computedVersionName

        ndk {
            abiFilters.addAll(listOf("arm64-v8a", "x86_64"))
        }
        externalNativeBuild {
            cmake {
                arguments.addAll(listOf(
                    "-DXCLOUD_DESKTOP_APP=ON",
                    "-DANDROID_STL=c++_shared",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384"
                ))
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    signingConfigs {
        create("release") {
            val ksPath = System.getenv("ANDROID_KEYSTORE_PATH") ?: "../keystore/psbox-release.jks"
            val ksFile = file(ksPath)
            if (ksFile.exists()) {
                storeFile = ksFile
                storePassword = System.getenv("ANDROID_KEYSTORE_PASSWORD") ?: "psboxrelease2026"
                keyAlias = System.getenv("ANDROID_KEY_ALIAS") ?: "psbox"
                keyPassword = System.getenv("ANDROID_KEY_PASSWORD") ?: "psboxrelease2026"
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            val releaseSigning = signingConfigs.findByName("release")
            if (releaseSigning?.storeFile?.exists() == true) {
                signingConfig = releaseSigning
            }
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    val generatedAssetsDir = layout.buildDirectory.dir("generated/psboxAssets")

    val generateAssetList by tasks.registering(Exec::class) {
        val script = file("../../tools/android/generate-asset-list.py")
        val outFile = generatedAssetsDir.get().file("asset-list.txt").asFile
        inputs.dir(file("../../assets"))
        inputs.file(script)
        outputs.file(outFile)
        commandLine("python3", script.absolutePath, outFile.absolutePath)
    }

    androidResources {
        ignoreAssetsPattern = "media_showcase:!.svn:!.git:!.ds_store:!*.scc:.*:<dir>_*:!CVS:!thumbs.db:!picasa.ini:!*~"
    }

    sourceSets["main"].apply {
        java.srcDirs(
            "src/main/java",
            "../../deps/SDL/android-project/app/src/main/java"
        )
        assets.srcDirs("../../assets", generatedAssetsDir)
    }
}

tasks.named("preBuild") {
    dependsOn("generateAssetList")
}
