#include "core/io/Storage.h"

#include <cstddef>
#include <filesystem>
#include <system_error>
#include <memory>
#include <vector>

#include <algorithm> // std::sort
#include <cctype> // std::isdigit / std::tolower

#include "core/io/DirectoryBackend.h"
#include "core/io/IoName.h"
#include "core/io/PathUtil.h"
#include "core/io/archive/Xp3Index.h"
#include "core/io/archive/Xp3Stream.h"
#include "core/log/Logger.h"

namespace krkr::io {

    namespace {
        struct Mount {
            enum class Kind { Directory, Xp3 };
            Kind kind = Kind::Directory;
            std::filesystem::path path;
            std::string pathUtf8;
            std::unique_ptr<Xp3Index> index;
        };

        bool LoadXp3Mount(const std::filesystem::path &path,
                          const std::string &pathUtf8, Mount &out,
                          std::string &error) {
            auto index = std::make_unique<Xp3Index>();
            if(!index->Load(pathUtf8)) {
                error = index->GetLastErrorText();
                return false;
            }
            out.kind = Mount::Kind::Xp3;
            out.path = path;
            out.pathUtf8 = pathUtf8;
            out.index = std::move(index);
            return true;
        }

        bool NaturalLess(const std::string &a, const std::string &b) {
            std::size_t i = 0;
            std::size_t j = 0;
            while(i < a.size() && j < b.size()) {
                const unsigned char ca = static_cast<unsigned char>(a[i]);
                const unsigned char cb = static_cast<unsigned char>(b[j]);
                if(std::isdigit(ca) && std::isdigit(cb)) {
                    std::size_t iEnd = i;
                    std::size_t jEnd = j;
                    while(iEnd < a.size() &&
                          std::isdigit(static_cast<unsigned char>(a[iEnd])))
                        ++iEnd;
                    while(jEnd < b.size() &&
                          std::isdigit(static_cast<unsigned char>(b[jEnd])))
                        ++jEnd;
                    std::size_t ia = i;
                    std::size_t jb = j;
                    while(ia < iEnd - 1 && a[ia] == '0')
                        ++ia;
                    while(jb < jEnd - 1 && b[jb] == '0')
                        ++jb;
                    const std::size_t la = iEnd - ia;
                    const std::size_t lb = jEnd - jb;
                    if(la != lb)
                        return la < lb;
                    const int cmp = a.compare(ia, la, b, jb, lb);
                    if(cmp != 0)
                        return cmp < 0;
                    i = iEnd;
                    j = jEnd;
                    continue;
                }
                if(ca != cb)
                    return ca < cb;
                ++i;
                ++j;
            }
            return a.size() < b.size();
        }

        void ScanArchives(const std::filesystem::path &rootDir,
                          std::vector<Mount> &mounts) {
            std::error_code ec;
            std::filesystem::directory_iterator it(rootDir, ec);
            if(ec) {
                krkr::log::Warn("mount scan: cannot list ", PathToUtf8(rootDir),
                                ": ", ec.message());
                return;
            }

            std::vector<std::string> patches;
            std::vector<std::string> others;
            for(const std::filesystem::directory_entry &entry : it) {
                std::error_code entryEc;
                if(!entry.is_regular_file(entryEc) || entryEc) {
                    continue;
                }

                const std::filesystem::path path = entry.path();
                std::string ext = PathToUtf8(path.extension());
                for(char &ch : ext) {
                    ch = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(ch)));
                }
                if(ext != ".xp3") {
                    continue;
                }

                const std::string name = PathToUtf8(path.filename());
                std::string lower = name;
                for(char &ch : lower) {
                    ch = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(ch)));
                }
                (lower.find("patch") != std::string::npos ? patches : others)
                    .push_back(name);
            }

            std::sort(patches.begin(), patches.end(), NaturalLess);
            std::sort(others.begin(), others.end(), NaturalLess);

            krkr::log::Warn("mount scan: ", patches.size(), " patch + ",
                            others.size(), " other archive(s)");

            for(const std::vector<std::string> *group : { &patches, &others }) {
                for(const std::string &name : *group) {
                    const std::filesystem::path path =
                        rootDir / PathFromUtf8(name);
                    Mount mount;
                    std::string error;
                    if(!LoadXp3Mount(path, PathToUtf8(path), mount, error)) {
                        krkr::log::Warn("mount scan: skipping bad archive ",
                                        name, ": ", error);
                        continue;
                    }
                    krkr::log::Warn("mount scan: + ", name, " (",
                                    mount.index->GetEntries().size(),
                                    " entries)");
                    mounts.push_back(std::move(mount));
                }
            }
        }
    } // namespace

    class Storage::Impl {
    public:
        std::filesystem::path root;
        std::string rootUtf8;
        std::string lastError;
        bool mounted = false;
        bool rootIsFile = false;
        std::vector<Mount> mounts;
    };

    Storage::Storage(const std::string &rootUtf8) :
        impl_(std::make_unique<Impl>()) {
        impl_->rootUtf8 = rootUtf8;
        impl_->root = PathFromUtf8(rootUtf8);
        if(impl_->root.empty()) {
            impl_->lastError = "root is not valid UTF-8: \"" + rootUtf8 + "\"";
            return;
        }

        std::error_code ec;
        if(!std::filesystem::exists(impl_->root, ec) || ec) {
            impl_->lastError = ec
                ? ("cannot stat root \"" + rootUtf8 + "\": " + ec.message())
                : ("root does not exist: \"" + rootUtf8 + "\"");
            return;
        }

        if(std::filesystem::is_regular_file(impl_->root, ec) && !ec) {
            Mount mount;
            std::string error;
            if(!LoadXp3Mount(impl_->root, rootUtf8, mount, error)) {
                impl_->lastError = error;
                return;
            }
            impl_->mounts.push_back(std::move(mount));
            impl_->mounted = true;
            impl_->lastError.clear();
            return;
        }

        if(!std::filesystem::is_directory(impl_->root, ec) || ec) {
            impl_->lastError =
                "root is neither a directory nor an XP3 archive: \"" +
                rootUtf8 + "\"";
            return;
        }
        Mount dir;
        dir.kind = Mount::Kind::Directory;
        dir.path = impl_->root;
        dir.pathUtf8 = rootUtf8;
        impl_->mounts.push_back(std::move(dir));

        ScanArchives(impl_->root, impl_->mounts);

        // log
        for(std::size_t i = 0; i < impl_->mounts.size(); ++i) {
            const Mount &mount = impl_->mounts[i];
            if(mount.kind == Mount::Kind::Xp3) {
                krkr::log::Warn("mount[", i, "] = xp3 \"", mount.pathUtf8,
                                "\" (", mount.index->GetEntries().size(),
                                " entries)");
            } else {
                krkr::log::Warn("mount[", i, "] = dir \"", mount.pathUtf8,
                                "\"");
            }
        }

        impl_->mounted = true;
        impl_->lastError.clear();
    }

    Storage::~Storage() = default;

    bool Storage::Mounted() const { return impl_->mounted; }

    bool Storage::IsArchive() const { return impl_->rootIsFile; }

    std::unique_ptr<IStream> Storage::Open(const std::string &nameUtf8) {
        if(!impl_->mounted) {
            impl_->lastError = "storage is not mounted";
            return nullptr;
        }

        const std::string name = NormalizeStorageName(nameUtf8);
        if(name.empty()) {
            impl_->lastError = "empty file name";
            return nullptr;
        }

        const std::filesystem::path namePath = PathFromUtf8(name);
        if(namePath.empty()) {
            impl_->lastError =
                "file name is not valid UTF-8: \"" + nameUtf8 + "\"";
            return nullptr;
        }

        for(std::size_t i = 0; i < impl_->mounts.size(); ++i) {
            const Mount &mount = impl_->mounts[i];
            std::string error;

            if(mount.kind == Mount::Kind::Xp3) {
                const Xp3Entry *entry = mount.index->Find(name);
                if(entry == nullptr) {
                    continue; // 这个包里没有 -> 看下一个
                }
                auto stream = Xp3Stream::Open(mount.path, *entry, error);
                if(stream == nullptr) {
                    impl_->lastError = error;
                    return nullptr;
                }
                impl_->lastError.clear();
                krkr::log::Debug("open \"", nameUtf8, "\" -> mount[", i, "]",
                                 mount.kind == Mount::Kind::Xp3 ? " xp3"
                                                                : " dir");
                return stream;
            }

            const std::filesystem::path fullPath = mount.path / namePath;
            std::error_code ec;
            if(!std::filesystem::exists(fullPath, ec) || ec) {
                continue; // 目录里没有 -> 看下一个
            }
            auto stream = OpenDirectoryFile(fullPath, error);
            if(stream == nullptr) {
                impl_->lastError = error;
                return nullptr;
            }
            impl_->lastError.clear();
            krkr::log::Debug("open \"", nameUtf8, "\" -> mount[", i, "]",
                             mount.kind == Mount::Kind::Xp3 ? " xp3" : " dir");
            return stream;
        }

        impl_->lastError = "\"" + nameUtf8 + "\" not found in any of " +
            std::to_string(impl_->mounts.size()) + " mount(s)";
        krkr::log::Warn("open \"", nameUtf8, "\" -> not found in ",
                        impl_->mounts.size(), " mount(s)");
        return nullptr;
    }

    const std::string &Storage::GetRootUtf8() const { return impl_->rootUtf8; }

    const char *Storage::GetLastErrorText() const {
        return impl_->lastError.c_str();
    }

} // namespace krkr::io
