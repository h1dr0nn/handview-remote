plugins { alias(libs.plugins.kotlin.jvm) }
kotlin { jvmToolchain(17) }
dependencies {
    implementation(project(":protocol"))
    testImplementation(libs.junit)
}
