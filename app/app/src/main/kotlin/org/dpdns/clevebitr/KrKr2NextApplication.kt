package org.dpdns.clevebitr

import android.app.Application
import android.util.Log
import org.dpdns.clevebitr.core.NativeEngine

class KrKr2NextApplication : Application() {

    companion object {
        private const val TAG = "KrKr2Next/App"

        /**
         * 引擎共享库是否加载成功。加载失败时 UI 需要给出明确提示，
         * 而不是让每次触碰 NativeEngine 都抛 UnsatisfiedLinkError。
         */
        @Volatile
        var engineLibraryLoaded: Boolean = false
            private set
    }

    override fun onCreate() {
        super.onCreate()

        try {
            // 触发 NativeEngine 的类初始化（内部 System.loadLibrary("engine_api")）
            // 并把 Application Context 交给引擎，供 AndroidUtils.cpp 在拿不到
            // Activity 时回退使用（getExternalFilesDirs / getFilesDir 等）。
            NativeEngine.nativeSetApplicationContext(this)
            engineLibraryLoaded = true
            Log.i(TAG, "libengine_api.so loaded, Application Context handed over")
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "failed to load libengine_api.so — 引擎未打包进 APK？", e)
        }
    }
}
