"""
PyBridge PyPI 预编译包服务

提供针对 Android 平台预编译的 Python wheel 包。

功能：
1. 索引 PyPI 上的纯 Python 包
2. 为 Android 交叉编译 C 扩展包
3. 提供 wheel 文件的下载服务
4. 管理包版本和依赖关系
"""

import os
import json
import hashlib
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Optional, Dict, List
from dataclasses import dataclass, asdict


@dataclass
class PackageInfo:
    """包元数据"""
    name: str
    version: str
    abi: str
    python_version: str
    is_pure_python: bool
    filename: str
    size: int
    sha256: str
    dependencies: List[str]


class PyBridgePyPIServer:
    """
    PyBridge 预编译包仓库管理器

    管理针对 Android 平台编译的 Python 包。
    """

    def __init__(self, repo_dir: str):
        self.repo_dir = Path(repo_dir)
        self.repo_dir.mkdir(parents=True, exist_ok=True)
        self.index_file = self.repo_dir / "index.json"
        self._load_index()

    def _load_index(self):
        """加载包索引"""
        if self.index_file.exists():
            with open(self.index_file) as f:
                self.index = json.load(f)
        else:
            self.index = {
                "version": "1.0",
                "packages": {}
            }

    def _save_index(self):
        """保存包索引"""
        with open(self.index_file, 'w') as f:
            json.dump(self.index, f, indent=2)

    def add_package(self, wheel_path: str, abi: str, python_version: str) -> PackageInfo:
        """
        添加预编译包到仓库

        Args:
            wheel_path: wheel 文件路径
            abi: 目标 ABI (arm64-v8a, x86_64 等)
            python_version: Python 版本 (3.11 等)
        """
        wheel_path = Path(wheel_path)
        if not wheel_path.exists():
            raise FileNotFoundError(f"Wheel not found: {wheel_path}")

        # 解析 wheel 文件名
        # 格式: {name}-{version}-{python}-{abi}-{platform}.whl
        parts = wheel_path.stem.split('-')
        name = parts[0]
        version = parts[1]
        is_pure = len(parts) <= 4

        # 计算哈希
        sha256 = self._compute_sha256(wheel_path)

        # 复制到仓库
        dest_dir = self.repo_dir / name / version
        dest_dir.mkdir(parents=True, exist_ok=True)
        dest_path = dest_dir / wheel_path.name
        shutil.copy2(wheel_path, dest_path)

        # 创建包信息
        info = PackageInfo(
            name=name,
            version=version,
            abi=abi,
            python_version=python_version,
            is_pure_python=is_pure,
            filename=wheel_path.name,
            size=wheel_path.stat().st_size,
            sha256=sha256,
            dependencies=[]
        )

        # 更新索引
        if name not in self.index["packages"]:
            self.index["packages"][name] = {}
        if version not in self.index["packages"][name]:
            self.index["packages"][name][version] = {}
        self.index["packages"][name][version][abi] = asdict(info)
        self._save_index()

        return info

    def get_package(self, name: str, version: str, abi: str) -> Optional[Path]:
        """获取包文件路径"""
        if name not in self.index["packages"]:
            return None
        if version not in self.index["packages"][name]:
            return None
        info = self.index["packages"][name][version].get(abi)
        if not info:
            return None

        return self.repo_dir / name / version / info["filename"]

    def list_packages(self) -> Dict:
        """列出所有包"""
        return self.index["packages"]

    def build_android_wheel(self, package_name: str, version: str,
                             abi: str, ndk_path: str) -> Path:
        """
        为 Android 交叉编译 Python 包

        Args:
            package_name: 包名
            version: 版本
            abi: 目标 ABI
            ndk_path: Android NDK 路径
        """
        with tempfile.TemporaryDirectory() as tmpdir:
            tmpdir = Path(tmpdir)

            # 下载源码
            subprocess.run([
                "pip", "download", "--no-binary", ":all:",
                "--dest", str(tmpdir),
                f"{package_name}=={version}"
            ], check=True)

            # 找到源码包
            src_package = list(tmpdir.glob("*.tar.gz"))[0]

            # 解压
            subprocess.run([
                "tar", "xzf", str(src_package),
                "-C", str(tmpdir)
            ], check=True)

            src_dir = list(tmpdir.iterdir())[0]

            # 设置交叉编译环境
            env = self._setup_cross_compile_env(abi, ndk_path)

            # 构建 wheel
            subprocess.run([
                "python3", "-m", "pip", "wheel",
                "--no-deps", "--wheel-dir", str(tmpdir / "wheels"),
                str(src_dir)
            ], env=env, check=True)

            # 找到生成的 wheel
            wheel = list((tmpdir / "wheels").glob("*.whl"))[0]

            # 添加到仓库
            return self.add_package(str(wheel), abi, "3.11")

    def _setup_cross_compile_env(self, abi: str, ndk_path: str) -> Dict:
        """设置交叉编译环境变量"""
        env = os.environ.copy()

        triples = {
            "arm64-v8a": "aarch64-linux-android",
            "armeabi-v7a": "arm-linux-androideabi",
            "x86_64": "x86_64-linux-android",
            "x86": "i686-linux-android",
        }

        triple = triples[abi]
        toolchain = f"{ndk_path}/toolchains/llvm/prebuilt/linux-x86_64"

        env["CC"] = f"{toolchain}/bin/{triple}24-clang"
        env["CXX"] = f"{toolchain}/bin/{triple}24-clang++"
        env["AR"] = f"{toolchain}/bin/llvm-ar"
        env["RANLIB"] = f"{toolchain}/bin/llvm-ranlib"

        return env

    def _compute_sha256(self, path: Path) -> str:
        """计算文件 SHA256"""
        sha256 = hashlib.sha256()
        with open(path, 'rb') as f:
            for chunk in iter(lambda: f.read(8192), b''):
                sha256.update(chunk)
        return sha256.hexdigest()


def serve(repo_dir: str, host: str = "0.0.0.0", port: int = 8080):
    """
    启动 PyPI 服务器

    提供预编译包的 HTTP 下载服务。
    """
    from http.server import HTTPServer, SimpleHTTPRequestHandler
    import functools

    class PyPIHandler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=repo_dir, **kwargs)

        def do_GET(self):
            if self.path == "/" or self.path == "/index":
                # 返回包索引页面
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                with open(os.path.join(repo_dir, "index.json")) as f:
                    self.wfile.write(f.read().encode())
            else:
                super().do_GET()

    server = HTTPServer((host, port), PyPIHandler)
    print(f"🐍 PyBridge PyPI server running at http://{host}:{port}")
    server.serve_forever()


if __name__ == "__main__":
    import sys
    repo = sys.argv[1] if len(sys.argv) > 1 else "./repo"
    serve(repo)
