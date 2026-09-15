//---------------------------------------------------------------------------
/*
        TJS2 Script Engine( Byte Code )
        Copyright (c), Takenori Imoto

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "tjs.h"
#include "tjsScriptBlock.h"
#include "tjsByteCodeLoader.h"
#include "tjsGlobalStringMap.h"

namespace TJS {

    bool tTJSByteCodeLoader::IsTJS2ByteCode(const tjs_uint8 *buff) {
        // TJS2
        int tag = read4byte(buff);
        if(tag != FILE_TAG_LE)
            return false;
        // 100'\0'
        int ver = read4byte(&(buff[4]));
        if(ver != VER_TAG_LE)
            return false;
        return true;
    }

    tTJSScriptBlock *tTJSByteCodeLoader::ReadByteCode(tTJS *owner,
                                                      const tjs_char *name,
                                                      const tjs_uint8 *buf,
                                                      size_t size) {
        ReadBuffer = buf;
        ReadIndex = 0;
        ReadSize = (tjs_uint32)size;

        const tjs_uint8 *databuff = ReadBuffer;

        // 字节码文件来自游戏包，长度字段全部不可信。ReadSize 是唯一可信的
        // 边界，下面每一步都先确认目标区间落在 [0, ReadSize) 之内。
        // 头 20 字节 = 4 个标签/版本/文件大小 + DATA 标签与大小
        auto inRange = [this](tjs_uint64 offset, tjs_uint64 length) {
            return offset <= ReadSize &&
                length <= (tjs_uint64)ReadSize - offset;
        };
        if(size < 20)
            return nullptr;

        // TJS2
        int tag = read4byte(databuff);
        if(tag != FILE_TAG_LE)
            return nullptr;
        // 100'\0'
        int ver = read4byte(&(databuff[4]));
        if(ver != VER_TAG_LE)
            return nullptr;

        int filesize = read4byte(&(databuff[8]));
        if(filesize != size)
            return nullptr;

        //// DATA
        tag = read4byte(&(databuff[12]));
        if(tag != DATA_TAG_LE)
            return nullptr;
        const tjs_int32 datasize = read4byte(&(databuff[16]));
        // 负数（或大到装不下）的数据区长度会让下面的 ReadDataArea 从越界处
        // 开读，offset 也会指到缓冲区之外
        if(datasize < 0 || !inRange(20, (tjs_uint64)datasize))
            return nullptr;
        if(!ReadDataArea(databuff, 20, (size_t)datasize))
            return nullptr;

        tjs_uint64 offset = 12 + (tjs_uint64)datasize; // データエリア後の位置
        // OBJS
        if(!inRange(offset, 8))
            return nullptr;
        tag = read4byte(&(databuff[offset]));
        offset += 4;
        if(tag != OBJ_TAG_LE)
            return nullptr;
        // int objsize = ibuff.get();
        const tjs_int32 objsize = read4byte(&(databuff[offset]));
        offset += 4;
        if(objsize < 0 || !inRange(offset, (tjs_uint64)objsize))
            return nullptr;
        auto *block = new tTJSScriptBlock(owner, name, 0);
        ReadObjects(block, databuff, (int)offset, objsize);
        return block;
    }

    bool tTJSByteCodeLoader::ReadDataArea(const tjs_uint8 *buff, int offset,
                                          size_t size) {
        // 七张表，每张都以一个 int32 count 开头。count 与随后的长度都来自文件，
        // 用之前必须对照缓冲区实际长度校验：
        //   - ByteArray 不拷贝，set() 存的就是 &buff[pos] 这段视图，"后续按
        //     index 取值"是否越界完全取决于这里 count 有没有越过缓冲区末尾；
        //   - 其余表按 count 分配并逐项读取，越界值就是数 GB 的分配。
        // size（数据区声明的长度）只作参考，硬边界始终是 ReadSize。
        tjs_uint64 pos = (tjs_uint64)offset;
        (void)size;
        auto canRead = [this](tjs_uint64 p, tjs_uint64 n) {
            return p <= ReadSize && n <= (tjs_uint64)ReadSize - p;
        };
        auto readCount = [&](tjs_int32 &out) {
            if(!canRead(pos, 4))
                return false;
            out = read4byte(&(buff[pos]));
            pos += 4;
            return true;
        };
        auto align4 = [](tjs_uint64 v) { return (v + 3u) & ~(tjs_uint64)3; };

        tjs_int32 count = 0;
        if(!readCount(count))
            return false;
        if(count > 0) {
            if(!canRead(pos, (tjs_uint64)count))
                return false;
            ByteArray.set((tjs_int8 *)&buff[pos], count);
            pos += align4((tjs_uint64)count);
        }

        if(!readCount(count))
            return false;
        if(count > 0) { // load short
            if(!canRead(pos, (tjs_uint64)count * 2))
                return false;
            ShortArray.clear();
            ShortArray.reserve((size_t)count);
            for(tjs_int32 i = 0; i < count; i++) {
                ShortArray.push_back(read2byte(&(buff[pos])));
                pos += 2;
            }
            pos += ((tjs_uint64)count & 1) << 1;
        }

        if(!readCount(count))
            return false;
        if(count > 0) {
            if(!canRead(pos, (tjs_uint64)count * 4))
                return false;
            LongArray.clear();
            LongArray.reserve((size_t)count);
            for(tjs_int32 i = 0; i < count; i++) {
                LongArray.push_back(read4byte(&(buff[pos])));
                pos += 4;
            }
        }

        if(!readCount(count))
            return false;
        if(count > 0) { // load long
            if(!canRead(pos, (tjs_uint64)count * 8))
                return false;
            LongLongArray.clear();
            LongLongArray.reserve((size_t)count);
            for(tjs_int32 i = 0; i < count; i++) {
                LongLongArray.push_back(read8byte(&(buff[pos])));
                pos += 8;
            }
        }

        if(!readCount(count))
            return false;
        if(count > 0) { // load double
            if(!canRead(pos, (tjs_uint64)count * 8))
                return false;
            DoubleArray.clear();
            DoubleArray.reserve((size_t)count);
            for(tjs_int32 i = 0; i < count; i++) {
                tjs_uint64 tmp = read8byte(&(buff[pos]));
                DoubleArray.push_back(*(double *)&tmp);
                pos += 8;
            }
        }

        if(!readCount(count))
            return false;
        if(count > 0) {
            StringArray.clear();
            StringArray.reserve((size_t)count);
            for(tjs_int32 i = 0; i < count; i++) {
                tjs_int32 len = 0;
                if(!readCount(len))
                    return false;
                if(len < 0 || !canRead(pos, (tjs_uint64)len * 2))
                    return false;
                std::vector<tjs_uint16> ch((size_t)len + 1);
                ch[(size_t)len] = 0;
                for(tjs_int32 j = 0; j < len; j++) {
                    ch[(size_t)j] = read2byte(&(buff[pos]));
                    pos += 2;
                }
                StringArray.push_back(
                    TJSMapGlobalStringMap((const tjs_char *)&(ch[0])));
                pos += ((tjs_uint64)len & 1) << 1;
            }
        }

        if(!readCount(count))
            return false;
        if(count > 0) {
            OctetArray.clear();
            OctetArray.reserve((size_t)count);
            for(tjs_int32 i = 0; i < count; i++) {
                tjs_int32 len = 0;
                if(!readCount(len))
                    return false;
                if(len < 0 || !canRead(pos, (tjs_uint64)len))
                    return false;
                auto *octet = new tTJSVariantOctet(&(buff[pos]),
                                                   len); // データはコピーされる
                OctetArray.push_back(octet);
                pos += align4((tjs_uint64)len);
            }
        }
        return true;
    }

    void tTJSByteCodeLoader::ReadObjects(tTJSScriptBlock *block,
                                         const tjs_uint8 *buff, int offset,
                                         int size) {
        // 对象区是字节码里唯一会**把文件里的数字当指针用**的部分：
        // objs[parent[o]]、StringArray[name]、ByteArray[index] 之类的下标全部
        // 来自文件，越界就是野指针读，属性注册那一步（obj->PropSet）甚至是
        // 往任意地址写。所以这里既做读取边界检查，也把每个下标约束回对应的
        // 数组；越界一律按"字节码损坏"抛异常，与既有的 tag 校验一致。
        // 代码数组末尾另留 kCodeSlack 个字：TranslateCodeAddress 对多字指令会
        // 读到 code[i+4]，末条指令被截断时那次读改写不能落到分配之外——补零后
        // 由它自己的 codeSize != i 检查判为损坏。
        // size（对象区声明的长度）只作参考，硬边界始终是 ReadSize。
        static const tjs_int kCodeSlack = 8;

        tjs_uint64 pos = (tjs_uint64)offset;
        (void)size;
        auto canRead = [this](tjs_uint64 p, tjs_uint64 n) {
            return p <= ReadSize && n <= (tjs_uint64)ReadSize - p;
        };
        // 抛异常，同时给编译器一个"这里一定有值"的路径，避免误报未初始化
        auto fail = [block]() -> tjs_int32 {
            TJS_eTJSScriptError(TJSByteCodeBroken, block, 0);
            return 0;
        };
        auto read = [&](tjs_int32 &out) {
            if(!canRead(pos, 4)) {
                out = fail();
            } else {
                out = read4byte(&(buff[pos]));
                pos += 4;
            }
        };
        // 下标（必须落在 [0, max)）
        auto readIndex = [&](tjs_int32 &out, tjs_uint64 max) {
            read(out);
            if(out < 0 || (tjs_uint64)out >= max)
                out = fail();
        };
        // 可为 -1 的下标（父对象、setter/getter 等）
        auto readOptionalIndex = [&](tjs_int32 &out, tjs_uint64 max) {
            read(out);
            if(out < -1 || (tjs_uint64)out >= max)
                out = fail();
        };
        // 计数：负数在下面的 new/vector 里会变成天文数字
        auto readCount = [&](tjs_int32 &out, tjs_uint64 perItem) {
            read(out);
            if(out < 0 || !canRead(pos, (tjs_uint64)out * perItem))
                out = fail();
        };

        tjs_int32 toplevel = 0;
        tjs_int32 objcount = 0;
        read(toplevel);
        read(objcount);
        // 下面按 objcount 分配若干等长数组，先挡住负数与不可能的计数
        if(objcount < 0 || (tjs_uint64)objcount > ReadSize)
            fail();
        readOptionalIndex(toplevel, (tjs_uint64)objcount);

        // tTJSInterCodeContext** objs = new
        // tTJSInterCodeContext*[objcount];
        std::vector<tTJSInterCodeContext *> objs(objcount);
        std::vector<VariantRepalace> work;
        std::vector<int> parent(objcount);
        std::vector<int> propSetter(objcount);
        std::vector<int> propGetter(objcount);
        std::vector<int> superClassGetter(objcount);
        std::vector<std::vector<int>> properties(objcount);
        for(tjs_int32 o = 0; o < objcount; o++) {
            tjs_int32 tag = 0;
            read(tag);
            if(tag != (tjs_int32)FILE_TAG_LE) {
                // throw new TJSException(Error.ByteCodeBroken);
                TJS_eTJSScriptError(TJSByteCodeBroken, block, 0);
            }
            // int objsize = read4byte( &(buff[offset]) );
            tjs_int32 ignored = 0;
            read(ignored);
            readOptionalIndex(parent[o], (tjs_uint64)objcount);
            tjs_int32 name = 0;
            readIndex(name, StringArray.size());
            tjs_int32 contextType = 0;
            read(contextType);
            if(contextType < (tjs_int32)ctTopLevel ||
               contextType > (tjs_int32)ctSuperClassGetter)
                fail();
            // 这几个是寄存器区的尺寸与基准，会直接进入 ExecuteAsFunction 的
            // Allocate(num_alloc) 与 ra[base + i]：负数即是下溢/越界。
            // 上界取一个远超正常脚本的值，避免损坏的头申请数 GB 内存。
            static const tjs_int32 kMaxRegisterCount = 0x00FFFFFF;
            tjs_int32 maxVariableCount = 0;
            tjs_int32 variableReserveCount = 0;
            tjs_int32 maxFrameCount = 0;
            tjs_int32 funcDeclArgCount = 0;
            tjs_int32 funcDeclUnnamedArgArrayBase = 0;
            tjs_int32 funcDeclCollapseBase = 0;
            for(tjs_int32 *field :
                { &maxVariableCount, &variableReserveCount, &maxFrameCount,
                  &funcDeclArgCount, &funcDeclUnnamedArgArrayBase,
                  &funcDeclCollapseBase }) {
                read(*field);
                if(*field < 0 || *field > kMaxRegisterCount)
                    fail();
            }
            readOptionalIndex(propSetter[o], (tjs_uint64)objcount);
            readOptionalIndex(propGetter[o], (tjs_uint64)objcount);
            readOptionalIndex(superClassGetter[o], (tjs_uint64)objcount);

            tjs_int32 count = 0;
            readCount(count, 8); // 两个 count*4 的数组

            // デバッグ用のソース位置を読み込む
            tTJSInterCodeContext::tSourcePos *srcPos = nullptr;
            tjs_int srcPosArraySize = 0;
            if(count > 0) {
                srcPos = new tTJSInterCodeContext::tSourcePos[count];
                srcPosArraySize = count;
                for(tjs_int32 i = 0; i < count; i++) {
                    srcPos[i].CodePos = read4byte(&(buff[pos]));
                    pos += 4;
                }
                for(tjs_int32 i = 0; i < count; i++) {
                    srcPos[i].SourcePos = read4byte(&(buff[pos]));
                    pos += 4;
                }
            }

            readCount(count, 2); // 代码区：count 个 int16（+ 对齐）
            const tjs_int codeSize = count;
            // count * sizeof(tjs_int32) 用有符号 count 直接算，负数会溢出成
            // 天文数字；readCount 已挡住负数并限定了量级，这里再按 size_t 算，
            // 并多留 kCodeSlack 个字（见函数开头说明）。
            tjs_int32 *code = (tjs_int32 *)TJS_malloc(
                ((size_t)count + kCodeSlack) * sizeof(tjs_int32));
            if(code == nullptr)
                TJS_eTJSScriptError(TJSInsufficientMem, block, 0);
            for(tjs_int32 i = 0; i < count; i++) {
                tjs_int16 c = (tjs_int16)read2byte(&(buff[pos]));
                code[i] = c;
                pos += 2;
            }
            memset(code + count, 0, kCodeSlack * sizeof(tjs_int32));
            TranslateCodeAddress(block, code, codeSize);
            pos += ((tjs_uint64)count & 1) << 1;

            readCount(count, 4); // 值表：count 个 (type, index) 短对
            auto *vdata = new tTJSVariant[count];
            const tjs_int datacount = count;
            for(tjs_int32 i = 0; i < datacount; i++) {
                // 两个下标都是 tjs_uint16：原来读进 short 会变成负数，
                // 接着就是 StringArray[-1] 这样的野读
                const tjs_uint16 type = read2byte(&(buff[pos]));
                const tjs_uint16 index = read2byte(&(buff[pos + 2]));
                pos += 4;
                switch((tjs_int32)type) {
                    case TYPE_VOID:
                        vdata[i].Clear();
                        break;
                    case TYPE_OBJECT:
                        vdata[i] = (iTJSDispatch2 *)nullptr;
                        break;
                    case TYPE_INTER_OBJECT:
                    case TYPE_INTER_GENERATOR:
                        if((tjs_uint64)index >= (tjs_uint64)objcount)
                            fail();
                        work.emplace_back(&(vdata[i]), (int)index);
                        break;
                    case TYPE_STRING:
                        if((size_t)index >= StringArray.size())
                            fail();
                        vdata[i] = StringArray[index].c_str(); // tTJSString
                        break;
                    case TYPE_OCTET:
                        if((size_t)index >= OctetArray.size())
                            fail();
                        vdata[i] = OctetArray[index]; // tTJSVariantOctet
                        break;
                    case TYPE_REAL:
                        if((size_t)index >= DoubleArray.size())
                            fail();
                        vdata[i] = (tjs_real)DoubleArray[index];
                        break;
                    case TYPE_BYTE:
                        if((size_t)index >= ByteArray.size())
                            fail();
                        vdata[i] = (tjs_int)ByteArray[index];
                        break;
                    case TYPE_SHORT:
                        if((size_t)index >= ShortArray.size())
                            fail();
                        vdata[i] = (tjs_int)ShortArray[index];
                        break;
                    case TYPE_INTEGER:
                        if((size_t)index >= LongArray.size())
                            fail();
                        vdata[i] = (tjs_int)LongArray[index];
                        break;
                    case TYPE_LONG:
                        if((size_t)index >= LongLongArray.size())
                            fail();
                        vdata[i] = (tjs_int64)LongLongArray[index];
                        break;
                    case TYPE_UNKNOWN:
                    default:
                        vdata[i].Clear();
                        break;
                }
            }
            readCount(count, 4); // super class getter 的代码位置
            // int* scgetterps = new int[count];
            std::vector<tjs_int> scgetterps((size_t)count);
            for(tjs_int32 i = 0; i < count; i++) {
                const tjs_int32 v = read4byte(&(buff[pos]));
                pos += 4;
                // 这些值会被当作起始执行位置交给 ExecuteAsFunction，而 start_ip
                // 是 CodeArea 的**字**偏移（CodeArea + start_ip），越界等于从
                // 任意位置开始跑 VM。
                if(v < 0 || v > codeSize)
                    fail();
                scgetterps[i] = v;
            }
            // properties
            readCount(count, 8); // count 个 (名字, 对象) 对
            if(count > 0) {
                std::vector<int> &props = properties[o];
                props.resize((size_t)count << 1);
                for(tjs_int32 i = 0; i < count; i++) {
                    const tjs_int32 pname = read4byte(&(buff[pos]));
                    const tjs_int32 pobj = read4byte(&(buff[pos + 4]));
                    pos += 8;
                    if(pname < 0 || (size_t)pname >= StringArray.size())
                        fail();
                    if(pobj < 0 || pobj >= objcount)
                        fail();
                    props[(size_t)i << 1] = pname;
                    props[((size_t)i << 1) | 1] = pobj;
                }
            }

            tTJSInterCodeContext *obj = new tTJSInterCodeContext(
                block, StringArray[name].c_str(), (tTJSContextType)contextType,
                code, codeSize, vdata, datacount, maxVariableCount,
                variableReserveCount, maxFrameCount, funcDeclArgCount,
                funcDeclUnnamedArgArrayBase, funcDeclCollapseBase, true, srcPos,
                srcPosArraySize, scgetterps);
            objs[o] = obj;
        }
        tTJSVariant val;
        for(int o = 0; o < objcount; o++) {
            tTJSInterCodeContext *parentObj = nullptr;
            tTJSInterCodeContext *propSetterObj = nullptr;
            tTJSInterCodeContext *propGetterObj = nullptr;
            tTJSInterCodeContext *superClassGetterObj = nullptr;

            if(parent[o] >= 0) {
                parentObj = objs[parent[o]];
            }
            if(propSetter[o] >= 0) {
                propSetterObj = objs[propSetter[o]];
            }
            if(propGetter[o] >= 0) {
                propGetterObj = objs[propGetter[o]];
            }
            if(superClassGetter[o] >= 0) {
                superClassGetterObj = objs[superClassGetter[o]];
            }
            objs[o]->SetCodeObject(parentObj, propSetterObj, propGetterObj,
                                   superClassGetterObj);

            if(properties[o].size() > 0) {
                tTJSInterCodeContext *obj = parentObj;
                // 属性是注册到父对象上的；没有父对象却带属性说明索引被改过，
                // 继续下去就是 obj->PropSet 的空指针解引用
                if(obj == nullptr)
                    fail();
                std::vector<int> &prop = properties[o];
                int length = (int)(prop.size() >> 1);
                for(int i = 0; i < length; i++) {
                    int pos = i << 1;
                    int pname = prop[pos];
                    int pobj = prop[pos + 1];
                    // register members to the parent object
                    val = objs[pobj];
                    obj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                                 StringArray[pname].c_str(), nullptr, &val,
                                 obj);
                }
            }
        }
        int count = (int)work.size();
        for(int i = 0; i < count; i++) {
            VariantRepalace &w = work[i];
            (*w.Work) = objs[w.Index];
        }
        work.clear();
        tTJSInterCodeContext *top = nullptr;
        if(toplevel >= 0) {
            top = objs[toplevel];
        }
        block->SetObjects(top, objs, objcount);
        // delete[] objs;
    }

#define TJS_OFFSET_VM_REG_ADDR(x) ((x) = TJS_TO_VM_REG_ADDR(x))
#define TJS_OFFSET_VM_CODE_ADDR(x) ((x) = TJS_TO_VM_CODE_ADDR(x))

    /**
     * バイトコード中のアドレスは配列のインデックスを指しているので、それをアドレスに変換する
     */
    void tTJSByteCodeLoader::TranslateCodeAddress(tTJSScriptBlock *block,
                                                  tjs_int32 *code,
                                                  const tjs_int32 codeSize) {
        tjs_int i = 0;
        for(; i < codeSize;) {
            tjs_int size;
            switch(code[i]) {
                case VM_NOP:
                    size = 1;
                    break;
                case VM_NF:
                    size = 1;
                    break;
                case VM_CONST:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                    size = 3;
                    break;

#define OP2_DISASM(c)                                                          \
    case c:                                                                    \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]);                                   \
        size = 3;                                                              \
        break

                    OP2_DISASM(VM_CP);
                    OP2_DISASM(VM_CEQ);
                    OP2_DISASM(VM_CDEQ);
                    OP2_DISASM(VM_CLT);
                    OP2_DISASM(VM_CGT);
                    OP2_DISASM(VM_CHKINS);
#undef OP2_DISASM

#define OP2_DISASM(c)                                                          \
    case c:                                                                    \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]);                                   \
        size = 3;                                                              \
        break;                                                                 \
    case c + 1:                                                                \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 3]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 4]);                                   \
        size = 5;                                                              \
        break;                                                                 \
    case c + 2:                                                                \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 3]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 4]);                                   \
        size = 5;                                                              \
        break;                                                                 \
    case c + 3:                                                                \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 3]);                                   \
        size = 4;                                                              \
        break

                    OP2_DISASM(VM_LOR);
                    OP2_DISASM(VM_LAND);
                    OP2_DISASM(VM_BOR);
                    OP2_DISASM(VM_BXOR);
                    OP2_DISASM(VM_BAND);
                    OP2_DISASM(VM_SAR);
                    OP2_DISASM(VM_SAL);
                    OP2_DISASM(VM_SR);
                    OP2_DISASM(VM_ADD);
                    OP2_DISASM(VM_SUB);
                    OP2_DISASM(VM_MOD);
                    OP2_DISASM(VM_DIV);
                    OP2_DISASM(VM_IDIV);
                    OP2_DISASM(VM_MUL);
#undef OP2_DISASM

#define OP1_DISASM                                                             \
    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);                                       \
    size = 2;
                case VM_TT:
                    OP1_DISASM
                    break;
                case VM_TF:
                    OP1_DISASM
                    break;
                case VM_SETF:
                    OP1_DISASM
                    break;
                case VM_SETNF:
                    OP1_DISASM
                    break;
                case VM_LNOT:
                    OP1_DISASM
                    break;
                case VM_BNOT:
                    OP1_DISASM
                    break;
                case VM_ASC:
                    OP1_DISASM
                    break;
                case VM_CHR:
                    OP1_DISASM
                    break;
                case VM_NUM:
                    OP1_DISASM
                    break;
                case VM_CHS:
                    OP1_DISASM
                    break;
                case VM_CL:
                    OP1_DISASM
                    break;
                case VM_INV:
                    OP1_DISASM
                    break;
                case VM_CHKINV:
                    OP1_DISASM
                    break;
                case VM_TYPEOF:
                    OP1_DISASM
                    break;
                case VM_EVAL:
                    OP1_DISASM
                    break;
                case VM_EEXP:
                    OP1_DISASM
                    break;
                case VM_INT:
                    OP1_DISASM
                    break;
                case VM_REAL:
                    OP1_DISASM
                    break;
                case VM_STR:
                    OP1_DISASM
                    break;
                case VM_OCTET:
                    OP1_DISASM
                    break;
#undef OP1_DISASM

                case VM_CCL:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    size = 3;
                    break;

#define OP1_DISASM(c)                                                          \
    case c:                                                                    \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]);                                   \
        size = 2;                                                              \
        break;                                                                 \
    case c + 1:                                                                \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 3]);                                   \
        size = 4;                                                              \
        break;                                                                 \
    case c + 2:                                                                \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 3]);                                   \
        size = 4;                                                              \
        break;                                                                 \
    case c + 3:                                                                \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]);                                   \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]);                                   \
        size = 3;                                                              \
        break

                    OP1_DISASM(VM_INC);
                    OP1_DISASM(VM_DEC);
#undef OP1_DISASM

#define OP1A_DISASM                                                            \
    TJS_OFFSET_VM_CODE_ADDR(code[i + 1]);                                      \
    size = 2;
                case VM_JF:
                    OP1A_DISASM
                    break;
                case VM_JNF:
                    OP1A_DISASM
                    break;
                case VM_JMP:
                    OP1A_DISASM
                    break;
#undef OP1A_DISASM

                case VM_CALL:
                case VM_CALLD:
                case VM_CALLI:
                case VM_NEW: {
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);

                    tjs_int st; // start of arguments
                    if(code[i] == VM_CALLD || code[i] == VM_CALLI) {
                        TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                        st = 5;
                    } else {
                        st = 4;
                    }
                    tjs_int num = code[i + st - 1]; // st-1 = argument count
                    tjs_int c = 0;
                    if(num == -1) {
                        size = st;
                    } else if(num == -2) {
                        st++;
                        num = code[i + st - 1];
                        size = st + num * 2;
                        for(tjs_int j = 0; j < num; j++) {
                            switch(code[i + st + j * 2]) {
                                case fatNormal:
                                    TJS_OFFSET_VM_REG_ADDR(
                                        code[i + st + j * 2 + 1]);
                                    break;
                                case fatExpand:
                                    TJS_OFFSET_VM_REG_ADDR(
                                        code[i + st + j * 2 + 1]);
                                    break;
                                case fatUnnamedExpand:
                                    break;
                            }
                        }
                    } else {
                        // normal operation
                        size = st + num;
                        while(num--) {
                            TJS_OFFSET_VM_REG_ADDR(code[i + c + st]);
                            c++;
                        }
                    }
                    break;
                }

                case VM_GPD:
                case VM_GPDS:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                    size = 4;
                    break;

                case VM_SPD:
                case VM_SPDE:
                case VM_SPDEH:
                case VM_SPDS:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                    size = 4;
                    break;

                case VM_GPI:
                case VM_GPIS:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                    size = 4;
                    break;

                case VM_SPI:
                case VM_SPIE:
                case VM_SPIS:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                    size = 4;
                    break;

                case VM_SETP:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                    size = 3;
                    break;

                case VM_GETP:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                    size = 3;
                    break;

                case VM_DELD:
                case VM_TYPEOFD:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                    size = 4;
                    break;

                case VM_DELI:
                case VM_TYPEOFI:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                    size = 4;
                    break;

                case VM_SRV:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    size = 2;
                    break;

                case VM_RET:
                    size = 1;
                    break;

                case VM_ENTRY:
                    TJS_OFFSET_VM_CODE_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                    size = 3;
                    break;

                case VM_EXTRY:
                    size = 1;
                    break;

                case VM_THROW:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    size = 2;
                    break;

                case VM_CHGTHIS:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                    size = 3;
                    break;

                case VM_GLOBAL:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    size = 2;
                    break;

                case VM_ADDCI:
                    TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                    TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                    size = 3;
                    break;

                case VM_REGMEMBER:
                    size = 1;
                    break;
                case VM_DEBUGGER:
                    size = 1;
                    break;
                default:
                    size = 1;
                    break;
            } /* switch */
            i += size;
        }
        if(codeSize != i) {
            TJS_eTJSScriptError(TJSByteCodeBroken, block, 0);
        }
    }

} // namespace TJS
