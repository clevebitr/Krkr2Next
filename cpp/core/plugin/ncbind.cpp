#include "ncbind.hpp"
#include <set>
#include <spdlog/spdlog.h>

// static変数の実体

// auto register 先頭ポインタ
ncbAutoRegister::ThisClassT const*
ncbAutoRegister::_top[ncbAutoRegister::LINE_COUNT] = NCB_INNER_AUTOREGISTER_LINES_INSTANCE;

std::map<ttstr, ncbAutoRegister::INTERNAL_PLUGIN_LISTS > ncbAutoRegister::_internal_plugins;

void ncbAutoRegister::ResetModuleStateForRestart()
{
	TVPRegisteredPlugins.clear();
	_internal_plugins.clear();
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
	ttstr name = _name.AsLowerCase();
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
		for (int line = 0; line < LINE_COUNT; ++line) {
            const auto &plugin_list = it->second.lists[line];
            for (auto i : plugin_list) {
                const ttstr module = i->modulename ? ttstr(i->modulename) : ttstr();
                spdlog::trace(
                    "ncbAutoRegister::LoadModule('{}'): Regist begin line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
                try {
				    i->Regist();
                } catch(...) {
                    spdlog::error(
                        "ncbAutoRegister::LoadModule('{}'): Regist threw at line={} entry='{}'",
                        name.AsStdString(), line, module.AsStdString());
                    throw;
                }
                spdlog::trace(
                    "ncbAutoRegister::LoadModule('{}'): Regist end line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
			}
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
	ttstr name = _name.AsLowerCase();
	if (_internal_plugins.find(name) != _internal_plugins.end())
		return true;
    // 与 LoadModule 一致：`.tpm` 按同名 `.dll` 回退（见上面的说明）。
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
        spdlog::trace("ncbAutoRegister::LoadAllModules: register '{}'",
                      name.AsStdString());
		for (int line = 0; line < LINE_COUNT; ++line) {
            const auto &plugin_list = kv.second.lists[line];
			for (auto i : plugin_list) {
                const ttstr module = i->modulename ? ttstr(i->modulename) : ttstr();
                spdlog::trace(
                    "ncbAutoRegister::LoadAllModules('{}'): Regist begin line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
                try {
				    i->Regist();
                } catch(...) {
                    spdlog::error(
                        "ncbAutoRegister::LoadAllModules('{}'): Regist threw at line={} entry='{}'",
                        name.AsStdString(), line, module.AsStdString());
                    throw;
                }
                spdlog::trace(
                    "ncbAutoRegister::LoadAllModules('{}'): Regist end line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
			}
		}
		TVPRegisteredPlugins.insert(name);
        spdlog::trace("ncbAutoRegister::LoadAllModules: module '{}' done",
                      name.AsStdString());
	}
    spdlog::trace("ncbAutoRegister::LoadAllModules: end");
}
