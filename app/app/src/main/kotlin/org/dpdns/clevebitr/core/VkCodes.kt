package org.dpdns.clevebitr.core

import android.view.KeyEvent as AndroidKeyEvent

/**
 * Windows 虚拟键码（VK）。
 *
 * 引擎的 `EngineLoop::HandleKeyDown` 把 `engine_input_event_t.key_code` **直接当作
 * Windows VK 码**使用（例如返回键会被改写成 `VK_ESCAPE = 0x1B`），这套码同时暴露
 * 给 TJS 脚本（`cpp/core/base/ScriptMgnIntf.cpp`），所以游戏脚本里判断的就是它。
 *
 * **Android 的 `KeyEvent.getKeyCode()` 返回 `KEYCODE_*`，数值与 VK 完全不同**
 * （Android `KEYCODE_ESCAPE=111` vs `VK_ESCAPE=0x1B=27`；Android `KEYCODE_ENTER=66`
 * vs `VK_RETURN=0x0D=13`）。必须经 [fromAndroid] 显式映射，不能透传。
 *
 * 取值来源：`cpp/core/environ/vkdefine.h`。详见 `README.md`「硬约束」。
 */
object VkCodes {
    const val LBUTTON = 0x01
    const val RBUTTON = 0x02
    const val MBUTTON = 0x04
    const val BACK = 0x08
    const val TAB = 0x09
    const val RETURN = 0x0D
    const val SHIFT = 0x10
    const val CONTROL = 0x11
    const val MENU = 0x12 // Alt
    const val PAUSE = 0x13
    const val CAPITAL = 0x14
    const val ESCAPE = 0x1B
    const val SPACE = 0x20
    const val PRIOR = 0x21 // PageUp
    const val NEXT = 0x22 // PageDown
    const val END = 0x23
    const val HOME = 0x24
    const val LEFT = 0x25
    const val UP = 0x26
    const val RIGHT = 0x27
    const val DOWN = 0x28
    const val INSERT = 0x2D
    const val DELETE = 0x2E

    // 数字 0-9：VK_0..VK_9 = 0x30..0x39
    // 字母 A-Z：  VK_A..VK_Z = 0x41..0x5A

    const val F1 = 0x70 // F1..F24 = 0x70..0x87

    /** 'A'..'Z' → VK_A..VK_Z */
    fun letter(c: Char): Int? {
        val upper = c.uppercaseChar()
        return if (upper in 'A'..'Z') 0x41 + (upper - 'A') else null
    }

    /** '0'..'9' → VK_0..VK_9 */
    fun digit(c: Char): Int? =
        if (c in '0'..'9') 0x30 + (c - '0') else null

    /**
     * Android `KeyEvent` 键码 → Windows VK 码。无法映射的返回 null（调用方应忽略该键）。
     */
    fun fromAndroid(androidKeyCode: Int): Int? = when (androidKeyCode) {
        AndroidKeyEvent.KEYCODE_BACK -> ESCAPE
        AndroidKeyEvent.KEYCODE_ESCAPE -> ESCAPE
        AndroidKeyEvent.KEYCODE_ENTER,
        AndroidKeyEvent.KEYCODE_NUMPAD_ENTER -> RETURN
        AndroidKeyEvent.KEYCODE_SPACE -> SPACE
        AndroidKeyEvent.KEYCODE_TAB -> TAB

        AndroidKeyEvent.KEYCODE_DEL -> BACK
        AndroidKeyEvent.KEYCODE_FORWARD_DEL -> DELETE
        AndroidKeyEvent.KEYCODE_INSERT -> INSERT
        AndroidKeyEvent.KEYCODE_MOVE_HOME -> HOME
        AndroidKeyEvent.KEYCODE_MOVE_END -> END
        AndroidKeyEvent.KEYCODE_PAGE_UP -> PRIOR
        AndroidKeyEvent.KEYCODE_PAGE_DOWN -> NEXT

        AndroidKeyEvent.KEYCODE_DPAD_LEFT -> LEFT
        AndroidKeyEvent.KEYCODE_DPAD_UP -> UP
        AndroidKeyEvent.KEYCODE_DPAD_RIGHT -> RIGHT
        AndroidKeyEvent.KEYCODE_DPAD_DOWN -> DOWN

        AndroidKeyEvent.KEYCODE_SHIFT_LEFT,
        AndroidKeyEvent.KEYCODE_SHIFT_RIGHT -> SHIFT
        AndroidKeyEvent.KEYCODE_CTRL_LEFT,
        AndroidKeyEvent.KEYCODE_CTRL_RIGHT -> CONTROL
        AndroidKeyEvent.KEYCODE_ALT_LEFT,
        AndroidKeyEvent.KEYCODE_ALT_RIGHT -> MENU
        AndroidKeyEvent.KEYCODE_CAPS_LOCK -> CAPITAL
        AndroidKeyEvent.KEYCODE_BREAK -> PAUSE

        // 字母
        AndroidKeyEvent.KEYCODE_A -> letter('A')
        AndroidKeyEvent.KEYCODE_B -> letter('B')
        AndroidKeyEvent.KEYCODE_C -> letter('C')
        AndroidKeyEvent.KEYCODE_D -> letter('D')
        AndroidKeyEvent.KEYCODE_E -> letter('E')
        AndroidKeyEvent.KEYCODE_F -> letter('F')
        AndroidKeyEvent.KEYCODE_G -> letter('G')
        AndroidKeyEvent.KEYCODE_H -> letter('H')
        AndroidKeyEvent.KEYCODE_I -> letter('I')
        AndroidKeyEvent.KEYCODE_J -> letter('J')
        AndroidKeyEvent.KEYCODE_K -> letter('K')
        AndroidKeyEvent.KEYCODE_L -> letter('L')
        AndroidKeyEvent.KEYCODE_M -> letter('M')
        AndroidKeyEvent.KEYCODE_N -> letter('N')
        AndroidKeyEvent.KEYCODE_O -> letter('O')
        AndroidKeyEvent.KEYCODE_P -> letter('P')
        AndroidKeyEvent.KEYCODE_Q -> letter('Q')
        AndroidKeyEvent.KEYCODE_R -> letter('R')
        AndroidKeyEvent.KEYCODE_S -> letter('S')
        AndroidKeyEvent.KEYCODE_T -> letter('T')
        AndroidKeyEvent.KEYCODE_U -> letter('U')
        AndroidKeyEvent.KEYCODE_V -> letter('V')
        AndroidKeyEvent.KEYCODE_W -> letter('W')
        AndroidKeyEvent.KEYCODE_X -> letter('X')
        AndroidKeyEvent.KEYCODE_Y -> letter('Y')
        AndroidKeyEvent.KEYCODE_Z -> letter('Z')

        // 数字（主键盘与数字小键盘）
        AndroidKeyEvent.KEYCODE_0 -> digit('0')
        AndroidKeyEvent.KEYCODE_1 -> digit('1')
        AndroidKeyEvent.KEYCODE_2 -> digit('2')
        AndroidKeyEvent.KEYCODE_3 -> digit('3')
        AndroidKeyEvent.KEYCODE_4 -> digit('4')
        AndroidKeyEvent.KEYCODE_5 -> digit('5')
        AndroidKeyEvent.KEYCODE_6 -> digit('6')
        AndroidKeyEvent.KEYCODE_7 -> digit('7')
        AndroidKeyEvent.KEYCODE_8 -> digit('8')
        AndroidKeyEvent.KEYCODE_9 -> digit('9')
        AndroidKeyEvent.KEYCODE_NUMPAD_0 -> 0x60
        AndroidKeyEvent.KEYCODE_NUMPAD_1 -> 0x61
        AndroidKeyEvent.KEYCODE_NUMPAD_2 -> 0x62
        AndroidKeyEvent.KEYCODE_NUMPAD_3 -> 0x63
        AndroidKeyEvent.KEYCODE_NUMPAD_4 -> 0x64
        AndroidKeyEvent.KEYCODE_NUMPAD_5 -> 0x65
        AndroidKeyEvent.KEYCODE_NUMPAD_6 -> 0x66
        AndroidKeyEvent.KEYCODE_NUMPAD_7 -> 0x67
        AndroidKeyEvent.KEYCODE_NUMPAD_8 -> 0x68
        AndroidKeyEvent.KEYCODE_NUMPAD_9 -> 0x69

        AndroidKeyEvent.KEYCODE_F1 -> F1
        AndroidKeyEvent.KEYCODE_F2 -> 0x71
        AndroidKeyEvent.KEYCODE_F3 -> 0x72
        AndroidKeyEvent.KEYCODE_F4 -> 0x73
        AndroidKeyEvent.KEYCODE_F5 -> 0x74
        AndroidKeyEvent.KEYCODE_F6 -> 0x75
        AndroidKeyEvent.KEYCODE_F7 -> 0x76
        AndroidKeyEvent.KEYCODE_F8 -> 0x77
        AndroidKeyEvent.KEYCODE_F9 -> 0x78
        AndroidKeyEvent.KEYCODE_F10 -> 0x79
        AndroidKeyEvent.KEYCODE_F11 -> 0x7A
        AndroidKeyEvent.KEYCODE_F12 -> 0x7B

        // OEM 键（US 布局）
        AndroidKeyEvent.KEYCODE_SEMICOLON -> 0xBA
        AndroidKeyEvent.KEYCODE_EQUALS -> 0xBB
        AndroidKeyEvent.KEYCODE_COMMA -> 0xBC
        AndroidKeyEvent.KEYCODE_MINUS -> 0xBD
        AndroidKeyEvent.KEYCODE_PERIOD -> 0xBE
        AndroidKeyEvent.KEYCODE_SLASH -> 0xBF
        AndroidKeyEvent.KEYCODE_GRAVE -> 0xC0
        AndroidKeyEvent.KEYCODE_LEFT_BRACKET -> 0xDB
        AndroidKeyEvent.KEYCODE_BACKSLASH -> 0xDC
        AndroidKeyEvent.KEYCODE_RIGHT_BRACKET -> 0xDD
        AndroidKeyEvent.KEYCODE_APOSTROPHE -> 0xDE

        else -> null
    }

    /**
     * Android `KeyEvent` 的修饰键位图 → 引擎期望的 shift 标志。
     *
     * 引擎侧 `EngineLoop::ConvertModifiers` 取低 8 位并按 TVP shift 标志解释：
     * bit0=Shift, bit1=Ctrl, bit2=Alt, bit5=Middle（鼠标）。这里只映射键盘修饰键。
     */
    fun modifiersFromAndroid(metaState: Int): Int {
        var flags = 0
        if (metaState and AndroidKeyEvent.META_SHIFT_ON != 0) flags = flags or 0x01
        if (metaState and AndroidKeyEvent.META_CTRL_ON != 0) flags = flags or 0x02
        if (metaState and AndroidKeyEvent.META_ALT_ON != 0) flags = flags or 0x04
        return flags
    }
}
