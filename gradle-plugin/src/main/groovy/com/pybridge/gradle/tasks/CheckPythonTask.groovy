package com.pybridge.gradle.tasks

import org.gradle.api.DefaultTask
import org.gradle.api.provider.Property
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.TaskAction
import org.gradle.process.ExecOperations
import javax.inject.Inject

/**
 * 检查 Python 解释器是否可用，版本是否匹配
 */
abstract class CheckPythonTask extends DefaultTask {

    @Input
    abstract Property<String> getPythonPath()

    @Input
    abstract Property<String> getPythonVersion()

    @Inject
    abstract ExecOperations getExecOperations()

    @TaskAction
    void check() {
        def python = pythonPath.get()
        def requiredVersion = pythonVersion.get()

        logger.lifecycle("🐍 Checking Python interpreter: ${python}")

        // 检查 Python 是否存在
        def result = execPython(python, "--version")
        if (result.exitCode != 0) {
            throw new RuntimeException(
                "Python not found at '${python}'. " +
                "Install Python ${requiredVersion} or set buildPython path."
            )
        }

        def actualVersion = result.output.trim()
        logger.lifecycle("   Found: ${actualVersion}")

        // 验证主版本号
        def match = (actualVersion =~ /Python (\d+\.\d+)/)
        if (!match.find()) {
            throw new RuntimeException("Cannot parse Python version: ${actualVersion}")
        }

        def majorMinor = match.group(1)
        if (!majorMinor.startsWith(requiredVersion)) {
            throw new RuntimeException(
                "Python version mismatch: required ${requiredVersion}.*, found ${actualVersion}"
            )
        }

        // 检查必要模块
        checkModule(python, "compileall")
        checkModule(python, "zipfile")
        checkModule(python, "pip")

        logger.lifecycle("✅ Python ${actualVersion} is ready")
    }

    private void checkModule(String python, String module) {
        def result = execPython(python, "-c", "import ${module}")
        if (result.exitCode != 0) {
            throw new RuntimeException("Python module '${module}' is not available")
        }
    }

    private ExecResult execPython(String... args) {
        def stdout = new ByteArrayOutputStream()
        def stderr = new ByteArrayOutputStream()
        def exitCode = 0
        try {
            execOperations.exec { exec ->
                exec.commandLine(args)
                exec.standardOutput = stdout
                exec.errorOutput = stderr
                exec.ignoreExitValue = true
            }
            exitCode = execOperations.exec { exec ->
                exec.commandLine(args)
                exec.standardOutput = stdout
                exec.errorOutput = stderr
                exec.ignoreExitValue = true
            }.exitValue
        } catch (Exception e) {
            return new ExecResult(1, "", e.message)
        }
        return new ExecResult(exitCode, stdout.toString(), stderr.toString())
    }

    static class ExecResult {
        int exitCode
        String output
        String error
        ExecResult(int exitCode, String output, String error) {
            this.exitCode = exitCode
            this.output = output
            this.error = error
        }
    }
}
