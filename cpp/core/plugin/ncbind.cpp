#include "ncbind.hpp"
#include "compat/ModuleGate.h"
#include <set>
#include <spdlog/spdlog.h>

// static変数の実体

// auto register 先頭ポインタ
ncbAutoRegister::ThisClassT const*
ncbAutoRegister::_top[ncbAutoRegister::LINE_COUNT] = NCB_INNER_AUTOREGISTER_LINES_INSTANCE;

std::map<ttstr, ncbAutoRegister::INTERNAL_PLUGIN_LISTS > ncbAutoRegister::_internal_plugins;

namespace {
    // 别名表：别名（小写）→ 规范模块名（小写）。注册顺序无关，只在查找时生效。
    std::map<ttstr, ttstr> &ModuleAliasMap() {
        static std::map<ttstr, ttstr> map;
        return map;
    }
} // namespace

void ncbAutoRegister::RegisterModuleAlias(NameT alias, NameT canonical) {
    if(!alias || !canonical)
        return;
    ttstr a(alias), c(canonical);
    a.ToLowerCase();
    c.ToLowerCase();
    if(a.IsEmpty() || c.IsEmpty() || a == c)
        return;
    ModuleAliasMap()[a] = c;
}

ttstr ncbAutoRegister::ResolveModuleAlias(const ttstr &name) {
    ttstr lower(name);
    lower.ToLowerCase();
    const auto &map = ModuleAliasMap();
    for(int hop = 0; hop < 4; ++hop) { // 限制跳数，防止别名成环时死循环
        auto it = map.find(lower);
        if(it == map.end())
            break;
        lower = it->second;
    }
    return lower;
}

void ncbAutoRegister::ResetModuleStateForRestart()
{
	TVPRegisteredPlugins.clear();
	_internal_plugins.clear();
    // 别名单不需要清（它们是静态注册期建立的，跨 restart 依然有效）。
}

// ---------------------------------------------------------------
// `.tpm` 与 `.dll` 是同一批插件在 KiriKiri Z 下的两种封装名：老游戏写
// `Plugins.link("wuvorbis.dll")`，krkrz 游戏写 `"wuvorbis.tpm"`（游戏目录里
// 也确实是 plugin/wuvorbis.tpm）。内置插件表只按模块名登记，所以按 `.tpm`
// 找不到时回退用同名 `.dll` 再查一次 —— 否则这些游戏会以 "module not found"
// 静默丢掉音频/视频插件（真机：チート緊縛術… 这类 krkrz 游戏就是这种包法）。
// ---------------------------------------------------------------
static bool AliasTpmToDll(const ttstr &name, ttstr &out)
{
    const tjs_char *kDll = TJS_W(".dll");
    const tjs_char *kTpm = TJS_W(".tpm");
    const tjs_char *p = TJS_strstr(name.c_str(), kTpm);
    if (!p)
        return false;
    // 只认结尾（"xxx.tpm"），避免把路径中间恰好含 ".tpm" 的名字改坏。
    if (p[TJS_strlen(kTpm)] != 0)
        return false;
    out = ttstr(name.c_str(), static_cast<size_t>(p - name.c_str()));
    out += kDll;
    return true;
}

bool ncbAutoRegister::LoadModule(const ttstr &_name)
{
	const ttstr requested = _name.AsLowerCase();
	// 先过别名表：游戏写的插件名可能不是我们注册的规范名（见 RegisterModuleAlias）。
	ttstr name = ResolveModuleAlias(requested);
	if(name != requested) {
        spdlog::info("ncbAutoRegister::LoadModule('{}'): 按模块别名解析到 '{}'",
                     requested.AsStdString(), name.AsStdString());
    }
	// 层归属门：登记为其它层专属的模块在当前层不注册（见 compat/ModuleGate.h）。
	if (!krkr::compat::AllowModuleLoad(name)) {
		return false;
	}
	if (TVPRegisteredPlugins.find(name) != TVPRegisteredPlugins.end()) {
        spdlog::trace("ncbAutoRegister::LoadModule('{}'): already registered",
                      name.AsStdString());
		return true;
    }
	auto it = _internal_plugins.find(name);
	if (it == _internal_plugins.end()) {
        ttstr alias;
        if (AliasTpmToDll(name, alias)) {
            auto alt = _internal_plugins.find(alias);
            if (alt != _internal_plugins.end()) {
                spdlog::info("ncbAutoRegister::LoadModule('{}'): 按 .tpm→.dll 回退到 '{}'",
                             name.AsStdString(), alias.AsStdString());
                it = alt;
            }
        }
    }
	if (it != _internal_plugins.end()) {
        spdlog::trace("ncbAutoRegister::LoadModule('{}'): found internal module",
                      name.AsStdString());
        // 一个 registrar 抛异常**不再中断**本模块其余条目，也不再把异常抛给
        // 调用方（旧行为：一次抛异常会让其后所有模块都不再注册 —— 真机表现为
        // "某个插件坏了，之后整批插件全部消失"，排查成本极高，见
        // zcompat_plugin.cpp 的历史注释）。这里改成逐条隔离：记错误、继续跑，
        // 最后用返回值告诉调用方"这个模块没完全注册成功"（Plugins.link 会拿到
        // false），并且不写进已注册表，便于后续重试。
        bool ok = true;
		for (int line = 0; line < LINE_COUNT; ++line) {
            const auto &plugin_list = it->second.lists[line];
            for (auto i : plugin_list) {
                const ttstr module = i->modulename ? ttstr(i->modulename) : ttstr();
                spdlog::trace(
                    "ncbAutoRegister::LoadModule('{}'): Regist begin line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
                try {
				    i->Regist();
                } catch(const TJS::eTJS &e) {
                    ok = false;
                    spdlog::error(
                        "ncbAutoRegister::LoadModule('{}'): Regist threw at line={} entry='{}': {}",
                        name.AsStdString(), line, module.AsStdString(),
                        e.GetMessage().AsStdString());
                } catch(const std::exception &e) {
                    ok = false;
                    spdlog::error(
                        "ncbAutoRegister::LoadModule('{}'): Regist threw at line={} entry='{}': {}",
                        name.AsStdString(), line, module.AsStdString(), e.what());
                } catch(...) {
                    ok = false;
                    spdlog::error(
                        "ncbAutoRegister::LoadModule('{}'): Regist threw at line={} entry='{}'",
                        name.AsStdString(), line, module.AsStdString());
                }
                spdlog::trace(
                    "ncbAutoRegister::LoadModule('{}'): Regist end line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
			}
		}
        if (!ok) {
            spdlog::error(
                "ncbAutoRegister::LoadModule('{}'): 部分条目注册失败，模块按未注册处理（返回 false，不写入已注册表）",
                name.AsStdString());
            return false;
        }
		TVPRegisteredPlugins.insert(name);
        spdlog::trace("ncbAutoRegister::LoadModule('{}'): regist complete",
                      name.AsStdString());
		return true;
	}
    spdlog::warn("ncbAutoRegister::LoadModule('{}'): module not found in internal plugin map",
                 name.AsStdString());
	return false;
}

bool ncbAutoRegister::HasModule(const ttstr &_name)
{
	// 与 LoadModule 保持同一套解析：模块别名 → `.tpm`/`.dll` 回退。
	// 两者必须一致，否则 `Storages.isExistentStorage("xxx.dll")` 会说"不存在"而
	// `Plugins.link("xxx.dll")` 却能成功（旧实现在 `.tpm` 回退上就踩过这个坑）。
	ttstr name = ResolveModuleAlias(_name);
	if (_internal_plugins.find(name) != _internal_plugins.end())
		return true;
    ttstr alias;
    if (AliasTpmToDll(name, alias))
        return _internal_plugins.find(alias) != _internal_plugins.end();
	return false;
}

void ncbAutoRegister::LoadAllModules()
{
    spdlog::trace("ncbAutoRegister::LoadAllModules: begin ({} modules in map)",
                  static_cast<int>(_internal_plugins.size()));
	for (auto &kv : _internal_plugins) {
		const ttstr &name = kv.first;
		if (TVPRegisteredPlugins.find(name) != TVPRegisteredPlugins.end())
			continue;
		// 层归属门：见 compat/ModuleGate.h（未登记归属的模块一律放行）。
		if (!krkr::compat::AllowModuleLoad(name))
			continue;
        spdlog::trace("ncbAutoRegister::LoadAllModules: register '{}'",
                      name.AsStdString());
        bool ok = true;
		for (int line = 0; line < LINE_COUNT; ++line) {
            const auto &plugin_list = kv.second.lists[line];
			for (auto i : plugin_list) {
                const ttstr module = i->modulename ? ttstr(i->modulename) : ttstr();
                spdlog::trace(
                    "ncbAutoRegister::LoadAllModules('{}'): Regist begin line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
                try {
				    i->Regist();
                } catch(const TJS::eTJS &e) {
                    ok = false;
                    spdlog::error(
                        "ncbAutoRegister::LoadAllModules('{}'): Regist threw at line={} entry='{}': {}",
                        name.AsStdString(), line, module.AsStdString(),
                        e.GetMessage().AsStdString());
                } catch(const std::exception &e) {
                    ok = false;
                    spdlog::error(
                        "ncbAutoRegister::LoadAllModules('{}'): Regist threw at line={} entry='{}': {}",
                        name.AsStdString(), line, module.AsStdString(), e.what());
                } catch(...) {
                    ok = false;
                    spdlog::error(
                        "ncbAutoRegister::LoadAllModules('{}'): Regist threw at line={} entry='{}'",
                        name.AsStdString(), line, module.AsStdString());
                }
                spdlog::trace(
                    "ncbAutoRegister::LoadAllModules('{}'): Regist end line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
			}
		}
        // 与 LoadModule 同样的策略：本条模块不写已注册表（下次还能重试），
        // 但**继续**注册后面的模块 —— 旧行为是直接 throw，一次失败让其后整批
        // 模块全部消失（见 LoadModule 注释）。
        if (!ok) {
            spdlog::error(
                "ncbAutoRegister::LoadAllModules('{}'): 部分条目注册失败，跳过写入已注册表并继续后面的模块",
                name.AsStdString());
            continue;
        }
		TVPRegisteredPlugins.insert(name);
        spdlog::trace("ncbAutoRegister::LoadAllModules: module '{}' done",
                      name.AsStdString());
	}
    spdlog::trace("ncbAutoRegister::LoadAllModules: end");
}
