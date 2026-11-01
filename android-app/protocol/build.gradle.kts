// Pure Kotlin protocol codec, kept apart from the app so the wire format can
// change in one place and be unit-tested on the JVM.
plugins {
    alias(libs.plugins.kotlin.jvm)
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    testImplementation(libs.junit)
}
