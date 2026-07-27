"""
PyBridge Demo — Android API 访问（Java/Kotlin 共用）
"""


def log_device_info(context):
    """从 Python 打印设备信息"""
    from pybridge_java import java

    Build = java.jclass("android.os.Build")
    Log = java.jclass("android.util.Log")

    info = (
        f"Brand: {Build.BRAND}, "
        f"Model: {Build.MODEL}, "
        f"SDK: {Build.VERSION.SDK_INT}"
    )
    Log.i("PyBridge-Demo", info)
    return info


def show_toast(context, message: str):
    """显示 Toast"""
    from pybridge_java import java

    Toast = java.jclass("android.widget.Toast")

    def show():
        toast = Toast.makeText(context, message, Toast.LENGTH_SHORT)
        toast.show()

    Looper = java.jclass("android.os.Looper")
    Handler = java.jclass("android.os.Handler")
    Handler(Looper.getMainLooper()).post(show)


def get_assets_list(context) -> list:
    """列出 assets 目录"""
    from pybridge_java import java

    am = context.getAssets()
    files = am.list("")
    return [str(f) for f in files]
