plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

// Release signing comes from the environment only (CI secrets). Without all
// four variables the release build stays unsigned and still assembles.
val signingEnv = listOf(
    "HANDVIEW_KEYSTORE_FILE",
    "HANDVIEW_KEYSTORE_PASSWORD",
    "HANDVIEW_KEY_ALIAS",
    "HANDVIEW_KEY_PASSWORD",
).associateWith { System.getenv(it) }
val hasReleaseSigning = signingEnv.values.all { !it.isNullOrBlank() }

android {
    namespace = "io.github.h1dr0nn.handview"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.h1dr0nn.handview"
        // See android-app/README.md for why 26.
        minSdk = 26
        targetSdk = 35
        // Single source of truth in gradle.properties, shared with tools/package.
        versionCode = providers.gradleProperty("handview.versionCode").get().toInt()
        versionName = providers.gradleProperty("handview.versionName").get()
    }

    signingConfigs {
        if (hasReleaseSigning) {
            create("release") {
                val keystore = file(signingEnv.getValue("HANDVIEW_KEYSTORE_FILE")!!)
                if (!keystore.isFile) {
                    throw GradleException("HANDVIEW_KEYSTORE_FILE does not point to a file: $keystore")
                }
                storeFile = keystore
                storePassword = signingEnv.getValue("HANDVIEW_KEYSTORE_PASSWORD")
                keyAlias = signingEnv.getValue("HANDVIEW_KEY_ALIAS")
                keyPassword = signingEnv.getValue("HANDVIEW_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            if (hasReleaseSigning) {
                signingConfig = signingConfigs.getByName("release")
            }
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
}

dependencies {
    implementation(project(":protocol"))
    implementation(project(":client"))
    implementation(libs.kotlinx.coroutines.android)

    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.activity.compose)
}
