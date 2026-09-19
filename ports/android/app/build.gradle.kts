plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

dependencies {
    implementation(project(":controls"))
    implementation("androidx.core:core-ktx:1.13.1")
    testImplementation("junit:junit:4.13.2")
}

// Native compilation is a separate incremental build, never a Gradle side effect.
val nativeStage = providers.gradleProperty("triaevumNativeStage")
android {
    namespace = "org.triaevum.android"
    compileSdk = 35
    defaultConfig {
        applicationId = "org.triaevum.android"
        minSdk = 29
        targetSdk = 33
        versionCode = 1
        versionName = "0.1-intro-dev"
        ndk { abiFilters += "arm64-v8a" }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    sourceSets.getByName("main") {
        if (nativeStage.isPresent) jniLibs.srcDir(nativeStage.get())
    }
    packaging { jniLibs.useLegacyPackaging = true }
}
tasks.register("checkNativeStage") {
    onlyIf {
        gradle.startParameter.taskNames.any { it.contains("assemble", ignoreCase = true) || it.contains("package", ignoreCase = true) }
    }
    doLast {
        check(nativeStage.isPresent) {
            "Supply -PtriaevumNativeStage from the Android native build."
        }
        for (name in listOf("TriAevum", "triaevum_title_bootstrap", "triaevum_title_aot", "c++_shared")) {
            check(file("${nativeStage.get()}/arm64-v8a/lib$name.so").isFile) { "Missing native library: $name" }
        }
    }
}
tasks.named("preBuild") { dependsOn("checkNativeStage") }
