//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Wave Loop Manager
//---------------------------------------------------------------------------

#ifndef WaveLoopManagerH
#define WaveLoopManagerH
//---------------------------------------------------------------------------

#include "tjsTypes.h"
#include <vector>
#include <string>
#include "WaveSegmentQueue.h"
#include "tjsUtils.h"

#ifdef TVP_IN_LOOP_TUNER
#include "WaveReader.h"
#endif

#define KRKR_WL_MAX_FLAGS 16

#define KRKR_WL_MAX_FLAG_VALUE 9999

#define KRKR_WL_SMOOTH_TIME 50
#define KRKR_WL_SMOOTH_TIME_HALF (KRKR_WL_SMOOTH_TIME / 2)

#define KRKR_WL_MAX_ID_LEN 16

namespace krkr::sound {

class WaveDecoder;
struct WaveFormat;

//---------------------------------------------------------------------------
#ifdef TVP_IN_LOOP_TUNER
typedef AnsiString LabelStringType;
typedef char LabelCharType;
#else
typedef ttstr LabelStringType;
typedef tjs_char LabelCharType;
#endif
//---------------------------------------------------------------------------

#ifdef TVP_IN_LOOP_TUNER
//---------------------------------------------------------------------------
// tTJSCriticalSection ( taken from tjsUtils.h )
//---------------------------------------------------------------------------
class tTJSCriticalSection {
    CRITICAL_SECTION CS;

public:
    tTJSCriticalSection() { InitializeCriticalSection(&CS); }
    ~tTJSCriticalSection() { DeleteCriticalSection(&CS); }

    void Enter() { EnterCriticalSection(&CS); }
    void Leave() { LeaveCriticalSection(&CS); }
};
//---------------------------------------------------------------------------
// tTJSCriticalSectionHolder ( taken from tjsUtils.h )
//---------------------------------------------------------------------------
class tTJSCriticalSectionHolder {
    tTJSCriticalSection *Section;

public:
    tTJSCriticalSectionHolder(tTJSCriticalSection &cs) {
        Section = &cs;
        Section->Enter();
    }

    ~tTJSCriticalSectionHolder() { Section->Leave(); }
};
#else

// tjsUtils.h 已在文件顶部 include（不能放进 namespace）

#endif
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// WaveLoopLink : link structure
//---------------------------------------------------------------------------
enum WaveLoopLinkCondition {
    llcNone,
    llcEqual,
    llcNotEqual,
    llcGreater,
    llcGreaterOrEqual,
    llcLesser,
    llcLesserOrEqual
};

//---------------------------------------------------------------------------
struct WaveLoopLink {
    tjs_int64 From; // 'From' in sample position
    tjs_int64 To; // 'To' in sample position
    bool Smooth; // Smooth transition (uses short 50ms crossfade)
    WaveLoopLinkCondition Condition; // Condition
    tjs_int RefValue;
    tjs_int CondVar; // Condition variable index
#ifdef TVP_IN_LOOP_TUNER
    // these are only used by the loop tuner
    tjs_int FromTier; // display tier of vertical 'from' line
    tjs_int LinkTier; // display tier of horizontal link
    tjs_int ToTier; // display tier of vertical 'to' allow line
    tjs_int Index; // link index

    struct tSortByDistanceFuncObj {
        bool operator()(const WaveLoopLink &lhs,
                        const WaveLoopLink &rhs) const {
            tjs_int64 lhs_dist = lhs.From - lhs.To;
            if(lhs_dist < 0)
                lhs_dist = -lhs_dist;
            tjs_int64 rhs_dist = rhs.From - rhs.To;
            if(rhs_dist < 0)
                rhs_dist = -rhs_dist;
            return lhs_dist < rhs_dist;
        }
    };

    struct tSortByIndexFuncObj {
        bool operator()(const WaveLoopLink &lhs,
                        const WaveLoopLink &rhs) const {
            return lhs.Index < rhs.Index;
        }
    };
#endif

    WaveLoopLink() {
        From = To = 0;
        Smooth = false;
        Condition = llcNone;
        RefValue = CondVar = 0;

#ifdef TVP_IN_LOOP_TUNER
        FromTier = LinkTier = ToTier = 0;
        Index = 0;
#endif
    }
};

//---------------------------------------------------------------------------
bool inline operator<(const WaveLoopLink &lhs,
                      const WaveLoopLink &rhs) {
    if(lhs.From < rhs.From)
        return true;
    if(lhs.From == rhs.From) {
        // give priority to conditional link
        if(lhs.Condition != rhs.Condition)
            return lhs.Condition > rhs.Condition;
        // give priority to conditional expression
        return lhs.CondVar > rhs.CondVar;
    }
    return false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// SampleAndLabelSource : source interface for sound sample and
// its label info
//---------------------------------------------------------------------------
class SampleAndLabelSource {
public:
    virtual void Decode(void *dest, tjs_uint samples, tjs_uint &written,
                        WaveSegmentQueue &segments) = 0;

    [[nodiscard]] virtual const WaveFormat &GetFormat() const = 0;

    virtual ~SampleAndLabelSource() = default;

    virtual bool DesiredFormat(const WaveFormat &format) { return false; }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// WaveLoopManager : wave loop manager
//---------------------------------------------------------------------------
class WaveLoopManager : public SampleAndLabelSource {
    tTJSCriticalSection FlagsCS; // CS to protect flags/links/labels
    int Flags[KRKR_WL_MAX_FLAGS];
    bool FlagsModifiedByLabelExpression; // true if the flags are
                                         // modified by
                                         // EvalLabelExpression
    std::vector<WaveLoopLink> Links;
    std::vector<WaveLabel> Labels;
    tTJSCriticalSection DataCS; // CS to protect other members
    WaveFormat *Format;
    WaveDecoder *Decoder;

    tjs_int ShortCrossFadeHalfSamples;
    // KRKR_WL_SMOOTH_TIME_HALF in sample unit

    bool Looping;

    tjs_int64 Position; // decoding position

    tjs_uint8 *CrossFadeSamples; // sample buffer for crossfading
    tjs_int CrossFadeLen;
    tjs_int CrossFadePosition;

    bool IsLinksSorted; // false if links are not yet sorted
    bool IsLabelsSorted; // false if labels are not yet sorted

    bool IgnoreLinks; // decode the samples with ignoring links

public:
    WaveLoopManager();

    ~WaveLoopManager() override;

    void SetDecoder(WaveDecoder *decoder);

    int GetFlag(tjs_int index);

    void CopyFlags(tjs_int *dest);

    bool GetFlagsModifiedByLabelExpression();

    void SetFlag(tjs_int index, tjs_int f);

    void ClearFlags();

    void ClearLinksAndLabels();

    [[nodiscard]] const std::vector<WaveLoopLink> &GetLinks() const;

    [[nodiscard]] const std::vector<WaveLabel> &GetLabels() const;

    void SetLinks(const std::vector<WaveLoopLink> &links);

    void SetLabels(const std::vector<WaveLabel> &labels);

    [[nodiscard]] bool GetIgnoreLinks() const;

    void SetIgnoreLinks(bool b);

    [[nodiscard]] tjs_int64 GetPosition() const;

    void SetPosition(tjs_int64 pos);

    [[nodiscard]] bool GetLooping() const { return Looping; }

    void SetLooping(bool b) { Looping = b; }

    void Decode(void *dest, tjs_uint samples, tjs_uint &written,
                WaveSegmentQueue &segments)
        override; // from SampleAndLabelSource

    [[nodiscard]] const WaveFormat &GetFormat() const override {
        return *Format;
    }

    // from SampleAndLabelSource
    bool DesiredFormat(const WaveFormat &format) override;

private:
    bool GetNearestEvent(tjs_int64 current, WaveLoopLink &link,
                         bool ignore_conditions);

    void GetLabelAt(tjs_int64 from, tjs_int64 to,
                    std::deque<WaveLabel> &labels);

    void DoCrossFade(void *dest, void *src1, void *src2, tjs_int samples,
                     tjs_int ratiostart, tjs_int ratioend);

    void ClearCrossFadeInformation();

    //--- flag manupulation by label expression
    enum tExpressionToken {
        etUnknown,
        etEOE, // End of the expression
        etLBracket, // '['
        etRBracket, // ']'
        etInteger, // integer number
        etEqual, // '='
        etPlus, // '+'
        etMinus, // '-'
        etPlusEqual, // '+='
        etMinusEqual, // '-='
        etIncrement, // '++'
        etDecrement // '--'
    };

public:
    static bool GetLabelExpression(const LabelStringType &label,
                                   tExpressionToken *ope = nullptr,
                                   tjs_int *lv = nullptr, tjs_int *rv = nullptr,
                                   bool *is_rv_indirect = nullptr);

private:
    bool EvalLabelExpression(const LabelStringType &label);

    static tExpressionToken GetExpressionToken(const LabelCharType *&p,
                                               tjs_int *value);

    static bool GetLabelCharInt(const LabelCharType *s, tjs_int &v);

    //--- loop information input/output stuff
private:
    static bool GetInt(char *s, tjs_int &v);

    static bool GetInt64(char *s, tjs_int64 &v);

    static bool GetBool(char *s, bool &v);

    static bool GetCondition(char *s, WaveLoopLinkCondition &v);

    static bool GetString(char *s, LabelStringType &v);

    static bool GetEntityToken(char *&p, char **name, char **value);

    static bool ReadLinkInformation(char *&p, WaveLoopLink &link);

    static bool ReadLabelInformation(char *&p, WaveLabel &label);

public:
    bool ReadInformation(char *p);

#ifdef TVP_IN_LOOP_TUNER
    // output facility (currently only available with VCL interface)
private:
    static void PutInt(AnsiString &s, tjs_int v);
    static void PutInt64(AnsiString &s, tjs_int64 v);
    static void PutBool(AnsiString &s, bool v);
    static void PutCondition(AnsiString &s, WaveLoopLinkCondition v);
    static void PutString(AnsiString &s, LabelStringType v);
    static void DoSpacing(AnsiString &l, int col);

public:
    void WriteInformation(AnsiString &s);
#endif

public:
};
//---------------------------------------------------------------------------

} // namespace krkr::sound


#endif
