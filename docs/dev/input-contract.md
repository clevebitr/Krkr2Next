# 输入契约（引擎 ↔ 壳）

本文件是引擎输入接口的唯一权威说明。原 Flutter 壳的输入实现在 `apps/` 删除后不再存在，
本文件替代它作为参照。改动输入相关代码前先读本文。

## 数据通路

```
壳（Kotlin）── engine_send_input(handle, engine_input_event_t) ──> EngineLoop::HandleInputEvent
                                                                        │
                                        TVPPostInputEvent(tTVPOn*InputEvent) ──> 下一帧 tick 派发
```

`engine_send_input` 在 `bridge/engine_api/src/engine_api.cpp` 中把事件拷入
`EngineInputEvent`（bridge → core 结构），再交 `EngineLoop::HandleInputEvent`
（`cpp/core/environ/EngineLoop.cpp:189`）。事件被**排队**，在**下一次 tick** 才派发。

**必须设置 `struct_size = sizeof(engine_input_event_t)`。**
结构体定义见 `bridge/engine_api/include/engine_api.h:127`。

### ⚠️ 线程约束：必须在 engine_create 所在线程调用

虽然事件本身是排队的，但 `engine_send_input`（`engine_api.cpp:2205`）会先做
`ValidateHandleThreadLocked`，要求调用线程等于 `owner_thread`——即 `engine_create`
所在的那条线程（本项目中是 Kotlin 的渲染线程）。

**不满足时返回 `ENGINE_RESULT_INVALID_STATE`，且事件被直接丢弃、不入队。**
症状是游戏完全无响应，且只有一条 `Log.w` 提示，很容易被当成"输入没做"。

因此壳侧从 UI 线程收到触摸/按键后，**必须切到渲染线程再调用**
（`EngineSession.sendInput` 内部用 `post{}` 完成这一步）。切换不增加可感知延迟——
事件本来就要等到下一次 tick 才派发。

## 事件类型

| 值 | 名称 | 用途 |
|---|---|---|
| 1 | `ENGINE_INPUT_EVENT_POINTER_DOWN` | 指针按下 |
| 2 | `ENGINE_INPUT_EVENT_POINTER_MOVE` | 指针移动 |
| 3 | `ENGINE_INPUT_EVENT_POINTER_UP` | 指针抬起 |
| 4 | `ENGINE_INPUT_EVENT_POINTER_SCROLL` | 滚轮 |
| 5 | `ENGINE_INPUT_EVENT_KEY_DOWN` | 按键按下 |
| 6 | `ENGINE_INPUT_EVENT_KEY_UP` | 按键抬起 |
| 7 | `ENGINE_INPUT_EVENT_TEXT_INPUT` | 文本输入（IME） |
| 8 | `ENGINE_INPUT_EVENT_BACK` | 返回键 |

## 关键约束一：`key_code` 是 Windows VK 码，不是 Android keycode

**这是最容易踩错的一点。** `EngineLoop::HandleKeyDown`（`EngineLoop.cpp:349`）把
`key_code` 直接当作 Windows 虚拟键码使用：

```cpp
tjs_uint key = static_cast<tjs_uint>(event.key_code);
if (event.type == kEngineInputBack) {
    key = 0x1B; // VK_ESCAPE
}
```

权威表在 `cpp/core/environ/vkdefine.h`；这些常量同时暴露给 TJS 脚本
（`cpp/core/base/ScriptMgnIntf.cpp:184-208`），所以游戏脚本里判断的就是这套码。
引擎自身的默认按键处理在 `cpp/core/visual/LayerIntf.cpp:3961-3982`（`VK_LEFT`/`VK_UP`/
`VK_RETURN`/`VK_ESCAPE`）。

**Android 的 `KeyEvent.getKeyCode()` 返回 `KEYCODE_*`，两者数值完全不同**
（如 Android `KEYCODE_ESCAPE=111`，而 `VK_ESCAPE=0x1B=27`；Android `KEYCODE_ENTER=66`，
`VK_RETURN=0x0D=13`）。**必须显式映射**，不能直接透传。

KAG 游戏常用键：

| 用途 | VK 码 | Android KeyEvent 来源 |
|---|---|---|
| 确认 / 推进文本 | `VK_RETURN` 0x0D、`VK_SPACE` 0x20 | `KEYCODE_ENTER`、`KEYCODE_SPACE` |
| 跳过 | `VK_CONTROL` 0x11 | `KEYCODE_CTRL_LEFT`/`_RIGHT` |
| 菜单 / 取消 | `VK_ESCAPE` 0x1B | `KEYCODE_ESCAPE`、系统返回键 |
| 选择项 | `VK_UP` 0x26 / `VK_DOWN` 0x28 / `VK_LEFT` 0x25 / `VK_RIGHT` 0x27 | 方向键 |
| 翻页 | `VK_PRIOR` 0x21（PageUp）/ `VK_NEXT` 0x22（PageDown） | — |

字母与数字：`VK_A`–`VK_Z` = 0x41–0x5A，`VK_0`–`VK_9` = 0x30–0x39。

## 关键约束二：返回键是三事件序列

`ENGINE_INPUT_EVENT_BACK` 在 `HandleInputEvent` 中被当作"Escape 按下"处理
（`EngineLoop.cpp:210-212` 注释："Treat Back as Escape key press"）。
原 Flutter 实现（`engine_surface.dart` 的 `sendBack()`）发的是一组**三个**事件：

```
KEY_DOWN(key_code=VK_ESCAPE)  →  BACK(key_code=VK_ESCAPE)  →  KEY_UP(key_code=VK_ESCAPE)
```

顺序不可省略：缺 `KEY_DOWN` 则部分游戏收不到普通键事件；缺 `KEY_UP` 会让虚拟 Esc
被误认为一直按住。壳的折叠菜单"返回"项也复用这个序列。

系统返回键在原 Flutter 实现中同时发 `keyDown` + `back` 两个事件（见 `_onKeyEvent`）。

## 关键约束三：指针坐标是物理像素

坐标流：**壳 → 物理像素 → C++ `DrawDevice::TransformToPrimaryLayerManager` → primary layer 坐标。**

- 原 Flutter 实现里 `localPosition` 是**逻辑像素**，所以乘了 `devicePixelRatio`
  （`engine_surface.dart:644-655` 有完整注释）。**Kotlin 侧不需这一步**——
  `MotionEvent.getX()/getY()` 返回的已是视图坐标（物理像素），直接填入即可。
- 引擎侧 `HandlePointerDown`（`EngineLoop.cpp:226`）直接 `static_cast<tjs_int>(event.x)`
  并调 `win->GetForm()->UpdateCursorPos(x, y)` 缓存，供 `Layer.cursorX/cursorY` 查询。
- **不要**在壳里重复应用缩放/偏移/旋转（`conventions.md` 有同样的禁令）。

## 指针按钮映射

`0=左, 1=右, 2=中`（`EngineLoop.cpp:237-241`，对应 `tTVPMouseButton`）。

| 壳侧 | 值 |
|---|---|
| 左键 / 触摸 | 0（默认） |
| `MotionEvent.BUTTON_SECONDARY` | 1 |
| `MotionEvent.BUTTON_TERTIARY` | 2 |

滚轮用 `delta_x` / `delta_y`。

## 指针移动合并（性能约定）

原 Flutter 实现（`_sendCoalescedPointerMove`）把高频 `POINTER_MOVE` **合并为每帧最多一次**，
避免在指针事件洪流下反复跨 FFI/JNI 调用。Android 侧的等价手段是
`MotionEvent.getHistoricalX/Y(i)` 批量读取一次事件内合并的多个采样点。

按下与抬起**不合并**，必须逐个发送。

## 修饰键

`modifiers` 经 `ConvertModifiers`（`EngineLoop.cpp:185`）转成 TVP shift 标志，
按位对应 `TVP_SS_LEFT`/`RIGHT`/`MIDDLE`（鼠标）与 Shift 等。位定义见
`EngineLoop.cpp:170-186` 的注释。

## 文本输入

IME 文本走 `ENGINE_INPUT_EVENT_TEXT_INPUT`，用 `unicode_codepoint` 字段。
游戏内文本输入框依赖它（原 Flutter 侧由 `TextInput` 通道驱动）。
