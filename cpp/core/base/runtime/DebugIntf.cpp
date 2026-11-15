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
struct LogItem {
    ttstr Log;
    ttstr Time;

    LogItem(const ttstr &log, const ttstr &time) {
        Log = log;
        Time = time;
    }
};

static std::deque<LogItem> *LogDeque = nullptr;
static tjs_uint LogMaxLines = 2048;

bool autoLogToFileOnError = true;
bool autoClearLogOnError = false;
bool loggingToFile = false;
static tjs_uint LogToFileRollBack = 100;
static ttstr *ImportantLogs = nullptr;
static ttstr LogLocation;
ttstr nativeLogLocation;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static bool LogObjectsInitialized = false;

static void EnsureLogObjects() {
    if(LogObjectsInitialized)
        return;
    LogObjectsInitialized = true;

    LogDeque = new std::deque<LogItem>();
    ImportantLogs = new ttstr();
}

//---------------------------------------------------------------------------
static void DestroyLogObjects() {
    if(LogDeque)
        delete LogDeque, LogDeque = nullptr;
    if(ImportantLogs)
        delete ImportantLogs, ImportantLogs = nullptr;
    // runtime-restart 时 krkr::base::SystemUninit 会经 AtExit 再次调用本函数，
    // 必须复位初始化标志，否则 restart 后 EnsureLogObjects 提前返回、
    // krkr::utils::AddLog 静默丢弃全部 [TVP Console] 输出（含 KAG 场景/错误信息）。
    LogObjectsInitialized = false;
}

//---------------------------------------------------------------------------
AtExit DestroyLogObjectsAtExit(KRKR_ATEXIT_PRI_CLEANUP,
                                      DestroyLogObjects);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void (*OnLog)(const ttstr &line) = nullptr;
// this function is invoked when a line is logged
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// krkr::utils::SetOnLog
//---------------------------------------------------------------------------
void SetOnLog(void (*func)(const ttstr &line)) { OnLog = func; }

//---------------------------------------------------------------------------
static std::vector<tTJSVariantClosure> LoggingHandlerVector;

static void CleanupLoggingHandlerVector() {
    // eliminate empty
    std::vector<tTJSVariantClosure>::iterator i;
    for(i = LoggingHandlerVector.begin(); i != LoggingHandlerVector.end();
        i++) {
        if(!i->Object) {
            i->Release();
            i = LoggingHandlerVector.erase(i);
        } else {
            i++;
        }
    }
}

static void DestroyLoggingHandlerVector() {
    spdlog::info("at-exit PREPARE LoggingHandler begin (count={})",
                 (int)LoggingHandlerVector.size());
    krkr::utils::SetOnLog(nullptr);
    std::vector<tTJSVariantClosure>::iterator i;
    tjs_int k = 0;
    for(i = LoggingHandlerVector.begin(); i != LoggingHandlerVector.end();
        ++i, ++k) {
        spdlog::info("at-exit LoggingHandler release closure[{}] begin", k);
        i->Release();
        spdlog::info("at-exit LoggingHandler release closure[{}] end", k);
    }
    LoggingHandlerVector.clear();
    spdlog::info("at-exit PREPARE LoggingHandler end");
}

void ClearLoggingHandlers() {
    // 供 Application::OnExit 在脚本引擎销毁前提前释放日志闭包
    // （krkr::utils::DestroyLoggingHandlerVector 此时再跑应为空/无闭包，作兜底）。
    if(!LoggingHandlerVector.empty()) {
        spdlog::info("TVPClearLoggingHandlers begin (count={})",
                     (int)LoggingHandlerVector.size());
        krkr::utils::DestroyLoggingHandlerVector();
        spdlog::info("TVPClearLoggingHandlers end");
    }
}

static AtExit
    DestroyLoggingHandlerAtExit(KRKR_ATEXIT_PRI_PREPARE,
                                   DestroyLoggingHandlerVector);
//---------------------------------------------------------------------------
static bool InDeliverLoggingEvent = false;

static void _TVPDeliverLoggingEvent(const ttstr &line) // internal
{
    if(!InDeliverLoggingEvent) {
        InDeliverLoggingEvent = true;
        try {
            if(LoggingHandlerVector.size()) {
                bool emptyflag = false;
                tTJSVariant vline(line);
                tTJSVariant *pvline[] = { &vline };
                for(auto &i : LoggingHandlerVector) {
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
                    CleanupLoggingHandlerVector();
                }
            }

            if(LoggingHandlerVector.empty()) {
                krkr::utils::SetOnLog(nullptr);
            }
        } catch(...) {
            InDeliverLoggingEvent = false;
            throw;
        }
        InDeliverLoggingEvent = false;
    }
}

//---------------------------------------------------------------------------
static void AddLoggingHandler(tTJSVariantClosure clo) {
    std::vector<tTJSVariantClosure>::iterator i;
    i = std::find(LoggingHandlerVector.begin(),
                  LoggingHandlerVector.end(), clo);
    if(i == LoggingHandlerVector.end()) {
        clo.AddRef();
        LoggingHandlerVector.push_back(clo);
        krkr::utils::SetOnLog(&_TVPDeliverLoggingEvent);
    }
}

//---------------------------------------------------------------------------
static void RemoveLoggingHandler(tTJSVariantClosure clo) {
    std::vector<tTJSVariantClosure>::iterator i;
    i = std::find(LoggingHandlerVector.begin(),
                  LoggingHandlerVector.end(), clo);
    if(i != LoggingHandlerVector.end()) {
        i->Release();
        i->Object = i->ObjThis = nullptr;
    }

    if(!InDeliverLoggingEvent) {
        CleanupLoggingHandlerVector();
        if(LoggingHandlerVector.empty()) {
            krkr::utils::SetOnLog(nullptr);
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// log stream holder
//---------------------------------------------------------------------------
class LogStreamHolder {
    FILE *Stream;
    bool Alive;
    bool OpenFailed;

public:
    LogStreamHolder() {
        Stream = nullptr;
        Alive = true;
        OpenFailed = false;
    }

    ~LogStreamHolder() {
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

} static logStreamHolder;

//---------------------------------------------------------------------------
void LogStreamHolder::Open(const tjs_nchar *mode) {
    if(OpenFailed)
        return; // no more try

    try {
        ttstr filename;
        if(LogLocation.GetLen() == 0) {
            Stream = nullptr;
            OpenFailed = true;
        } else {
            // no log location specified
            filename = krkr::utils::nativeLogLocation + TJS_W("/krkr.console.log");
            EnsureDataPathDirectory();
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
void LogStreamHolder::Clear() {
    // clear log text
    if(Stream)
        fclose(Stream);

    Open(TJS_N("wb"));
}

//---------------------------------------------------------------------------
void LogStreamHolder::Log(const ttstr &text) {
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
    // exceeded lines over LogMaxLines are eliminated.
    // this function is not thread-safe ...

    EnsureLogObjects();
    if(!LogDeque)
        return; // log system is shuttingdown
    if(!ImportantLogs)
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

    LogDeque->emplace_back(line, prevtimebuf);

    if(appendtoimportant) {
#ifdef TJS_TEXT_OUT_CRLF
        *ImportantLogs +=
            ttstr(timebuf) + TJS_W(" ! ") + line + TJS_W("\r\n");
#else
        *ImportantLogs += ttstr(timebuf) + TJS_W(" ! ") + line + TJS_W("\n");
#endif
    }
    while(LogDeque->size() >= LogMaxLines + 100) {
        auto i = LogDeque->begin();
        LogDeque->erase(i, i + 100);
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
    if(OnLog)
        OnLog(buf);

    Application->PrintConsole(buf, appendtoimportant);

    if(krkr::utils::loggingToFile)
        logStreamHolder.Log(buf);
}

//---------------------------------------------------------------------------
void AddLog(const ttstr &line) { krkr::utils::AddLog(line, false); }

//---------------------------------------------------------------------------
void AddImportantLog(const ttstr &line) { krkr::utils::AddLog(line, true); }

//---------------------------------------------------------------------------
ttstr GetImportantLog() {
    if(!ImportantLogs)
        return {};
    return *ImportantLogs;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// krkr::utils::GetLastLog : get last n lines of the log ( each line is
// spearated with
// '\n'/'\r\n' )
//---------------------------------------------------------------------------
ttstr GetLastLog(tjs_uint n) {
    EnsureLogObjects();
    if(!LogDeque)
        return TJS_W(""); // log system is shuttingdown

    tjs_uint len = 0;
    auto size = (tjs_uint)LogDeque->size();
    if(n > size)
        n = size;
    if(n == 0)
        return {};
    auto i = LogDeque->end();
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

    i = LogDeque->end();
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
    EnsureLogObjects();
    if(!ImportantLogs)
        return; // log system is shuttingdown

    if(krkr::utils::loggingToFile)
        return; // already logging
    if(clear)
        logStreamHolder.Clear();

    // log last lines

    logStreamHolder.Log(*ImportantLogs);

#ifdef TJS_TEXT_OUT_CRLF
    ttstr separator(
        TJS_W("\r\n") TJS_W("--------------------------------------------------"
                            "----------------------------\r\n"));
#else
    ttstr separator(TJS_W("\n")
                        TJS_W("------------------------------------------"
                              "------------------------------------\n"));
#endif

    logStreamHolder.Log(separator);

    ttstr content = krkr::utils::GetLastLog(LogToFileRollBack);
    logStreamHolder.Log(content);

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
    LogLocation = krkr::io::NormalizeStorageName(loc);

    ttstr native = krkr::io::GetLocallyAccessiblePath(LogLocation);
    if(native.IsEmpty()) {
        krkr::utils::nativeLogLocation.Clear();
        LogLocation.Clear();
    } else {
        krkr::utils::nativeLogLocation = native;
        if(krkr::utils::nativeLogLocation[krkr::utils::nativeLogLocation.length() - 1] !=
           TJS_W('/'))
            krkr::utils::nativeLogLocation += TJS_W("/");
    }

    logStreamHolder.Reopen();

    // check force logging option
    tTJSVariant val;
    if(krkr::base::GetCommandLine(TJS_W("-forcelog"), &val)) {
        ttstr str(val);
        if(str == TJS_W("yes")) {
            krkr::utils::loggingToFile = false;
            krkr::utils::StartLogToFile(false);
        } else if(str == TJS_W("clear")) {
            krkr::utils::loggingToFile = false;
            krkr::utils::StartLogToFile(true);
        }
    }
    if(krkr::base::GetCommandLine(TJS_W("-logerror"), &val)) {
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

        RemoveLoggingHandler(clo);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(
        /*func. name*/ removeLoggingHandler)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getLastLog) {
        tjs_uint lines = LogMaxLines + 100;

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
        TJS_BEGIN_NATIVE_PROP_GETTER{ *result = LogLocation;
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
    krkr::utils::AddImportantLog(GetVersionInformation());
    krkr::utils::AddImportantLog(ttstr(TVPVersionInformation2));
#endif
} // end of tTJSNC_Debug::tTJSNC_Debug
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TJS2 Console Output Gateway
//---------------------------------------------------------------------------
class TJS2ConsoleOutputGateway : public iTJSConsoleOutput {
    void ExceptionPrint(const tjs_char *msg) override { krkr::utils::AddLog(msg); }

    void Print(const tjs_char *msg) override { krkr::utils::AddLog(msg); }
} static tjs2ConsoleOutputGateway;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TJS2 Dump Output Gateway
//---------------------------------------------------------------------------
static ttstr DumpOutFileName;
static FILE *DumpOutFile = nullptr; // use traditional output routine
//---------------------------------------------------------------------------
class TJS2DumpOutputGateway : public iTJSConsoleOutput {
    void ExceptionPrint(const tjs_char *msg) override { Print(msg); }

    void Print(const tjs_char *msg) override {
        if(DumpOutFile) {
            fwrite(msg, 1, TJS_strlen(msg) * sizeof(tjs_char), DumpOutFile);
#ifdef TJS_TEXT_OUT_CRLF
            fwrite(TJS_W("\r\n"), 1, 2 * sizeof(tjs_char), DumpOutFile);
#else
            fwrite(TJS_W("\n"), 1, 1 * sizeof(tjs_char), DumpOutFile);
#endif
        }
    }
} static tjs2DumpOutputGateway;

//---------------------------------------------------------------------------
void StartTJS2Dump() {
#if 0
    ttstr filename = ExePath() + TJS_W(".dump.txt");
    DumpOutFileName = filename;
    DumpOutFile = _wfopen(filename.c_str(), TJS_W("wb+"));
    if(DumpOutFile)
    {
        // TODO: 32-bit unicode support
        fwrite(TJS_N("\xff\xfe"), 1, 2, DumpOutFile); // indicate unicode text
    }
#endif
}

//---------------------------------------------------------------------------
void EndTJS2Dump() {
#if 0
    if (DumpOutFile)
    {
        fclose(DumpOutFile), DumpOutFile = nullptr;
        krkr::utils::AddLog(ttstr(TJS_W("Dumped to ")) + DumpOutFileName);
    }
#endif
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// console interface retrieving functions
//---------------------------------------------------------------------------
iTJSConsoleOutput *GetTJS2ConsoleOutputGateway() {
    return &tjs2ConsoleOutputGateway;
}

//---------------------------------------------------------------------------
iTJSConsoleOutput *GetTJS2DumpOutputGateway() {
    return &tjs2DumpOutputGateway;
}
//---------------------------------------------------------------------------

/*
//---------------------------------------------------------------------------
// on-error hook
//---------------------------------------------------------------------------
void krkr::utils::OnErrorHook()
{
        if(MainForm) MainForm->NotifySystemError();
}
//---------------------------------------------------------------------------
*/

} // namespace krkr::utils
