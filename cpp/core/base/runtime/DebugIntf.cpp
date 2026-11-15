//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Utilities for Debugging
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <deque>
#include <algorithm>
#include <ctime>
#include <spdlog/spdlog.h>
#include "DebugIntf.h"
#include "MsgIntf.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "SysInitImpl.h"

#ifdef ENABLE_DEBUGGER
#include "tjsDebug.h"
#endif // ENABLE_DEBUGGER

#include "Application.h"
#include "SystemControl.h"

namespace krkr::utils {

//---------------------------------------------------------------------------
// global variables
//---------------------------------------------------------------------------
struct tTVPLogItem {
    ttstr Log;
    ttstr Time;

    tTVPLogItem(const ttstr &log, const ttstr &time) {
        Log = log;
        Time = time;
    }
};

static std::deque<tTVPLogItem> *TVPLogDeque = nullptr;
static tjs_uint TVPLogMaxLines = 2048;

bool autoLogToFileOnError = true;
bool autoClearLogOnError = false;
bool loggingToFile = false;
static tjs_uint TVPLogToFileRollBack = 100;
static ttstr *TVPImportantLogs = nullptr;
static ttstr TVPLogLocation;
ttstr nativeLogLocation;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static bool TVPLogObjectsInitialized = false;

static void TVPEnsureLogObjects() {
    if(TVPLogObjectsInitialized)
        return;
    TVPLogObjectsInitialized = true;

    TVPLogDeque = new std::deque<tTVPLogItem>();
    TVPImportantLogs = new ttstr();
}

//---------------------------------------------------------------------------
static void TVPDestroyLogObjects() {
    if(TVPLogDeque)
        delete TVPLogDeque, TVPLogDeque = nullptr;
    if(TVPImportantLogs)
        delete TVPImportantLogs, TVPImportantLogs = nullptr;
    // runtime-restart 时 TVPSystemUninit 会经 tTVPAtExit 再次调用本函数，
    // 必须复位初始化标志，否则 restart 后 TVPEnsureLogObjects 提前返回、
    // krkr::utils::AddLog 静默丢弃全部 [TVP Console] 输出（含 KAG 场景/错误信息）。
    TVPLogObjectsInitialized = false;
}

//---------------------------------------------------------------------------
tTVPAtExit TVPDestroyLogObjectsAtExit(TVP_ATEXIT_PRI_CLEANUP,
                                      TVPDestroyLogObjects);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void (*TVPOnLog)(const ttstr &line) = nullptr;
// this function is invoked when a line is logged
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// krkr::utils::SetOnLog
//---------------------------------------------------------------------------
void SetOnLog(void (*func)(const ttstr &line)) { TVPOnLog = func; }

//---------------------------------------------------------------------------
static std::vector<tTJSVariantClosure> TVPLoggingHandlerVector;

static void TVPCleanupLoggingHandlerVector() {
    // eliminate empty
    std::vector<tTJSVariantClosure>::iterator i;
    for(i = TVPLoggingHandlerVector.begin(); i != TVPLoggingHandlerVector.end();
        i++) {
        if(!i->Object) {
            i->Release();
            i = TVPLoggingHandlerVector.erase(i);
        } else {
            i++;
        }
    }
}

static void DestroyLoggingHandlerVector() {
    spdlog::info("at-exit PREPARE LoggingHandler begin (count={})",
                 (int)TVPLoggingHandlerVector.size());
    krkr::utils::SetOnLog(nullptr);
    std::vector<tTJSVariantClosure>::iterator i;
    tjs_int k = 0;
    for(i = TVPLoggingHandlerVector.begin(); i != TVPLoggingHandlerVector.end();
        ++i, ++k) {
        spdlog::info("at-exit LoggingHandler release closure[{}] begin", k);
        i->Release();
        spdlog::info("at-exit LoggingHandler release closure[{}] end", k);
    }
    TVPLoggingHandlerVector.clear();
    spdlog::info("at-exit PREPARE LoggingHandler end");
}

void ClearLoggingHandlers() {
    // 供 tTVPApplication::OnExit 在脚本引擎销毁前提前释放日志闭包
    // （krkr::utils::DestroyLoggingHandlerVector 此时再跑应为空/无闭包，作兜底）。
    if(!TVPLoggingHandlerVector.empty()) {
        spdlog::info("TVPClearLoggingHandlers begin (count={})",
                     (int)TVPLoggingHandlerVector.size());
        krkr::utils::DestroyLoggingHandlerVector();
        spdlog::info("TVPClearLoggingHandlers end");
    }
}

static tTVPAtExit
    TVPDestroyLoggingHandlerAtExit(TVP_ATEXIT_PRI_PREPARE,
                                   DestroyLoggingHandlerVector);
//---------------------------------------------------------------------------
static bool TVPInDeliverLoggingEvent = false;

static void _TVPDeliverLoggingEvent(const ttstr &line) // internal
{
    if(!TVPInDeliverLoggingEvent) {
        TVPInDeliverLoggingEvent = true;
        try {
            if(TVPLoggingHandlerVector.size()) {
                bool emptyflag = false;
                tTJSVariant vline(line);
                tTJSVariant *pvline[] = { &vline };
                for(auto &i : TVPLoggingHandlerVector) {
                    if(i.Object) {
                        tjs_error er;
                        try {
                            er = i.FuncCall(0, nullptr, nullptr, nullptr, 1,
                                            pvline, nullptr);
                        } catch(...) {
                            // failed
                            i.Release();
                            i.Object = i.ObjThis = nullptr;
                            throw;
                        }
                        if(TJS_FAILED(er)) {
                            // failed
                            i.Release();
                            i.Object = i.ObjThis = nullptr;
                            emptyflag = true;
                        }
                    } else {
                        emptyflag = true;
                    }
                }

                if(emptyflag) {
                    // the array has empty cell
                    TVPCleanupLoggingHandlerVector();
                }
            }

            if(TVPLoggingHandlerVector.empty()) {
                krkr::utils::SetOnLog(nullptr);
            }
        } catch(...) {
            TVPInDeliverLoggingEvent = false;
            throw;
        }
        TVPInDeliverLoggingEvent = false;
    }
}

//---------------------------------------------------------------------------
static void AddLoggingHandler(tTJSVariantClosure clo) {
    std::vector<tTJSVariantClosure>::iterator i;
    i = std::find(TVPLoggingHandlerVector.begin(),
                  TVPLoggingHandlerVector.end(), clo);
    if(i == TVPLoggingHandlerVector.end()) {
        clo.AddRef();
        TVPLoggingHandlerVector.push_back(clo);
        krkr::utils::SetOnLog(&_TVPDeliverLoggingEvent);
    }
}

//---------------------------------------------------------------------------
static void TVPRemoveLoggingHandler(tTJSVariantClosure clo) {
    std::vector<tTJSVariantClosure>::iterator i;
    i = std::find(TVPLoggingHandlerVector.begin(),
                  TVPLoggingHandlerVector.end(), clo);
    if(i != TVPLoggingHandlerVector.end()) {
        i->Release();
        i->Object = i->ObjThis = nullptr;
    }

    if(!TVPInDeliverLoggingEvent) {
        TVPCleanupLoggingHandlerVector();
        if(TVPLoggingHandlerVector.empty()) {
            krkr::utils::SetOnLog(nullptr);
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// log stream holder
//---------------------------------------------------------------------------
class tTVPLogStreamHolder {
    FILE *Stream;
    bool Alive;
    bool OpenFailed;

public:
    tTVPLogStreamHolder() {
        Stream = nullptr;
        Alive = true;
        OpenFailed = false;
    }

    ~tTVPLogStreamHolder() {
        if(Stream)
            fclose(Stream);
        Alive = false;
    }

private:
    void Open(const tjs_nchar *mode);

public:
    void Clear(); // clear log stream
    void Log(const ttstr &text); // log given text

    void Reopen() {
        if(Stream)
            fclose(Stream);
        Stream = nullptr;
        Alive = false;
        OpenFailed = false;
    } // reopen log stream

} static TVPLogStreamHolder;

//---------------------------------------------------------------------------
void tTVPLogStreamHolder::Open(const tjs_nchar *mode) {
    if(OpenFailed)
        return; // no more try

    try {
        ttstr filename;
        if(TVPLogLocation.GetLen() == 0) {
            Stream = nullptr;
            OpenFailed = true;
        } else {
            // no log location specified
            filename = krkr::utils::nativeLogLocation + TJS_W("/krkr.console.log");
            TVPEnsureDataPathDirectory();
            std::string _filename = filename.AsStdString();
            Stream = fopen(_filename.c_str(), mode);
            if(!Stream)
                OpenFailed = true;
        }

        if(Stream) {
            fseek(Stream, 0, SEEK_END);
            if(ftell(Stream) == 0) {
                // write BOM
                // TODO: 32-bit unicode support
                fwrite(TJS_N("\xff\xfe"), 1, 2,
                       Stream); // indicate unicode text
            }

#ifdef TJS_TEXT_OUT_CRLF
            ttstr separator(TVPSeparatorCRLF);
#else
            ttstr separator(TVPSeparatorCR);
#endif
            Log(separator);

            static tjs_char timebuf[80];

            tm *struct_tm;
            time_t timer;
            timer = time(&timer);

            struct_tm = localtime(&timer);
            TJS_strftime(timebuf, 79, TJS_W("%#c"), struct_tm);

            Log(ttstr(TJS_W("Logging to ")) + ttstr(filename) +
                TJS_W(" started on ") + timebuf);
        }
    } catch(...) {
        OpenFailed = true;
    }
}

//---------------------------------------------------------------------------
void tTVPLogStreamHolder::Clear() {
    // clear log text
    if(Stream)
        fclose(Stream);

    Open(TJS_N("wb"));
}

//---------------------------------------------------------------------------
void tTVPLogStreamHolder::Log(const ttstr &text) {
    if(!Stream)
        Open(TJS_N("ab"));

    try {
        if(Stream) {
            size_t len = text.GetLen() * sizeof(tjs_char);
            if(len != fwrite(text.c_str(), 1, len, Stream)) {
                // cannot write
                fclose(Stream);
                OpenFailed = true;
                return;
            }
#ifdef TJS_TEXT_OUT_CRLF
            fwrite(TJS_W("\r\n"), 1, 2 * sizeof(tjs_char), Stream);
#else
            fwrite(TJS_W("\n"), 1, 1 * sizeof(tjs_char), Stream);
#endif

            // flush
            fflush(Stream);
        }
    } catch(...) {
        try {
            if(Stream)
                fclose(Stream);
        } catch(...) {
        }

        OpenFailed = true;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// krkr::utils::AddLog
//---------------------------------------------------------------------------
void AddLog(const ttstr &line, bool appendtoimportant) {
    // add a line to the log.
    // exceeded lines over TVPLogMaxLines are eliminated.
    // this function is not thread-safe ...

    TVPEnsureLogObjects();
    if(!TVPLogDeque)
        return; // log system is shuttingdown
    if(!TVPImportantLogs)
        return; // log system is shuttingdown

    static time_t prevlogtime = 0;
    static ttstr prevtimebuf;
    static tjs_char timebuf[40];

    tm *struct_tm;
    time_t timer;
    timer = time(&timer);

    if(prevlogtime != timer) {
        struct_tm = localtime(&timer);
        TJS_strftime(timebuf, 39, TJS_W("%H:%M:%S"), struct_tm);
        prevlogtime = timer;
        prevtimebuf = timebuf;
    }

    TVPLogDeque->emplace_back(line, prevtimebuf);

    if(appendtoimportant) {
#ifdef TJS_TEXT_OUT_CRLF
        *TVPImportantLogs +=
            ttstr(timebuf) + TJS_W(" ! ") + line + TJS_W("\r\n");
#else
        *TVPImportantLogs += ttstr(timebuf) + TJS_W(" ! ") + line + TJS_W("\n");
#endif
    }
    while(TVPLogDeque->size() >= TVPLogMaxLines + 100) {
        auto i = TVPLogDeque->begin();
        TVPLogDeque->erase(i, i + 100);
    }

    // FIXME: need fix get timebuf
    // FIXME: remove prefix `2` log message: 2 xxxx
    //    tjs_int timebuflen = (tjs_int)TJS_strlen(timebuf);
    ttstr buf((tTJSStringBufferLength)(/*timebuflen + 1 +*/ line.GetLen()));
    tjs_char *p = buf.Independ();
    //    TJS_strcpy(p, timebuf);
    //    p += timebuflen;
    *p = TJS_W(' ');
    p++;
    TJS_strcpy(p, line.c_str());
    if(TVPOnLog)
        TVPOnLog(buf);

    Application->PrintConsole(buf, appendtoimportant);

    if(krkr::utils::loggingToFile)
        TVPLogStreamHolder.Log(buf);
}

//---------------------------------------------------------------------------
void AddLog(const ttstr &line) { krkr::utils::AddLog(line, false); }

//---------------------------------------------------------------------------
void AddImportantLog(const ttstr &line) { krkr::utils::AddLog(line, true); }

//---------------------------------------------------------------------------
ttstr GetImportantLog() {
    if(!TVPImportantLogs)
        return {};
    return *TVPImportantLogs;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// krkr::utils::GetLastLog : get last n lines of the log ( each line is
// spearated with
// '\n'/'\r\n' )
//---------------------------------------------------------------------------
ttstr GetLastLog(tjs_uint n) {
    TVPEnsureLogObjects();
    if(!TVPLogDeque)
        return TJS_W(""); // log system is shuttingdown

    tjs_uint len = 0;
    auto size = (tjs_uint)TVPLogDeque->size();
    if(n > size)
        n = size;
    if(n == 0)
        return {};
    auto i = TVPLogDeque->end();
    i -= n;
    tjs_uint c;
    for(c = 0; c < n; c++, i++) {
#ifdef TJS_TEXT_OUT_CRLF
        len += i->Time.GetLen() + 1 + i->Log.GetLen() + 2;
#else
        len += i->Time.GetLen() + 1 + i->Log.GetLen() + 1;
#endif
    }

    ttstr buf((tTJSStringBufferLength)len);
    tjs_char *p = buf.Independ();

    i = TVPLogDeque->end();
    i -= n;
    for(c = 0; c < n; c++) {
        TJS_strcpy(p, i->Time.c_str());
        p += i->Time.GetLen();
        *p = TJS_W(' ');
        p++;
        TJS_strcpy(p, i->Log.c_str());
        p += i->Log.GetLen();
#ifdef TJS_TEXT_OUT_CRLF
        *p = TJS_W('\r');
        p++;
        *p = TJS_W('\n');
        p++;
#else
        *p = TJS_W('\n');
        p++;
#endif
        i++;
    }
    return buf;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// krkr::utils::StartLogToFile
//---------------------------------------------------------------------------
void StartLogToFile(bool clear) {
    TVPEnsureLogObjects();
    if(!TVPImportantLogs)
        return; // log system is shuttingdown

    if(krkr::utils::loggingToFile)
        return; // already logging
    if(clear)
        TVPLogStreamHolder.Clear();

    // log last lines

    TVPLogStreamHolder.Log(*TVPImportantLogs);

#ifdef TJS_TEXT_OUT_CRLF
    ttstr separator(
        TJS_W("\r\n") TJS_W("--------------------------------------------------"
                            "----------------------------\r\n"));
#else
    ttstr separator(TJS_W("\n")
                        TJS_W("------------------------------------------"
                              "------------------------------------\n"));
#endif

    TVPLogStreamHolder.Log(separator);

    ttstr content = krkr::utils::GetLastLog(TVPLogToFileRollBack);
    TVPLogStreamHolder.Log(content);

    //
    krkr::utils::loggingToFile = true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// krkr::utils::OnError
//---------------------------------------------------------------------------
void OnError() {
    if(krkr::utils::autoLogToFileOnError)
        krkr::utils::StartLogToFile(krkr::utils::autoClearLogOnError);
    // krkr::utils::OnErrorHook();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// krkr::utils::SetLogLocation
//---------------------------------------------------------------------------
void SetLogLocation(const ttstr &loc) {
    TVPLogLocation = krkr::io::NormalizeStorageName(loc);

    ttstr native = krkr::io::GetLocallyAccessiblePath(TVPLogLocation);
    if(native.IsEmpty()) {
        krkr::utils::nativeLogLocation.Clear();
        TVPLogLocation.Clear();
    } else {
        krkr::utils::nativeLogLocation = native;
        if(krkr::utils::nativeLogLocation[krkr::utils::nativeLogLocation.length() - 1] !=
           TJS_W('/'))
            krkr::utils::nativeLogLocation += TJS_W("/");
    }

    TVPLogStreamHolder.Reopen();

    // check force logging option
    tTJSVariant val;
    if(TVPGetCommandLine(TJS_W("-forcelog"), &val)) {
        ttstr str(val);
        if(str == TJS_W("yes")) {
            krkr::utils::loggingToFile = false;
            krkr::utils::StartLogToFile(false);
        } else if(str == TJS_W("clear")) {
            krkr::utils::loggingToFile = false;
            krkr::utils::StartLogToFile(true);
        }
    }
    if(TVPGetCommandLine(TJS_W("-logerror"), &val)) {
        ttstr str(val);
        if(str == TJS_W("no")) {
            krkr::utils::autoClearLogOnError = false;
            krkr::utils::autoLogToFileOnError = false;
        } else if(str == TJS_W("clear")) {
            krkr::utils::autoClearLogOnError = true;
            krkr::utils::autoLogToFileOnError = true;
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Debug
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Debug::ClassID = -1;

tTJSNC_Debug::tTJSNC_Debug() : tTJSNativeClass(TJS_W("Debug")) {
    TJS_BEGIN_NATIVE_MEMBERS(Debug)
    TJS_DECL_EMPTY_FINALIZE_METHOD
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(
        /*TJS class name*/ Debug) {
        return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Debug)
    //----------------------------------------------------------------------

    //-- methods

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ message) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(numparams == 1) {
            krkr::utils::AddLog(*param[0]);
        } else {
            // display the arguments separated with ", "
            ttstr args;
            for(int i = 0; i < numparams; i++) {
                if(i != 0)
                    args += TJS_W(", ");
                args += ttstr(*param[i]);
            }
            krkr::utils::AddLog(args);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ message)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ notice) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(numparams == 1) {
            krkr::utils::AddImportantLog(*param[0]);
        } else {
            // display the arguments separated with ", "
            ttstr args;
            for(int i = 0; i < numparams; i++) {
                if(i != 0)
                    args += TJS_W(", ");
                args += ttstr(*param[i]);
            }
            krkr::utils::AddImportantLog(args);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ notice)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ startLogToFile) {
        bool clear = false;

        if(numparams >= 1)
            clear = param[0]->operator bool();

        krkr::utils::StartLogToFile(clear);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ startLogToFile)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ logAsError) {
        krkr::utils::OnError();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ logAsError)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ addLoggingHandler) {
        // add function to logging handler list

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();

        AddLoggingHandler(clo);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(
        /*func. name*/ addLoggingHandler)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ removeLoggingHandler) {
        // remove function from logging handler list

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();

        TVPRemoveLoggingHandler(clo);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(
        /*func. name*/ removeLoggingHandler)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getLastLog) {
        tjs_uint lines = TVPLogMaxLines + 100;

        if(numparams >= 1)
            lines = (tjs_uint)param[0]->AsInteger();

        if(result)
            *result = krkr::utils::GetLastLog(lines);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getLastLog)
    //---------------------------------------------------------------------------

    //-- properies

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_PROP_DECL(logLocation){
        TJS_BEGIN_NATIVE_PROP_GETTER{ *result = TVPLogLocation;
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    krkr::utils::SetLogLocation(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(logLocation)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(logToFileOnError){
    TJS_BEGIN_NATIVE_PROP_GETTER{ *result = krkr::utils::autoLogToFileOnError;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    krkr::utils::autoLogToFileOnError = param->operator bool();
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(logToFileOnError)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(clearLogFileOnError){
    TJS_BEGIN_NATIVE_PROP_GETTER{ *result = krkr::utils::autoClearLogOnError;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    krkr::utils::autoClearLogOnError = param->operator bool();
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(clearLogFileOnError)
//----------------------------------------------------------------------

//----------------------------------------------------------------------
TJS_END_NATIVE_MEMBERS

// put version information to DMS
#if 0
    krkr::utils::AddImportantLog(TVPGetVersionInformation());
    krkr::utils::AddImportantLog(ttstr(TVPVersionInformation2));
#endif
} // end of tTJSNC_Debug::tTJSNC_Debug
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TJS2 Console Output Gateway
//---------------------------------------------------------------------------
class tTVPTJS2ConsoleOutputGateway : public iTJSConsoleOutput {
    void ExceptionPrint(const tjs_char *msg) override { krkr::utils::AddLog(msg); }

    void Print(const tjs_char *msg) override { krkr::utils::AddLog(msg); }
} static TVPTJS2ConsoleOutputGateway;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TJS2 Dump Output Gateway
//---------------------------------------------------------------------------
static ttstr TVPDumpOutFileName;
static FILE *TVPDumpOutFile = nullptr; // use traditional output routine
//---------------------------------------------------------------------------
class tTVPTJS2DumpOutputGateway : public iTJSConsoleOutput {
    void ExceptionPrint(const tjs_char *msg) override { Print(msg); }

    void Print(const tjs_char *msg) override {
        if(TVPDumpOutFile) {
            fwrite(msg, 1, TJS_strlen(msg) * sizeof(tjs_char), TVPDumpOutFile);
#ifdef TJS_TEXT_OUT_CRLF
            fwrite(TJS_W("\r\n"), 1, 2 * sizeof(tjs_char), TVPDumpOutFile);
#else
            fwrite(TJS_W("\n"), 1, 1 * sizeof(tjs_char), TVPDumpOutFile);
#endif
        }
    }
} static TVPTJS2DumpOutputGateway;

//---------------------------------------------------------------------------
void StartTJS2Dump() {
#if 0
    ttstr filename = ExePath() + TJS_W(".dump.txt");
    TVPDumpOutFileName = filename;
    TVPDumpOutFile = _wfopen(filename.c_str(), TJS_W("wb+"));
    if(TVPDumpOutFile)
    {
        // TODO: 32-bit unicode support
        fwrite(TJS_N("\xff\xfe"), 1, 2, TVPDumpOutFile); // indicate unicode text
    }
#endif
}

//---------------------------------------------------------------------------
void EndTJS2Dump() {
#if 0
    if (TVPDumpOutFile)
    {
        fclose(TVPDumpOutFile), TVPDumpOutFile = nullptr;
        krkr::utils::AddLog(ttstr(TJS_W("Dumped to ")) + TVPDumpOutFileName);
    }
#endif
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// console interface retrieving functions
//---------------------------------------------------------------------------
iTJSConsoleOutput *GetTJS2ConsoleOutputGateway() {
    return &TVPTJS2ConsoleOutputGateway;
}

//---------------------------------------------------------------------------
iTJSConsoleOutput *GetTJS2DumpOutputGateway() {
    return &TVPTJS2DumpOutputGateway;
}
//---------------------------------------------------------------------------

/*
//---------------------------------------------------------------------------
// on-error hook
//---------------------------------------------------------------------------
void krkr::utils::OnErrorHook()
{
        if(TVPMainForm) TVPMainForm->NotifySystemError();
}
//---------------------------------------------------------------------------
*/

} // namespace krkr::utils
