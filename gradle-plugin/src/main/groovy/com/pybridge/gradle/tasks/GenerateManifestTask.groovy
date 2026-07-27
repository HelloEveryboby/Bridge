package com.pybridge.gradle.tasks

import org.gradle.api.DefaultTask
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.provider.ListProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.*

/**
 * 生成 Python 资产清单，供运行时使用
 */
abstract class GenerateManifestTask extends DefaultTask {

    @Input
    abstract ListProperty<String> getPackages()

    @Input
    abstract Property<String> getPythonVersion()

    @OutputFile
    abstract RegularFileProperty getOutputFile()

    @TaskAction
    void generate() {
        def manifest = [
            schemaVersion: "1.0",
            pythonVersion: pythonVersion.get(),
            buildTime: new Date().format("yyyy-MM-dd'T'HH:mm:ss'Z'", TimeZone.getTimeZone("UTC")),
            packages: packages.get().collect { pkg ->
                def name = pkg.split("[=<>!~]+")[0].trim()
                def version = ""
                def spec = pkg
                if (pkg.contains("==")) {
                    version = pkg.split("==")[1].trim()
                }
                return [name: name, version: version, spec: spec]
            },
            assets: [
                compiledModules: "compiled/",
                packages: "packages/",
                runtime: "runtime/"
            ]
        ]

        def outputFile = outputFile.get().asFile
        outputFile.parentFile.mkdirs()
        outputFile.text = new groovy.json.JsonBuilder(manifest).toPrettyString()

        logger.lifecycle("📋 Python manifest generated: ${outputFile}")
    }
}
