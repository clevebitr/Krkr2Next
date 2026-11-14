#pragma once

#include "tjs.h"

namespace krkr::extension {

//---------------------------------------------------------------------------
// 扩展类安装登记：AtInstallClass 在静态初始化期登记，
// AddClassHandler 收下这一条登记，安装阶段由 InstallRegisteredClasses 统一安装。
//---------------------------------------------------------------------------
extern void AddClassHandler(const tjs_char *name,
                            iTJSDispatch2 *(*handler)(iTJSDispatch2 *),
                            const tjs_char *dependences);
struct AtInstallClass {
    AtInstallClass(const tjs_char *name,
                   iTJSDispatch2 *(*handler)(iTJSDispatch2 *),
                   const tjs_char *dependences) {
        AddClassHandler(name, handler, dependences);
    }
};

/*

以下のような書式で、cpp
に入れておくと、VM初期化後、クラスを追加する段階で追加してくれます。
静的リンクするとクラス追加されるpluginの様な機能です。
pluginは実行時にクラス等追加しますが、extensionはビルドする時に、入れるクラスを選択する形です。
static AtInstallClass InstallClassFoo
        (TJS_W("ClassFoo"),
TVPCreateNativeClass_ClassFoo,TJS_W("Window,Layer"));
呼び出される関数にはglobalが渡されるので、必要であればそこからメンバなど取得します。
登録は呼び出し側が返り値のiTJSDispatch2をglobalに登録するので、呼び出された関数内で登録する必要
はありません。
登録時依存クラスを3番目に指定可能ですが、現在のところ無視されています。
*/
extern void InstallRegisteredClasses(iTJSDispatch2 *global);

// 复位「待安装扩展类」登记状态，使 engine_destroy
// 后可干净二次启动（runtime-restart）。
extern void ResetInstallStateForRestart();

} // namespace krkr::extension
