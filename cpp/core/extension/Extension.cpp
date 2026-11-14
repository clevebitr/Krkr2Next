
#include "tjsCommHead.h"
#include "Extension.h"

namespace krkr::extension {

//---------------------------------------------------------------------------
// AddClassHandler related
//---------------------------------------------------------------------------
struct AtClassInstallInfo {
    AtClassInstallInfo(const tjs_char *name,
                       iTJSDispatch2 *(*handler)(iTJSDispatch2 *),
                       const tjs_char *dependences) {
        Name = name, Handler = handler;
        if(dependences) {
            ttstr dep(dependences);
            const tjs_char *start = dependences;
            const tjs_char *cur = dependences;
            while(*cur) {
                if((*cur) == TJS_W(',')) {
                    if(start != cur) {
                        Dependences.emplace_back(start, cur - start);
                    }
                    start = cur + 1;
                }
                cur++;
            }
            if(start != cur) {
                Dependences.emplace_back(start, cur - start);
            }
        }
    }
    const tjs_char *Name;
    iTJSDispatch2 *(*Handler)(iTJSDispatch2 *);
    std::vector<ttstr> Dependences; // 依存クラスリスト
};
static std::vector<AtClassInstallInfo> *atClassInstallInfos = nullptr;
static bool atClassesInstalled = false;
//---------------------------------------------------------------------------
void AddClassHandler(const tjs_char *name,
                     iTJSDispatch2 *(*handler)(iTJSDispatch2 *),
                     const tjs_char *dependences) {
    if(atClassesInstalled)
        return;

    if(!atClassInstallInfos)
        atClassInstallInfos = new std::vector<AtClassInstallInfo>();
    atClassInstallInfos->emplace_back(name, handler, dependences);
}
//---------------------------------------------------------------------------
void InstallRegisteredClasses(iTJSDispatch2 *global) {
    if(atClassesInstalled)
        return;
    atClassesInstalled = true;

    if(atClassInstallInfos) {
        iTJSDispatch2 *dsp;
        tTJSVariant val;
        std::vector<AtClassInstallInfo>::iterator i;
        for(i = atClassInstallInfos->begin();
            i != atClassInstallInfos->end(); i++) {
            dsp = i->Handler(global);
            val = tTJSVariant(dsp /*, dsp*/);
            dsp->Release();
            global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, i->Name, nullptr,
                            &val, global);
        }
        delete atClassInstallInfos;
        atClassInstallInfos = nullptr;
    }
}
//---------------------------------------------------------------------------
void ResetInstallStateForRestart() {
    // 二次初始化前复位「已触发安装 / 待装类列表」状态。
    // atClassInstallInfos 在 InstallRegisteredClasses 里已被 delete
    // 置空， 这里只需复位标志，使下次 AddClassHandler /
    // InstallRegisteredClasses 可再次登记并安装。
    atClassesInstalled = false;
    atClassInstallInfos = nullptr;
}
//---------------------------------------------------------------------------

} // namespace krkr::extension
