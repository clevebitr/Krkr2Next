/**
 * @file win32dialog.cpp
 * @brief win32dialog.dll —— Windows 对话框（确认框/模板对话框）的宿主实现。
 *
 * 真机上"游戏需要 Windows 窗体确认"这一步卡住的原因：本文件以前只注册了
 * `WIN32Dialog.messageBox` 一个方法，而游戏侧（KiriKiri Z 自带的
 * `win32dialog.tjs`，以及 k2compat 内嵌的同名脚本）会用到
 * `WIN32Dialog.Header` / `WIN32Dialog.Items` / `getItem*` / `setItem*` / `open()`
 * 这一整套 Win32 形状的 API —— 缺一个就是 `Member ... does not exist`，
 * 对话框根本构造不起来。
 *
 * 本实现分两类：
 *   * **真实可用**：`messageBox`（走 `System.inform` → 宿主壳的 Android 确认框，
 *     按钮语义按 Win32 MB_* / ID* 常量映射）；`open()`（确认型对话框：把标题/
 *     正文/按钮文案交给宿主消息框，按用户选择返回 IDOK/IDCANCEL/IDYES/IDNO…）。
 *   * **形状实现**：`Header`/`Items`/`Bitmap` 与各 `getItem*`/`setItem*` 记录模板
 *     与控件状态、读写都不抛异常，让游戏脚本能走完自己的流程。逐控件的 Win32
 *     模板自绘（编辑框/列表/进度条）尚未实现 —— 这一点与 Kirikiroid2 原版一致，
 *     它的 win32dialog.cpp 同样只有 `messageBox`。
 *
 * 注意：`open()` 是**阻塞**语义（真实 Win32 对话框也是），它最终走到宿主的
 * 消息框队列，用户不点就不会返回。
 */

#include "ncbind.hpp"
#include "tjsCommHead.h"
#include "tjsString.h"
#include "ScriptMgnIntf.h"
#include "CharacterSet.h"

#include <vector>
#include <spdlog/spdlog.h>

#define NCB_MODULE_NAME TJS_W("win32dialog.dll")

namespace {

// UTF-8 内嵌脚本：与 zcompat/k2compat_scripts.cpp 同一套做法（UTF-8 常量转 wide
// 后执行），便于写中文注释与 Win32 常量表。
const char *kWin32DialogScript = R"TJS(
// ─────────────────────────────────────────────────────────────────────────────
// Win32 常量（数值与 Windows SDK 一致；游戏脚本会拿它们做位运算）
// ─────────────────────────────────────────────────────────────────────────────
class WIN32Dialog {
	function messageBox(message, caption, type) {
		if (message === void) message = '';
		if (caption === void) caption = 'Information';
		if (type === void) type = 0;
		var yesno = (type & WIN32Dialog.MB_YESNO) != 0
			|| (type & WIN32Dialog.MB_YESNOCANCEL) != 0;
		var ret = System.inform(message, caption, yesno ? 2 : 1);
		if (yesno) return ret == 0 ? WIN32Dialog.IDYES : WIN32Dialog.IDNO;
		return ret == 0 ? WIN32Dialog.IDOK : WIN32Dialog.IDCANCEL;
	}

	function initCommonControlsEx(classes) { return true; }

	// ── 模板对话框（确认型真实实现 + 其余形状，见文件头说明）───────────────
	function WIN32Dialog(owner) {
		this.owner = owner;
		this.items = [];
		this.results = %[];
		this.title = '';
		this.text = '';
		this.opened = false;
	}

	function store(elm) {
		if (typeof elm != 'Object') return false;
		if (typeof elm.title == 'String') this.title = elm.title;
		if (typeof elm.text == 'String') this.text = elm.text;
		if (typeof elm.items == 'Object') {
			var n = elm.items.count;
			for (var i = 0; i < n; i++) this.addItem(elm.items[i]);
		}
		return true;
	}

	function addItem(item) {
		if (typeof item != 'Object') return false;
		this.items.add(item);
		// 常见写法：静态文本控件承载正文。
		if (this.text == '' && typeof item.text == 'String' && item.text != '') {
			var cls = (typeof item.windowClass == 'String') ? item.windowClass : '';
			if (cls == 'Static' || cls == 'STATIC') this.text = item.text;
		}
		return true;
	}

	// 收集可点击按钮的文案与 ID（按加入顺序；没有按钮时退化成"OK"）。
	function buttonList() {
		var buttons = [], ids = [];
		for (var i = 0; i < this.items.count; i++) {
			var it = this.items[i];
			if (typeof it != 'Object') continue;
			var cls = (typeof it.windowClass == 'String') ? it.windowClass : '';
			if (cls != 'Button' && cls != 'BUTTON') continue;
			var caption = (typeof it.text == 'String' && it.text != '') ? it.text : 'OK';
			var id = (typeof it.id != 'void') ? it.id : WIN32Dialog.IDOK;
			if (typeof id != 'Integer') id = WIN32Dialog.IDOK;
			buttons.add(caption);
			ids.add(id);
		}
		if (buttons.count == 0) {
			buttons.add('OK');
			ids.add(WIN32Dialog.IDOK);
		}
		return [buttons, ids];
	}

	function open() {
		var pair = buttonList();
		var buttons = pair[0];
		var ids = pair[1];
		var caption = (this.title != '') ? this.title : 'Information';
		var body = this.text;
		if (body == '') body = this.title;
		// 宿主消息框支持 1..3 个按钮，多了只留前三个（Win32 常见上限）。
		if (buttons.count > 3) {
			buttons = [buttons[0], buttons[1], buttons[2]];
			ids = [ids[0], ids[1], ids[2]];
		}
		var idx = System.inform(body, caption, buttons);
		if (idx < 0 || idx >= ids.count) idx = 0;
		var result = ids[idx];
		this.results = %[];
		this.results[result] = true;
		this.opened = true;
		// 与真实 Win32Dialog 一致：触发脚本侧命令回调（k2compat 的
		// WIN32DialogEX 会在这里收敛结果）。
		try { this.onCommand(result, 0, 0); } catch (e) { }
		return result;
	}

	function show() { return open(); }

	function close() {
		this.opened = false;
		try { this.onClose(); } catch (e) { }
		return true;
	}

	function finalize() { this.close(); }

	// 控件状态：模板里没有真实控件，用字典记录，读写都不抛异常。
	function itemSlot(id) {
		if (typeof id == 'void' || id === null) return null;
		if (typeof this._slots != 'Object') this._slots = %[];
		var key = id;
		if (typeof this._slots[key] != 'Object') this._slots[key] = %[];
		return this._slots[key];
	}

	function getItem(id) { return this.itemSlot(id); }
	function getItemText(id) {
		var s = this.itemSlot(id);
		return (s != null && typeof s.text == 'String') ? s.text : '';
	}
	function setItemText(id, text) {
		var s = this.itemSlot(id);
		if (s != null) s.text = text;
		return true;
	}
	function getItemInt(id) {
		var s = this.itemSlot(id);
		return (s != null && typeof s.value == 'Integer') ? s.value : 0;
	}
	function setItemInt(id, value) {
		var s = this.itemSlot(id);
		if (s != null) s.value = value;
		return true;
	}
	function getItemEnabled(id) {
		var s = this.itemSlot(id);
		return (s != null && typeof s.enabled == 'Integer') ? s.enabled != 0 : true;
	}
	function setItemEnabled(id, enabled) {
		var s = this.itemSlot(id);
		if (s != null) s.enabled = enabled ? 1 : 0;
		return true;
	}
	function setItemFocus(id) { return true; }
	function setItemBitmap(id, bmp) {
		var s = this.itemSlot(id);
		if (s != null) s.bitmap = bmp;
		return true;
	}
	function setItemPos(id, x, y) {
		var s = this.itemSlot(id);
		if (s != null) { s.x = x; s.y = y; }
		return true;
	}
	function setItemSize(id, w, h) {
		var s = this.itemSlot(id);
		if (s != null) { s.w = w; s.h = h; }
		return true;
	}
	function setPos(x, y) { this.x = x; this.y = y; return true; }
	function setSize(w, h) { this.w = w; this.h = h; return true; }
	function getDialogTemplate() { return this; }
	function loadResource(name) { return true; }

	// 事件回调：真实实现由脚本子类覆盖（k2compat 的 WIN32DialogEX 就是），
	// 基类给空实现，避免子类尚未覆盖时被调用即报错。
	function onInit() { return true; }
	function onCommand(id, code, ctrl) { return 0; }
	function onNotify(id, code, ctrl) { return 0; }
	function onSize(w, h) { return true; }
	function onClose() { return true; }
	function onHScroll(code, pos, ctrl) { return 0; }
	function onVScroll(code, pos, ctrl) { return 0; }

	// 地址类接口：模板解析偶尔会用到，返回 null 而不是报错。
	function getOctetAddress(octet, offset) { return null; }
	function getStringAddress(str, offset) { return null; }
}

// 模板/控件容器：脚本用 new WIN32Dialog.Header() / .Items() 构造并 store/add。
class WIN32DialogHeader {
	function WIN32DialogHeader() { this.style = 0; this.exStyle = 0; }
	// 显式键拷贝（不用 for..in：TJS 方言差异不值得为它冒运行时语法错误的风险，
	// 一旦脚本整体抛异常，游戏连 messageBox 都用不上了）。
	function store(elm) {
		if (typeof elm != 'Object') return false;
		var keys = ['style', 'exStyle', 'x', 'y', 'cx', 'cy', 'title', 'font',
			'pointSize', 'weight', 'italic', 'charset', 'helpID'];
		for (var i = 0; i < keys.count; i++) {
			var key = keys[i];
			if (typeof elm[key] != 'void') {
				try { this[key] = elm[key]; } catch (e) { }
			}
		}
		return true;
	}
	function save() { return true; }
	function loadResource() { return true; }
}

class WIN32DialogItems {
	function WIN32DialogItems() { this._items = []; }
	function add(item) { this._items.add(item); return true; }
	function clear() { this._items.clear(); return true; }
	property count { getter { return this._items.count; } }
	property items { getter { return this._items; } }
}

class WIN32DialogBitmap {
	function WIN32DialogBitmap(...) { }
	function load(...) { return true; }
	function save(...) { return true; }
	function getWidth() { return 0; }
	function getHeight() { return 0; }
}

WIN32Dialog.Header = WIN32DialogHeader;
WIN32Dialog.Items  = WIN32DialogItems;
WIN32Dialog.Bitmap = WIN32DialogBitmap;

WIN32Dialog.MB_OK = 0;
WIN32Dialog.MB_OKCANCEL = 1;
WIN32Dialog.MB_ABORTRETRYIGNORE = 2;
WIN32Dialog.MB_YESNOCANCEL = 3;
WIN32Dialog.MB_YESNO = 4;
WIN32Dialog.MB_RETRYCANCEL = 5;
WIN32Dialog.MB_CANCELTRYCONTINUE = 6;
WIN32Dialog.MB_ICONHAND = 0x10;
WIN32Dialog.MB_ICONSTOP = 0x10;
WIN32Dialog.MB_ICONERROR = 0x10;
WIN32Dialog.MB_ICONQUESTION = 0x20;
WIN32Dialog.MB_ICONEXCLAMATION = 0x30;
WIN32Dialog.MB_ICONWARNING = 0x30;
WIN32Dialog.MB_ICONASTERISK = 0x40;
WIN32Dialog.MB_ICONINFORMATION = 0x40;
WIN32Dialog.MB_DEFBUTTON1 = 0;
WIN32Dialog.MB_DEFBUTTON2 = 0x100;
WIN32Dialog.MB_DEFBUTTON3 = 0x200;
WIN32Dialog.MB_DEFBUTTON4 = 0x300;
WIN32Dialog.DS_SETFONT = 0x40;
WIN32Dialog.DS_MODALFRAME = 0x80;
WIN32Dialog.WS_POPUP = 0x80000000;
WIN32Dialog.WS_CAPTION = 0x00c00000;
WIN32Dialog.WS_SYSMENU = 0x00080000;
WIN32Dialog.WS_CHILD = 0x40000000;
WIN32Dialog.WS_VISIBLE = 0x10000000;
WIN32Dialog.WS_TABSTOP = 0x00010000;
WIN32Dialog.WS_GROUP = 0x00020000;
WIN32Dialog.WS_BORDER = 0x00800000;
WIN32Dialog.FW_DONTCARE = 0;
WIN32Dialog.FW_THIN = 100;
WIN32Dialog.FW_EXTRALIGHT = 200;
WIN32Dialog.FW_LIGHT = 300;
WIN32Dialog.FW_NORMAL = 400;
WIN32Dialog.FW_MEDIUM = 500;
WIN32Dialog.FW_SEMIBOLD = 600;
WIN32Dialog.FW_BOLD = 700;
WIN32Dialog.FW_EXTRABOLD = 800;
WIN32Dialog.FW_HEAVY = 900;
WIN32Dialog.ICC_BAR_CLASSES = 0x00000004;
WIN32Dialog.IDOK = 1;
WIN32Dialog.IDCANCEL = 2;
WIN32Dialog.IDABORT = 3;
WIN32Dialog.IDRETRY = 4;
WIN32Dialog.IDIGNORE = 5;
WIN32Dialog.IDYES = 6;
WIN32Dialog.IDNO = 7;
WIN32Dialog.IDCLOSE = 8;
WIN32Dialog.IDHELP = 9;
WIN32Dialog.IDTRYAGAIN = 10;
WIN32Dialog.IDCONTINUE = 11;
)TJS";

void ExecUtf8Script(const char *utf8, const char *name) {
    if(!utf8 || !*utf8)
        return;
    tjs_int len = TVPUtf8ToWideCharString(utf8, nullptr);
    if(len < 0) {
        spdlog::error("win32dialog: invalid UTF-8 in {}", name);
        return;
    }
    std::vector<tjs_char> buf(static_cast<size_t>(len) + 1);
    TVPUtf8ToWideCharString(utf8, buf.data());
    buf[len] = 0;
    try {
        TVPExecuteScript(ttstr(buf.data()));
    } catch(...) {
        // 抛出去会让整个插件注册失败（游戏侧连 messageBox 都没有），这里挡住。
        spdlog::error("win32dialog: 执行内嵌脚本 {} 抛异常", name);
    }
}

} // namespace

static void InitPlugin_WIN32Dialog() {
    ExecUtf8Script(kWin32DialogScript, "win32dialog/tjs");
    spdlog::info("win32dialog: 已注册 WIN32Dialog（messageBox 与 open 走宿主对话框；"
                 "Header/Items/getItem*/setItem* 为形状实现）");
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_WIN32Dialog);
