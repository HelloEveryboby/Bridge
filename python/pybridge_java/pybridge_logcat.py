"""
PyBridge Logcat — stdout/stderr → Android Logcat 重定向

将 Python 的 print() 输出和异常信息重定向到 Android Logcat。

使用方式：
    from pybridge_java import LogcatRedirect

    # 自动重定向（推荐）
    LogcatRedirect.install()

    # 自定义标签
    LogcatRedirect.install(tag="MyApp", log_level="DEBUG")

    # 卸载
    LogcatRedirect.uninstall()

重定向规则：
    sys.stdout.write() → android.util.Log.i(TAG, message)
    sys.stderr.write() → android.util.Log.e(TAG, message)
    Python warnings      → android.util.Log.w(TAG, message)
"""

import sys
import logging
import warnings
from typing import Optional


class _LogcatStream:
    """
    将输出流转发到 Android Logcat

    缓冲行输出，每行写入一条 Logcat 记录。
    """

    def __init__(self, tag: str, priority: str, is_stderr: bool = False):
        self._tag = tag
        self._priority = priority
        self._is_stderr = is_stderr
        self._buffer = ""
        self._Log = None

    def _get_log(self):
        """延迟加载 android.util.Log"""
        if self._Log is None:
            try:
                from pybridge_java import jclass
                self._Log = jclass("android.util.Log")
            except Exception:
                self._Log = False
        return self._Log if self._Log is not False else None

    def write(self, message: str):
        if not message:
            return
        self._buffer += message
        while "\n" in self._buffer:
            line, self._buffer = self._buffer.split("\n", 1)
            self._write_line(line)

    def _write_line(self, line: str):
        if not line.strip():
            return
        Log = self._get_log()
        if Log is not None:
            try:
                if self._priority == "ERROR":
                    Log.e(self._tag, line)
                elif self._priority == "WARNING":
                    Log.w(self._tag, line)
                elif self._priority == "DEBUG":
                    Log.d(self._tag, line)
                elif self._priority == "VERBOSE":
                    Log.v(self._tag, line)
                else:
                    Log.i(self._tag, line)
            except Exception:
                sys.__stderr__.write(f"[{self._tag}] {line}\n")
        else:
            if self._is_stderr:
                sys.__stderr__.write(f"[{self._tag}] {line}\n")
            else:
                sys.__stdout__.write(f"[{self._tag}] {line}\n")

    def flush(self):
        if self._buffer.strip():
            self._write_line(self._buffer)
            self._buffer = ""

    def isatty(self) -> bool:
        return False


class LogcatRedirect:
    """stdout/stderr → Logcat 重定向管理器"""

    _installed: bool = False
    _stdout_stream: Optional[_LogcatStream] = None
    _stderr_stream: Optional[_LogcatStream] = None
    _original_stdout: Optional[object] = None
    _original_stderr: Optional[object] = None
    _original_showwarning: Optional[object] = None

    @classmethod
    def install(cls, tag: str = "PyBridge", log_level: str = "INFO"):
        if cls._installed:
            return

        cls._original_stdout = sys.stdout
        cls._original_stderr = sys.stderr
        cls._stdout_stream = _LogcatStream(tag, log_level, is_stderr=False)
        cls._stderr_stream = _LogcatStream(tag, "ERROR", is_stderr=True)
        sys.stdout = cls._stdout_stream
        sys.stderr = cls._stderr_stream

        # 重定向 Python warnings
        cls._original_showwarning = warnings.showwarning

        def _logcat_showwarning(message, category, filename, lineno, file=None, line=None):
            Log = cls._stdout_stream._get_log() if cls._stdout_stream else None
            warning_msg = f"{filename}:{lineno}: {category.__name__}: {message}"
            if Log is not None:
                try:
                    Log.w(tag, warning_msg)
                except Exception:
                    pass
            if cls._original_stderr:
                cls._original_stderr.write(f"Warning: {warning_msg}\n")

        warnings.showwarning = _logcat_showwarning
        _setup_logging_handler(tag)
        cls._installed = True

    @classmethod
    def uninstall(cls):
        if not cls._installed:
            return
        if cls._stdout_stream:
            cls._stdout_stream.flush()
        if cls._stderr_stream:
            cls._stderr_stream.flush()
        if cls._original_stdout is not None:
            sys.stdout = cls._original_stdout
        if cls._original_stderr is not None:
            sys.stderr = cls._original_stderr
        if cls._original_showwarning is not None:
            warnings.showwarning = cls._original_showwarning
        cls._installed = False
        cls._stdout_stream = None
        cls._stderr_stream = None


def _setup_logging_handler(tag: str):
    """将 Python logging 输出重定向到 Logcat"""
    try:
        from pybridge_java import jclass
        Log = jclass("android.util.Log")
    except Exception:
        return

    class LogcatHandler(logging.Handler):
        def emit(self, record: logging.LogRecord):
            try:
                msg = self.format(record)
                if record.levelno >= logging.ERROR:
                    Log.e(tag, msg)
                elif record.levelno >= logging.WARNING:
                    Log.w(tag, msg)
                elif record.levelno >= logging.INFO:
                    Log.i(tag, msg)
                else:
                    Log.d(tag, msg)
            except Exception:
                sys.__stderr__.write(f"[{tag}] {self.format(record)}\n")

    handler = LogcatHandler()
    handler.setFormatter(logging.Formatter(
        '%(asctime)s [%(levelname)s] %(name)s: %(message)s',
        datefmt='%H:%M:%S'
    ))
    logging.root.addHandler(handler)
    logging.root.setLevel(logging.DEBUG)


def install(tag: str = "PyBridge", log_level: str = "INFO"):
    """安装 Logcat 重定向（便捷函数）"""
    LogcatRedirect.install(tag=tag, log_level=log_level)


def uninstall():
    """卸载 Logcat 重定向（便捷函数）"""
    LogcatRedirect.uninstall()