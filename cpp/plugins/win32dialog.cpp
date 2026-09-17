/**
 * @file win32dialog.cpp
 * @brief win32dialog.dll —— Windows 对话框（确认框 / 模板对话框）的宿主实现。
 *
 * 为什么要补齐：游戏侧（KiriKiri Z 自带 `win32dialog.tjs`、k2compat 内嵌的同名
 * 脚本、以及大量老游戏的对话框架子）会用到 `WIN32Dialog.Header` /
 * `WIN32Dialog.Items` / `getItem*` / `setItem*` / `open()` 这一整套 Win32 形状的
 * API。只注册 `messageBox` 时，脚本一碰这些成员就是 `Member ... does not exist`，
 * 对话框架不起来（真机：部分老游戏与 krkrz 游戏的"确认"窗口）。
 *
 * ⚠️ 实现方式刻意用 **C++/NCB**，不用内嵌 TJS：内嵌脚本没有编译期检查，一个语法
 * 错误在真机上就是 TJS 解析失败 + SIGSEGV（本项目踩过一次，整机闪退），而 C++ 版本
 * 由 CI 编译校验。
 *
 * 能力边界（≥ 参考实现 Kirikiroid2 / AetherKiri 的 win32dialog，二者只有 messageBox）：
 *   * `messageBox` —— 真实：`TVPShowSimpleMessageBox` → 宿主壳原生确认框，按 Win32
 *     MB_* / ID* 语义返回；兼容 3 参与 4 参两种调用形态。
 *   * `open()` / `show()` —— 真实（确认型）：把模板里的标题/正文/按钮文案交给宿主
 *     消息框，按用户点击返回对应 ID，并回调脚本 `onCommand(id, 0, 0)`；
 *     **阻塞语义**与真实 Win32 对话框一致（用户不点不返回）。
 *   * `Header` / `Items` / `Bitmap`、`store` / `addItem`、`getItem*` / `setItem*`、
 *     `setPos` / `setSize` / `getDialogTemplate` / `loadResource` —— 记录模板与控件
 *     状态、读写都不抛异常的"形状实现"。逐控件的 Win32 模板自绘（编辑框/列表/
 *     进度条）仍未实现，等真机日志显示游戏真正用到哪些控件再补。
 *
 * NCB 约定：暴露给 TJS 的方法必须是 **static + 原始回调签名**
 * `(result, numparams, param, objthis)`（`ncbRawCallbackMethod` 只接受普通函数
 * 指针，成员函数指针不支持）；实例状态用
 * `ncbInstanceAdaptor<T>::GetNativeInstance(objthis)` 取回，与 motionplayer 等既有
 * 插件同一套写法。
 */

#include "ncbind.hpp"
#include "MsgIntf.h"

#include <map>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#define NCB_MODULE_NAME TJS_W("win32dialog.dll")

// 取回本机实例；失败即报"native class crash"（与既有插件一致）。
#define DLG_SELF(Type, name)                                            \
    Type *name = ncbInstanceAdaptor<Type>::GetNativeInstance(objthis);  \
    if(!name)                                                           \
        return TJS_E_NATIVECLASSCRASH

namespace {

// Win32 常量（数值与 Windows SDK 一致；游戏脚本会拿它们做位运算）。
#define WIN32DLG_CONST(name, value)                                       \
    static tjs_error Const_##name(tTJSVariant *r, tjs_int, tTJSVariant **, \
                                  iTJSDispatch2 *) {                      \
        if(r)                                                            \
            *r = tTJSVariant(static_cast<tjs_int>(value));                 \
        return TJS_S_OK;                                                  \
    }

WIN32DLG_CONST(MB_OK, 0)
WIN32DLG_CONST(MB_OKCANCEL, 1)
WIN32DLG_CONST(MB_ABORTRETRYIGNORE, 2)
WIN32DLG_CONST(MB_YESNOCANCEL, 3)
WIN32DLG_CONST(MB_YESNO, 4)
WIN32DLG_CONST(MB_RETRYCANCEL, 5)
WIN32DLG_CONST(MB_CANCELTRYCONTINUE, 6)
WIN32DLG_CONST(MB_ICONHAND, 0x10)
WIN32DLG_CONST(MB_ICONSTOP, 0x10)
WIN32DLG_CONST(MB_ICONERROR, 0x10)
WIN32DLG_CONST(MB_ICONQUESTION, 0x20)
WIN32DLG_CONST(MB_ICONEXCLAMATION, 0x30)
WIN32DLG_CONST(MB_ICONWARNING, 0x30)
WIN32DLG_CONST(MB_ICONASTERISK, 0x40)
WIN32DLG_CONST(MB_ICONINFORMATION, 0x40)
WIN32DLG_CONST(MB_DEFBUTTON1, 0)
WIN32DLG_CONST(MB_DEFBUTTON2, 0x100)
WIN32DLG_CONST(MB_DEFBUTTON3, 0x200)
WIN32DLG_CONST(MB_DEFBUTTON4, 0x300)
WIN32DLG_CONST(DS_SETFONT, 0x40)
WIN32DLG_CONST(DS_MODALFRAME, 0x80)
WIN32DLG_CONST(WS_POPUP, 0x80000000)
WIN32DLG_CONST(WS_CAPTION, 0x00c00000)
WIN32DLG_CONST(WS_SYSMENU, 0x00080000)
WIN32DLG_CONST(WS_CHILD, 0x40000000)
WIN32DLG_CONST(WS_VISIBLE, 0x10000000)
WIN32DLG_CONST(WS_TABSTOP, 0x00010000)
WIN32DLG_CONST(WS_GROUP, 0x00020000)
WIN32DLG_CONST(WS_BORDER, 0x00800000)
WIN32DLG_CONST(FW_DONTCARE, 0)
WIN32DLG_CONST(FW_THIN, 100)
WIN32DLG_CONST(FW_EXTRALIGHT, 200)
WIN32DLG_CONST(FW_LIGHT, 300)
WIN32DLG_CONST(FW_NORMAL, 400)
WIN32DLG_CONST(FW_MEDIUM, 500)
WIN32DLG_CONST(FW_SEMIBOLD, 600)
WIN32DLG_CONST(FW_BOLD, 700)
WIN32DLG_CONST(FW_EXTRABOLD, 800)
WIN32DLG_CONST(FW_HEAVY, 900)
WIN32DLG_CONST(ICC_BAR_CLASSES, 0x00000004)
WIN32DLG_CONST(IDOK, 1)
WIN32DLG_CONST(IDCANCEL, 2)
WIN32DLG_CONST(IDABORT, 3)
WIN32DLG_CONST(IDRETRY, 4)
WIN32DLG_CONST(IDIGNORE, 5)
WIN32DLG_CONST(IDYES, 6)
WIN32DLG_CONST(IDNO, 7)
WIN32DLG_CONST(IDCLOSE, 8)
WIN32DLG_CONST(IDHELP, 9)
WIN32DLG_CONST(IDTRYAGAIN, 10)
WIN32DLG_CONST(IDCONTINUE, 11)

#undef WIN32DLG_CONST

// 取对象字符串属性；不存在/类型不符时返回 fallback。
ttstr PropText(iTJSDispatch2 *obj, const tjs_char *name, const ttstr &fallback) {
    if(!obj)
        return fallback;
    tTJSVariant v;
    if(TJS_SUCCEEDED(obj->PropGet(0, name, nullptr, &v, obj)) &&
       v.Type() != tvtVoid)
        return ttstr(v);
    return fallback;
}

bool PropInt(iTJSDispatch2 *obj, const tjs_char *name, tjs_int &out) {
    if(!obj)
        return false;
    tTJSVariant v;
    if(TJS_SUCCEEDED(obj->PropGet(0, name, nullptr, &v, obj)) &&
       v.Type() == tvtInteger) {
        out = static_cast<tjs_int>(v);
        return true;
    }
    return false;
}

bool IsButtonClass(const ttstr &cls) {
    return cls == TJS_W("Button") || cls == TJS_W("BUTTON");
}

// 无状态的通用回调（常量、no-op）。签名与实例方法一致，NCB 只要求普通函数指针。
tjs_error CbTrue(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    if(r)
        *r = tTJSVariant(true);
    return TJS_S_OK;
}

tjs_error CbZero(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    if(r)
        *r = tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}

tjs_error CbNull(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    if(r)
        r->Clear();
    return TJS_S_OK;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// WIN32Dialog.Header —— 模板头（形状实现）
// ─────────────────────────────────────────────────────────────────────────────
class WIN32DialogHeader {
public:
    WIN32DialogHeader() = default;

    static tjs_error Store(tTJSVariant *, tjs_int numparams,
                           tTJSVariant **param, iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32DialogHeader, self);
        if(numparams < 1 || !param[0] || param[0]->Type() != tvtObject)
            return TJS_S_OK;
        iTJSDispatch2 *elm = param[0]->AsObjectNoAddRef();
        static const tjs_char *kKeys[] = {
            TJS_W("style"),  TJS_W("exStyle"), TJS_W("x"),
            TJS_W("y"),      TJS_W("cx"),      TJS_W("cy"),
            TJS_W("title"),  TJS_W("font"),    TJS_W("pointSize"),
            TJS_W("weight"), TJS_W("italic"),  TJS_W("charset"),
            TJS_W("helpID"),
        };
        for(const tjs_char *key : kKeys) {
            tTJSVariant v;
            if(TJS_SUCCEEDED(elm->PropGet(0, key, nullptr, &v, elm)) &&
               v.Type() != tvtVoid)
                self->_values[key] = v;
        }
        return TJS_S_OK;
    }

    static tjs_error GetField(tTJSVariant *r, tjs_int numparams,
                              tTJSVariant **param, iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32DialogHeader, self);
        if(!r)
            return TJS_S_OK;
        if(numparams < 1 || !param[0]) {
            r->Clear();
            return TJS_S_OK;
        }
        const ttstr key(*param[0]);
        auto it = self->_values.find(key);
        if(it == self->_values.end())
            r->Clear();
        else
            *r = it->second;
        return TJS_S_OK;
    }

private:
    std::map<ttstr, tTJSVariant> _values;
};

// ─────────────────────────────────────────────────────────────────────────────
// WIN32Dialog.Items —— 控件容器（形状实现）
// ─────────────────────────────────────────────────────────────────────────────
class WIN32DialogItems {
public:
    WIN32DialogItems() = default;

    static tjs_error Add(tTJSVariant *r, tjs_int numparams, tTJSVariant **param,
                         iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32DialogItems, self);
        if(numparams >= 1 && param[0] && param[0]->Type() == tvtObject)
            self->_items.push_back(*param[0]);
        if(r)
            *r = tTJSVariant(true);
        return TJS_S_OK;
    }

    static tjs_error Clear(tTJSVariant *r, tjs_int, tTJSVariant **,
                           iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32DialogItems, self);
        self->_items.clear();
        if(r)
            *r = tTJSVariant(true);
        return TJS_S_OK;
    }

    static tjs_error GetCount(tTJSVariant *r, tjs_int, tTJSVariant **,
                              iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32DialogItems, self);
        if(r)
            *r = tTJSVariant(static_cast<tjs_int>(self->_items.size()));
        return TJS_S_OK;
    }

    /** at(i)：脚本里除了 count 也会按下标取项。 */
    static tjs_error GetAt(tTJSVariant *r, tjs_int numparams,
                           tTJSVariant **param, iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32DialogItems, self);
        if(!r)
            return TJS_S_OK;
        if(numparams < 1 || !param[0] || param[0]->Type() != tvtInteger) {
            r->Clear();
            return TJS_S_OK;
        }
        const tjs_int idx = static_cast<tjs_int>(*param[0]);
        if(idx < 0 || idx >= static_cast<tjs_int>(self->_items.size()))
            r->Clear();
        else
            *r = self->_items[static_cast<size_t>(idx)];
        return TJS_S_OK;
    }

private:
    std::vector<tTJSVariant> _items;
};

// ─────────────────────────────────────────────────────────────────────────────
// WIN32Dialog.Bitmap —— 位图容器（形状实现，注册时复用 CbTrue/CbZero）
// ─────────────────────────────────────────────────────────────────────────────
class WIN32DialogBitmap {
public:
    WIN32DialogBitmap() = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// WIN32Dialog —— 对话框本体
// ─────────────────────────────────────────────────────────────────────────────
class WIN32Dialog {
public:
    // 无参构造：NCB 的实参校验是"至少这么多个"（ncbind.hpp: numparams < ArgsCount），
    // 所以 0 参构造能同时接受 `new WIN32Dialog()`、`new WIN32Dialog(null)`、
    // `new WIN32Dialog(this)` —— 游戏侧三种写法都有。
    WIN32Dialog() = default;
    ~WIN32Dialog() = default;

    static tjs_error MessageBox(tTJSVariant *r, tjs_int numparams,
                                tTJSVariant **param, iTJSDispatch2 *) {
        // 兼容两种调用形态：(message, caption, type) 与
        // (owner, message, caption, type)（老脚本两种都有）。
        ttstr message, caption(TJS_W("Information"));
        tjs_int type = 0;
        if(numparams >= 4) {
            message = (param[1] && param[1]->Type() != tvtVoid)
                ? ttstr(*param[1])
                : ttstr();
            if(param[2] && param[2]->Type() != tvtVoid)
                caption = ttstr(*param[2]);
            if(param[3] && param[3]->Type() == tvtInteger)
                type = static_cast<tjs_int>(*param[3]);
        } else {
            if(numparams >= 1 && param[0] && param[0]->Type() != tvtVoid)
                message = ttstr(*param[0]);
            if(numparams >= 2 && param[1] && param[1]->Type() != tvtVoid)
                caption = ttstr(*param[1]);
            if(numparams >= 3 && param[2] && param[2]->Type() == tvtInteger)
                type = static_cast<tjs_int>(*param[2]);
        }

        // MB_YESNO(4) / MB_YESNOCANCEL(3)：按"是/否"给按钮。
        const bool yesNo = (type & 4) != 0 || (type & 3) == 3;
        std::vector<ttstr> buttons;
        if(yesNo) {
            buttons.emplace_back(TJS_W("Yes"));
            buttons.emplace_back(TJS_W("No"));
        } else if((type & 1) != 0) {
            buttons.emplace_back(TJS_W("OK"));
            buttons.emplace_back(TJS_W("Cancel"));
        } else {
            buttons.emplace_back(TJS_W("OK"));
        }
        const int ret = TVPShowSimpleMessageBox(message, caption, buttons);
        tjs_int id;
        if(yesNo)
            id = (ret == 0) ? 6 /*IDYES*/ : 7 /*IDNO*/;
        else if(buttons.size() >= 2)
            id = (ret == 0) ? 1 /*IDOK*/ : 2 /*IDCANCEL*/;
        else
            id = 1; // IDOK
        if(r)
            *r = tTJSVariant(id);
        return TJS_S_OK;
    }

    static tjs_error Store(tTJSVariant *r, tjs_int numparams,
                           tTJSVariant **param, iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32Dialog, self);
        if(numparams >= 1 && param[0] && param[0]->Type() == tvtObject) {
            iTJSDispatch2 *elm = param[0]->AsObjectNoAddRef();
            self->_title = PropText(elm, TJS_W("title"), self->_title);
            self->_text = PropText(elm, TJS_W("text"), self->_text);
            tTJSVariant items;
            if(TJS_SUCCEEDED(
                   elm->PropGet(0, TJS_W("items"), nullptr, &items, elm)) &&
               items.Type() == tvtObject) {
                iTJSDispatch2 *arr = items.AsObjectNoAddRef();
                tjs_int n = 0;
                if(!PropInt(arr, TJS_W("count"), n))
                    PropInt(arr, TJS_W("length"), n);
                for(tjs_int i = 0; i < n; i++) {
                    tTJSVariant item;
                    if(TJS_SUCCEEDED(
                           arr->PropGet(i, nullptr, nullptr, &item, arr)) &&
                       item.Type() == tvtObject)
                        self->_items.push_back(item);
                }
            }
        }
        if(r)
            *r = tTJSVariant(true);
        return TJS_S_OK;
    }

    static tjs_error AddItem(tTJSVariant *r, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32Dialog, self);
        if(numparams >= 1 && param[0] && param[0]->Type() == tvtObject) {
            iTJSDispatch2 *it = param[0]->AsObjectNoAddRef();
            self->_items.push_back(*param[0]);
            if(self->_text.IsEmpty())
                self->_text = PropText(it, TJS_W("text"), self->_text);
        }
        if(r)
            *r = tTJSVariant(true);
        return TJS_S_OK;
    }

    /** 模板里同 ID 的控件对象；没有就返回 nullptr。 */
    static iTJSDispatch2 *FindItem(WIN32Dialog *self, tjs_int id) {
        for(const tTJSVariant &v : self->_items) {
            if(v.Type() != tvtObject)
                continue;
            iTJSDispatch2 *it = v.AsObjectNoAddRef();
            tjs_int itemId = -1;
            if(PropInt(it, TJS_W("id"), itemId) && itemId == id)
                return it;
        }
        return nullptr;
    }

    static tjs_error Open(tTJSVariant *r, tjs_int, tTJSVariant **,
                          iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32Dialog, self);
        std::vector<ttstr> labels;
        std::vector<tjs_int> ids;
        for(const tTJSVariant &v : self->_items) {
            if(v.Type() != tvtObject)
                continue;
            iTJSDispatch2 *it = v.AsObjectNoAddRef();
            if(!IsButtonClass(PropText(it, TJS_W("windowClass"), ttstr())))
                continue;
            ttstr caption = PropText(it, TJS_W("text"), ttstr(TJS_W("OK")));
            if(caption.IsEmpty())
                caption = TJS_W("OK");
            tjs_int id = 1;
            if(!PropInt(it, TJS_W("id"), id))
                id = 1;
            labels.emplace_back(caption);
            ids.emplace_back(id);
        }
        if(labels.empty()) {
            labels.emplace_back(TJS_W("OK"));
            ids.emplace_back(1);
        }
        // 宿主消息框最多三个按钮，多了取前三个。
        if(labels.size() > 3) {
            labels.resize(3);
            ids.resize(3);
        }
        const ttstr caption = self->_title.IsEmpty()
            ? ttstr(TJS_W("Information"))
            : self->_title;
        const ttstr body = self->_text.IsEmpty() ? caption : self->_text;
        const int idx = TVPShowSimpleMessageBox(body, caption, labels);
        const tjs_int id =
            (idx >= 0 && idx < static_cast<int>(ids.size())) ? ids[idx] : ids[0];
        self->_opened = true;

        // 与真实 Win32Dialog 一致：回调脚本 onCommand(id, 0, 0)，
        // k2compat 的 WIN32DialogEX 就在这里收敛结果。
        if(objthis) {
            tTJSVariant args[3] = { tTJSVariant(id), tTJSVariant((tjs_int)0),
                                    tTJSVariant((tjs_int)0) };
            tTJSVariant *argv[3] = { &args[0], &args[1], &args[2] };
            try {
                objthis->FuncCall(0, TJS_W("onCommand"), nullptr, nullptr, 3,
                                  argv, objthis);
            } catch(...) {
                // 脚本自己的 onCommand 抛错不影响返回值。
            }
        }
        if(r)
            *r = tTJSVariant(id);
        return TJS_S_OK;
    }

    static tjs_error Close(tTJSVariant *r, tjs_int, tTJSVariant **,
                           iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32Dialog, self);
        self->_opened = false;
        if(objthis) {
            try {
                objthis->FuncCall(0, TJS_W("onClose"), nullptr, nullptr, 0,
                                  nullptr, objthis);
            } catch(...) {
            }
        }
        if(r)
            *r = tTJSVariant(true);
        return TJS_S_OK;
    }

    static tjs_error GetItem(tTJSVariant *r, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32Dialog, self);
        if(!r)
            return TJS_S_OK;
        tjs_int id = 0;
        if(numparams >= 1 && param[0] && param[0]->Type() == tvtInteger)
            id = static_cast<tjs_int>(*param[0]);
        if(iTJSDispatch2 *it = FindItem(self, id)) {
            *r = tTJSVariant(it, it);
            return TJS_S_OK;
        }
        *r = self->_slots[id];
        return TJS_S_OK;
    }

    static tjs_error GetItemText(tTJSVariant *r, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32Dialog, self);
        tjs_int id = 0;
        if(numparams >= 1 && param[0] && param[0]->Type() == tvtInteger)
            id = static_cast<tjs_int>(*param[0]);
        if(iTJSDispatch2 *it = FindItem(self, id)) {
            if(r)
                *r = tTJSVariant(PropText(it, TJS_W("text"), ttstr()));
            return TJS_S_OK;
        }
        if(r)
            *r = tTJSVariant(ttstr());
        return TJS_S_OK;
    }

    static tjs_error SetItemText(tTJSVariant *r, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32Dialog, self);
        if(numparams >= 2 && param[0] && param[0]->Type() == tvtInteger &&
           param[1]) {
            if(iTJSDispatch2 *it = FindItem(self, static_cast<tjs_int>(*param[0])))
                it->PropSet(TJS_MEMBERENSURE, TJS_W("text"), nullptr, param[1],
                            it);
        }
        if(r)
            *r = tTJSVariant(true);
        return TJS_S_OK;
    }

    static tjs_error GetOpened(tTJSVariant *r, tjs_int, tTJSVariant **,
                               iTJSDispatch2 *objthis) {
        DLG_SELF(WIN32Dialog, self);
        if(r)
            *r = tTJSVariant(self->_opened);
        return TJS_S_OK;
    }

private:
    tTJSVariant _title;
    tTJSVariant _text;
    std::vector<tTJSVariant> _items;
    std::map<tjs_int, tTJSVariant> _slots;
    bool _opened = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// 注册
// ─────────────────────────────────────────────────────────────────────────────
NCB_REGISTER_SUBCLASS_DELAY(WIN32DialogHeader) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD_RAW_CALLBACK(store, &WIN32DialogHeader::Store, 0);
    NCB_METHOD_RAW_CALLBACK(getField, &WIN32DialogHeader::GetField, 0);
    NCB_METHOD_RAW_CALLBACK(save, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(loadResource, &CbTrue, 0);
}

NCB_REGISTER_SUBCLASS_DELAY(WIN32DialogItems) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD_RAW_CALLBACK(add, &WIN32DialogItems::Add, 0);
    NCB_METHOD_RAW_CALLBACK(clear, &WIN32DialogItems::Clear, 0);
    NCB_METHOD_RAW_CALLBACK(at, &WIN32DialogItems::GetAt, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(count, &WIN32DialogItems::GetCount, 0);
}

NCB_REGISTER_SUBCLASS_DELAY(WIN32DialogBitmap) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD_RAW_CALLBACK(load, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(save, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(getWidth, &CbZero, 0);
    NCB_METHOD_RAW_CALLBACK(getHeight, &CbZero, 0);
}

NCB_REGISTER_CLASS(WIN32Dialog) {
    NCB_CONSTRUCTOR(());

    NCB_METHOD_RAW_CALLBACK(messageBox, &WIN32Dialog::MessageBox, 0);
    NCB_METHOD_RAW_CALLBACK(initCommonControlsEx, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(store, &WIN32Dialog::Store, 0);
    NCB_METHOD_RAW_CALLBACK(addItem, &WIN32Dialog::AddItem, 0);
    NCB_METHOD_RAW_CALLBACK(open, &WIN32Dialog::Open, 0);
    NCB_METHOD_RAW_CALLBACK(show, &WIN32Dialog::Open, 0);
    NCB_METHOD_RAW_CALLBACK(close, &WIN32Dialog::Close, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &WIN32Dialog::Close, 0);
    NCB_METHOD_RAW_CALLBACK(getItem, &WIN32Dialog::GetItem, 0);
    NCB_METHOD_RAW_CALLBACK(getItemText, &WIN32Dialog::GetItemText, 0);
    NCB_METHOD_RAW_CALLBACK(setItemText, &WIN32Dialog::SetItemText, 0);
    NCB_METHOD_RAW_CALLBACK(getItemInt, &CbZero, 0);
    NCB_METHOD_RAW_CALLBACK(setItemInt, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(getItemEnabled, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(setItemEnabled, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(setItemFocus, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(setItemBitmap, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(setItemPos, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(setItemSize, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(setPos, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(setSize, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(getDialogTemplate, &CbNull, 0);
    NCB_METHOD_RAW_CALLBACK(loadResource, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(onInit, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(onCommand, &CbZero, 0);
    NCB_METHOD_RAW_CALLBACK(onNotify, &CbZero, 0);
    NCB_METHOD_RAW_CALLBACK(onSize, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(onClose, &CbTrue, 0);
    NCB_METHOD_RAW_CALLBACK(onHScroll, &CbZero, 0);
    NCB_METHOD_RAW_CALLBACK(onVScroll, &CbZero, 0);
    NCB_METHOD_RAW_CALLBACK(getOctetAddress, &CbNull, 0);
    NCB_METHOD_RAW_CALLBACK(getStringAddress, &CbNull, 0);

    NCB_PROPERTY_RAW_CALLBACK_RO(opened, &WIN32Dialog::GetOpened, 0);

    NCB_SUBCLASS(Header, WIN32DialogHeader);
    NCB_SUBCLASS(Items, WIN32DialogItems);
    NCB_SUBCLASS(Bitmap, WIN32DialogBitmap);

    // Win32 常量（脚本用来位运算/比较，必须是静态成员而不是实例属性）。
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_OK, Const_MB_OK, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_OKCANCEL, Const_MB_OKCANCEL,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_ABORTRETRYIGNORE, Const_MB_ABORTRETRYIGNORE,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_YESNOCANCEL, Const_MB_YESNOCANCEL,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_YESNO, Const_MB_YESNO, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_RETRYCANCEL, Const_MB_RETRYCANCEL,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_CANCELTRYCONTINUE,
                                 Const_MB_CANCELTRYCONTINUE, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_ICONHAND, Const_MB_ICONHAND,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_ICONSTOP, Const_MB_ICONSTOP,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_ICONERROR, Const_MB_ICONERROR,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_ICONQUESTION, Const_MB_ICONQUESTION,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_ICONEXCLAMATION, Const_MB_ICONEXCLAMATION,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_ICONWARNING, Const_MB_ICONWARNING,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_ICONASTERISK, Const_MB_ICONASTERISK,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_ICONINFORMATION, Const_MB_ICONINFORMATION,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_DEFBUTTON1, Const_MB_DEFBUTTON1,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_DEFBUTTON2, Const_MB_DEFBUTTON2,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_DEFBUTTON3, Const_MB_DEFBUTTON3,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MB_DEFBUTTON4, Const_MB_DEFBUTTON4,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(DS_SETFONT, Const_DS_SETFONT,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(DS_MODALFRAME, Const_DS_MODALFRAME,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(WS_POPUP, Const_WS_POPUP, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(WS_CAPTION, Const_WS_CAPTION,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(WS_SYSMENU, Const_WS_SYSMENU,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(WS_CHILD, Const_WS_CHILD, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(WS_VISIBLE, Const_WS_VISIBLE,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(WS_TABSTOP, Const_WS_TABSTOP,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(WS_GROUP, Const_WS_GROUP, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(WS_BORDER, Const_WS_BORDER, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(FW_DONTCARE, Const_FW_DONTCARE,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(FW_THIN, Const_FW_THIN, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(FW_EXTRALIGHT, Const_FW_EXTRALIGHT,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(FW_LIGHT, Const_FW_LIGHT, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(FW_NORMAL, Const_FW_NORMAL, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(FW_MEDIUM, Const_FW_MEDIUM, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(FW_SEMIBOLD, Const_FW_SEMIBOLD,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(FW_BOLD, Const_FW_BOLD, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(FW_EXTRABOLD, Const_FW_EXTRABOLD,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(FW_HEAVY, Const_FW_HEAVY, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ICC_BAR_CLASSES, Const_ICC_BAR_CLASSES,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(IDOK, Const_IDOK, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(IDCANCEL, Const_IDCANCEL, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(IDABORT, Const_IDABORT, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(IDRETRY, Const_IDRETRY, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(IDIGNORE, Const_IDIGNORE, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(IDYES, Const_IDYES, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(IDNO, Const_IDNO, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(IDCLOSE, Const_IDCLOSE, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(IDHELP, Const_IDHELP, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(IDTRYAGAIN, Const_IDTRYAGAIN,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(IDCONTINUE, Const_IDCONTINUE,
                                 TJS_STATICMEMBER);
}

static void InitPlugin_WIN32Dialog() {
    spdlog::info("win32dialog: WIN32Dialog 已注册（messageBox/open 走宿主对话框；"
                 "Header/Items/getItem*/setItem* 为形状实现）");
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_WIN32Dialog);
