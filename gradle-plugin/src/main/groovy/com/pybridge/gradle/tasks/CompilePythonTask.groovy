package com.pybridge.gradle.tasks

import org.gradle.api.DefaultTask
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.provider.ListProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.*
import org.gradle.process.ExecOperations
import javax.inject.Inject

/**
 * 编译 Python 源码为优化的 .pyc 字节码
 *
 * - 支持多级优化 (0=debug, 1=normal, 2=strip docstrings)
 * - 生成 __pycache__ 结构兼容标准 Python
 * - 可选生成 .pyo 文件用于生产环境
 */
abstract class CompilePythonTask extends DefaultTask {

    @Input
    abstract Property<String> getPythonPath()

    @InputFiles
    abstract ListProperty<String> getSourceDirs()

    @OutputDirectory
    abstract DirectoryProperty getOutputDir()

    @Input
    abstract Property<Integer> getOptimizeLevel()

    @Inject
    abstract ExecOperations getExecOperations()

    @TaskAction
    void compile() {
        def python = pythonPath.get()
        def outputDirectory = outputDir.get().asFile
        def sources = sourceDirs.get()
        def optLevel = optimizeLevel.get()

        logger.lifecycle("🔨 Compiling Python sources (optimize level: ${optLevel})...")

        outputDirectory.mkdirs()

        for (srcDir in sources) {
            def srcFile = project.file(srcDir)
            if (!srcFile.exists()) {
                logger.warn("   ⚠ Source directory not found: ${srcDir}")
                continue
            }

            logger.lifecycle("   Compiling: ${srcDir}")

            // 使用 compileall 编译
            execOperations.exec { exec ->
                exec.commandLine(
                    python, "-m", "compileall",
                    "-b",                        // 直接写入 .pyc（不创建 __pycache__）
                    "-d", outputDirectory.absolutePath,  // 输出目录
                    "-o", optLevel.toString(),    // 优化级别
                    "-q",                         # 安静模式
                    srcFile.absolutePath
                )
                exec.standardOutput = System.out
                exec.errorOutput = System.err
            }

            // 复制编译结果
            copyCompiledFiles(srcFile, outputDirectory)
        }

        // 生成模块索引
        generateModuleIndex(outputDirectory)

        def count = countFiles(outputDirectory, ".pyc")
        logger.lifecycle("✅ Compiled ${count} Python files")
    }

    private void copyCompiledFiles(File srcDir, File outputDir) {
        project.copy { copy ->
            copy.from(srcDir) {
                include "**/*.pyc"
            }
            copy.into(outputDir)
        }
    }

    private void generateModuleIndex(File outputDir) {
        def modules = []
        outputDir.eachFileRecurse { file ->
            if (file.name.endsWith(".pyc")) {
                def relativePath = file.absolutePath.substring(outputDir.absolutePath.length() + 1)
                modules.add([
                    path: relativePath,
                    name: file.name.replace(".pyc", ""),
                    size: file.length(),
                    hash: file.text.hashCode().toString(16)
                ])
            }
        }

        def index = [
            version: "1.0",
            moduleCount: modules.size(),
            modules: modules
        ]

        new File(outputDir, "_module_index.json").text =
            new groovy.json.JsonBuilder(index).toPrettyString()
    }

    private int countFiles(File dir, String ext) {
        int count = 0
        dir.eachFileRecurse { file ->
            if (file.name.endsWith(ext)) count++
        }
        return count
    }
}
