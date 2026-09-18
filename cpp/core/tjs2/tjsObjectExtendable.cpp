//---------------------------------------------------------------------------
// TJS2 "ExtendableObject" class implementation
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "tjsObjectExtendable.h"

namespace TJS {
    // 启动期"可写名"白名单（8 个）。
    //
    // 为什么需要：KAG 系脚本在启动阶段会写 `KAGWindow.OGLDrawDevice = …`、
    // `kag.drawDevice = …` 这类成员，而 ExtendableObject 的 PropSet 对**尚未存在**的成员
    // 会返回 ACCESSDENYED / MEMBERNOTFOUND ⇒ 脚本侧写入被静默吞掉（"看着有、其实没用"）。
    // 上游 AetherKiri 用这张固定白名单放行（tjsObjectExtendable.cpp:9-19 + :96-105），
    // 本仓库此前没有，只能从 C++ 侧绕开（krkrgles.cpp 的 PropSet）。
    //
    // 名单是**固定的 8 个名字**，不是"任意成员都允许"，所以不会掩盖拼写错误。
    // 用户裁决：这一项两层都要（与 A 块的其它回退不同，不需要按层开关）。
    static bool TJSIsStartupCompatWritableNameEx(const tjs_char *membername) {
        return membername &&
               (!TJS_strcmp(membername, TJS_W("debugWindowEnabled")) ||
                !TJS_strcmp(membername, TJS_W("inXP3archivePacked")) ||
                !TJS_strcmp(membername, TJS_W("convertMode")) ||
                !TJS_strcmp(membername, TJS_W("drawDevice")) ||
                !TJS_strcmp(membername, TJS_W("gpuDrawDevice")) ||
                !TJS_strcmp(membername, TJS_W("nativeDrawDevice")) ||
                !TJS_strcmp(membername, TJS_W("OGLDrawDevice")) ||
                !TJS_strcmp(membername, TJS_W("GLESAdaptor")));
    }

    tTJSExtendableObject::~tTJSExtendableObject() {
        if(SuperClass) {
            SuperClass->Release();
            SuperClass = nullptr;
        }
    }

    void tTJSExtendableObject::SetSuper(iTJSDispatch2 *dsp) {
        if(SuperClass) {
            SuperClass->Release();
            SuperClass = nullptr;
        }
        SuperClass = dsp;
        SuperClass->AddRef();
    }

    void tTJSExtendableObject::ExtendsClass(iTJSDispatch2 *global,
                                            const ttstr &classname) {
        tTJSVariant val;
        tjs_error er = global->PropGet(TJS_MEMBERMUSTEXIST, classname.c_str(),
                                       nullptr, &val, global);
        if(TJS_FAILED(er))
            TJSThrowFrom_tjs_error(er, classname.c_str());

        SetSuper(val.AsObjectNoAddRef());
    }

    tjs_error
    tTJSExtendableObject::FuncCall(tjs_uint32 flag, const tjs_char *membername,
                                   tjs_uint32 *hint, tTJSVariant *result,
                                   tjs_int numparams, tTJSVariant **param,
                                   iTJSDispatch2 *objthis) {
        tjs_error hr = inherited::FuncCall(flag, membername, hint, result,
                                           numparams, param, objthis);
        if(hr == TJS_E_MEMBERNOTFOUND && SuperClass != nullptr &&
           membername != nullptr) {
            hr = SuperClass->FuncCall(flag, membername, hint, result, numparams,
                                      param, objthis);
        }
        return hr;
    }

    tjs_error
    tTJSExtendableObject::CreateNew(tjs_uint32 flag, const tjs_char *membername,
                                    tjs_uint32 *hint, iTJSDispatch2 **result,
                                    tjs_int numparams, tTJSVariant **param,
                                    iTJSDispatch2 *objthis) {
        tjs_error hr = inherited::CreateNew(flag, membername, hint, result,
                                            numparams, param, objthis);
        if(hr == TJS_E_MEMBERNOTFOUND && SuperClass != nullptr &&
           membername != nullptr) {
            hr = SuperClass->CreateNew(flag, membername, hint, result,
                                       numparams, param, objthis);
        }
        return hr;
    }

    tjs_error tTJSExtendableObject::PropGet(tjs_uint32 flag,
                                            const tjs_char *membername,
                                            tjs_uint32 *hint,
                                            tTJSVariant *result,
                                            iTJSDispatch2 *objthis) {
        tjs_error hr =
            inherited::PropGet(flag, membername, hint, result, objthis);
        if(hr == TJS_E_MEMBERNOTFOUND && SuperClass != nullptr &&
           membername != nullptr) {
            hr = SuperClass->PropGet(flag, membername, hint, result, objthis);
        }
        return hr;
    }

    tjs_error tTJSExtendableObject::PropSet(tjs_uint32 flag,
                                            const tjs_char *membername,
                                            tjs_uint32 *hint,
                                            const tTJSVariant *param,
                                            iTJSDispatch2 *objthis) {
        tjs_error hr =
            inherited::PropSet(flag, membername, hint, param, objthis);
        // 白名单重试：见 TJSIsStartupCompatWritableNameEx 的说明。
        if((hr == TJS_E_ACCESSDENYED || hr == TJS_E_MEMBERNOTFOUND) &&
           membername != nullptr &&
           TJSIsStartupCompatWritableNameEx(membername)) {
            hr = inherited::PropSet(
                flag | TJS_MEMBERENSURE | TJS_IGNOREPROP, membername, hint,
                param, objthis);
        }
        if(hr == TJS_E_MEMBERNOTFOUND && SuperClass != nullptr &&
           membername != nullptr) {
            hr = SuperClass->PropSet(flag, membername, hint, param, objthis);
        }
        return hr;
    }

    tjs_error tTJSExtendableObject::IsInstanceOf(tjs_uint32 flag,
                                                 const tjs_char *membername,
                                                 tjs_uint32 *hint,
                                                 const tjs_char *classname,
                                                 iTJSDispatch2 *objthis) {
        tjs_error hr =
            inherited::IsInstanceOf(flag, membername, hint, classname, objthis);
        if(hr == TJS_E_MEMBERNOTFOUND && SuperClass != nullptr &&
           membername != nullptr) {
            hr = SuperClass->IsInstanceOf(flag, membername, hint, classname,
                                          objthis);
        }
        return hr;
    }

    tjs_error tTJSExtendableObject::GetCount(tjs_int *result,
                                             const tjs_char *membername,
                                             tjs_uint32 *hint,
                                             iTJSDispatch2 *objthis) {
        tjs_error hr = inherited::GetCount(result, membername, hint, objthis);
        if(hr == TJS_E_MEMBERNOTFOUND && SuperClass != nullptr &&
           membername != nullptr) {
            hr = SuperClass->GetCount(result, membername, hint, objthis);
        }
        return hr;
    }

    tjs_error tTJSExtendableObject::DeleteMember(tjs_uint32 flag,
                                                 const tjs_char *membername,
                                                 tjs_uint32 *hint,
                                                 iTJSDispatch2 *objthis) {
        tjs_error hr = inherited::DeleteMember(flag, membername, hint, objthis);
        if(hr == TJS_E_MEMBERNOTFOUND && SuperClass != nullptr &&
           membername != nullptr) {
            hr = SuperClass->DeleteMember(flag, membername, hint, objthis);
        }
        return hr;
    }

    tjs_error tTJSExtendableObject::Invalidate(tjs_uint32 flag,
                                               const tjs_char *membername,
                                               tjs_uint32 *hint,
                                               iTJSDispatch2 *objthis) {
        tjs_error hr = inherited::Invalidate(flag, membername, hint, objthis);
        if(hr == TJS_E_MEMBERNOTFOUND && SuperClass != nullptr &&
           membername != nullptr) {
            hr = SuperClass->Invalidate(flag, membername, hint, objthis);
        }
        if(membername == nullptr) {
            SuperClass->Invalidate(flag, membername, hint, objthis);
        }
        return hr;
    }

    tjs_error tTJSExtendableObject::IsValid(tjs_uint32 flag,
                                            const tjs_char *membername,
                                            tjs_uint32 *hint,
                                            iTJSDispatch2 *objthis) {
        tjs_error hr = inherited::IsValid(flag, membername, hint, objthis);
        if(hr == TJS_E_MEMBERNOTFOUND && SuperClass != nullptr &&
           membername != nullptr) {
            hr = SuperClass->IsValid(flag, membername, hint, objthis);
        }
        return hr;
    }

    tjs_error tTJSExtendableObject::Operation(
        tjs_uint32 flag, const tjs_char *membername, tjs_uint32 *hint,
        tTJSVariant *result, const tTJSVariant *param, iTJSDispatch2 *objthis) {
        tjs_error hr = inherited::Operation(flag, membername, hint, result,
                                            param, objthis);
        if(hr == TJS_E_MEMBERNOTFOUND && SuperClass != nullptr &&
           membername != nullptr) {
            hr = SuperClass->Operation(flag, membername, hint, result, param,
                                       objthis);
        }
        return hr;
    }

    tjs_error tTJSExtendableObject::NativeInstanceSupport(
        tjs_uint32 flag, tjs_int32 classid, iTJSNativeInstance **pointer) {
        tjs_error hr = inherited::NativeInstanceSupport(flag, classid, pointer);
        if(hr != TJS_S_OK && SuperClass != nullptr &&
           flag == TJS_NIS_GETINSTANCE) {
            hr = SuperClass->NativeInstanceSupport(flag, classid, pointer);
        }
        return hr;
    }

    tjs_error tTJSExtendableObject::ClassInstanceInfo(tjs_uint32 flag,
                                                      tjs_uint num,
                                                      tTJSVariant *value) {
        if(flag == TJS_CII_SET_SUPRECLASS) {
            SetSuper(value->AsObjectNoAddRef());
            return TJS_S_OK;
        } else if(flag == TJS_CII_GET_SUPRECLASS) {
            if(SuperClass) {
                *value = tTJSVariant(SuperClass, SuperClass);
                return TJS_S_OK;
            } else {
                return TJS_E_FAIL;
            }
        } else {
            return inherited::ClassInstanceInfo(flag, num, value);
        }
    }

} // namespace TJS
