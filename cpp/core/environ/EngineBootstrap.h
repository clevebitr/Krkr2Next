/**
 * @file EngineBootstrap.h
 * @brief Engine bootstrapper — replaces original AppDelegate for host-mode
 *        startup (e.g. the Android host shell).
 *
 * Uses a native EGL surface (Pbuffer, or ANativeWindow on Android) for headless
 * GLES 3.0 rendering, completely independent of any external framework.
 */
#pragma once

#include <cstdint>
#include <string>

class TVPEngineBootstrap {
public:
    /**
     * Initialize the engine runtime for host mode.
     *
     * This replaces TVPAppDelegate::bootstrapForHostRuntime() and performs:
     *   1. SDL initialization
     *   2. Native EGL context creation (Pbuffer surface)
     *   3. Search path configuration
     *   4. UI extension initialization
     *   5. Locale configuration
     *
     * @param width   Initial surface width in pixels
     * @param height  Initial surface height in pixels
     * @return true on success
     */
    static bool Initialize(uint32_t width, uint32_t height);

    /**
     * Shut down the engine runtime and destroy the EGL context.
     */
    static void Shutdown();

    /**
     * Resize the rendering surface.
     *
     * @param width   New width in pixels
     * @param height  New height in pixels
     * @return true on success
     */
    static bool Resize(uint32_t width, uint32_t height);

    /**
     * Check if the engine bootstrap has been initialized.
     */
    static bool IsInitialized();

private:
    static void InitializeGraphics(uint32_t width, uint32_t height);
    static void InitializeLocale();

    static bool s_initialized;
};
