//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Definition of Messages and Message Related Utilities
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "tjsError.h"
//---------------------------------------------------------------------------
// import message strings
//---------------------------------------------------------------------------

#define TVP_MSG_DECL(name, msg) tTJSMessageHolder name(TJS_W(#name), msg);
#define TVP_MSG_DECL_CONST(name, msg)                                          \
    tTJSMessageHolder name(TJS_W(#name), msg, false);
#define TVP_MSG_DECL_NULL(name)                                                \
    tTJSMessageHolder name(TJS_W(#name), nullptr, false);

#include "MsgIntf.h"
#include "DebugIntf.h"
#include "ApplicationSpecialPath.h"

namespace krkr::base {

//---------------------------------------------------------------------------
// FormatMessage
//---------------------------------------------------------------------------
/*
        these functions do :
        replace each %%, %1, %2 into %, p1, p2.
        %1 must appear only once in the message string, otherwise
   internal buffer will overflow. ( %2 must also so )
*/
ttstr FormatMessage(const tjs_char *msg, const ttstr &p1) {
    tjs_char *p;
    auto *buf = new tjs_char[TJS_strlen(msg) + p1.GetLen() + 1];
    p = buf;
    for(; *msg; msg++, p++) {
        if(*msg == TJS_W('%')) {
            if(msg[1] == TJS_W('%')) {
                // %%
                *p = TJS_W('%');
                msg++;
                continue;
            } else if(msg[1] == TJS_W('1')) {
                // %1
                TJS_strcpy(p, p1.c_str());
                p += p1.GetLen();
                p--;
                msg++;
                continue;
            }
        }
        *p = *msg;
    }

    *p = 0;

    ttstr ret(buf);
    delete[] buf;
    return ret;
}

ttstr FormatMessage(const tjs_char *msg, const ttstr &p1, const ttstr &p2) {
    tjs_char *p;
    auto *buf = new tjs_char[TJS_strlen(msg) + p1.GetLen() + p2.GetLen() + 1];
    p = buf;
    for(; *msg; msg++, p++) {
        if(*msg == TJS_W('%')) {
            if(msg[1] == TJS_W('%')) {
                // %%
                *p = TJS_W('%');
                msg++;
                continue;
            } else if(msg[1] == TJS_W('1')) {
                // %1
                TJS_strcpy(p, p1.c_str());
                p += p1.GetLen();
                p--;
                msg++;
                continue;
            } else if(msg[1] == TJS_W('2')) {
                // %2
                TJS_strcpy(p, p2.c_str());
                p += p2.GetLen();
                p--;
                msg++;
                continue;
            }
        }
        *p = *msg;
    }

    *p = 0;

    ttstr ret(buf);
    delete[] buf;
    return ret;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// ThrowExceptionMessage
//---------------------------------------------------------------------------
void ThrowExceptionMessage(const tjs_char *msg) { throw eTJSError(msg); }

void ThrowExceptionMessage(const tjs_char *msg, const ttstr &p1,
                              tjs_int num) {
    throw eTJSError(FormatMessage(msg, p1, ttstr(num)));
}

void ThrowExceptionMessage(const tjs_char *msg, const ttstr &p1) {
    throw eTJSError(FormatMessage(msg, p1));
}

void ThrowExceptionMessage(const tjs_char *msg, const ttstr &p1,
                              const ttstr &p2) {
    throw eTJSError(FormatMessage(msg, p1, p2));
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// version retrieving
//---------------------------------------------------------------------------
tjs_int VersionMajor;
tjs_int VersionMinor;
tjs_int VersionRelease;
tjs_int VersionBuild;
//---------------------------------------------------------------------------

#ifndef WIDEN2
#define WIDEN2(x) TJS_W(x)
#define WIDEN(x) WIDEN2(x)
#endif
const tjs_char *CompileDate = WIDEN(__DATE__);
const tjs_char *CompileTime = WIDEN(__TIME__);

//---------------------------------------------------------------------------
// version information related functions
//---------------------------------------------------------------------------
extern ttstr ReadAboutStringFromResource();

ttstr GetAboutString() {
    GetVersion();
    ttstr verstr{ fmt::format("{}.{}.{}.{}", VersionMajor, VersionMinor,
                              VersionRelease, VersionBuild) };

    ttstr tjsverstr{ fmt::format("{}.{}.{}", TJSVersionMajor, TJSVersionMinor,
                                 TJSVersionRelease) };

    return FormatMessage(ReadAboutStringFromResource().c_str(), verstr,
                            tjsverstr) +
        krkr::utils::GetImportantLog();
}

//---------------------------------------------------------------------------
ttstr GetVersionInformation() {
    GetVersion();
    ttstr verstr{ fmt::format("{}.{}.{}.{}", VersionMajor, VersionMinor,
                              VersionRelease, VersionBuild) };

    ttstr tjsverstr{ fmt::format("{}.{}.{}", TJSVersionMajor, TJSVersionMinor,
                                 TJSVersionRelease) };

    ttstr version = FormatMessage(TVPVersionInformation, verstr, tjsverstr);
    ttstr str = ApplicationSpecialPath::ReplaceStringAll(
        version.AsStdString(), TJS_W("%DATE%"), ttstr(CompileDate));
    str = ApplicationSpecialPath::ReplaceStringAll(str, TJS_W("%TIME%"),
                                                   ttstr(CompileTime));
    return str;
}

//---------------------------------------------------------------------------
ttstr GetVersionString() {
    GetVersion();
    ttstr verstr{ fmt::format("{}.{}.{}.{}", VersionMajor, VersionMinor,
                              VersionRelease, VersionBuild) };
    return verstr;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Versoin retriving
//---------------------------------------------------------------------------
void GetSystemVersion(tjs_int &major, tjs_int &minor, tjs_int &release,
                         tjs_int &build) {
    GetVersion();
    major = VersionMajor;
    minor = VersionMinor;
    release = VersionRelease;
    build = VersionBuild;
}

//---------------------------------------------------------------------------
void GetTJSVersion(tjs_int &major, tjs_int &minor, tjs_int &release) {
    major = TJSVersionMajor;
    minor = TJSVersionMinor;
    release = TJSVersionRelease;
}
//---------------------------------------------------------------------------

} // namespace krkr::base
