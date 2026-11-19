// RIZZ, a C++ adaptation of libwyag.py by Thibault Polge (2018-2023).
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <fnmatch.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/evp.h>
#endif

using namespace std;
namespace fs = std::filesystem;
using Bytes = string; // A string can hold every byte value, including zero.

void require(bool ok, const string& message) {
    if (!ok) throw runtime_error(message);
}

Bytes readFile(const fs::path& path) {
    ifstream input(path, ios::binary);
    require(input.good(), "Cannot open " + path.string());
    ostringstream data;
    data << input.rdbuf();
    require(!input.bad(), "Cannot read " + path.string());
    return data.str();
}

// Write beside the destination, then replace it in one rename.
void writeFile(const fs::path& path, const Bytes& data) {
    fs::create_directories(path.parent_path());
    string pattern = path.string() + ".tmp.XXXXXX";
    int fd = mkstemp(pattern.data());
    require(fd >= 0, "Cannot create temporary file for " + path.string());
    size_t offset = 0;
    while (offset < data.size()) {
        ssize_t count = write(fd, data.data() + offset, data.size() - offset);
        if (count <= 0) break;
        offset += count;
    }
    bool closed = close(fd) == 0;
    if (offset != data.size() || !closed || rename(pattern.c_str(), path.c_str()) != 0) {
        fs::remove(pattern);
        throw runtime_error("Cannot write " + path.string());
    }
}

string trim(string s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == string::npos) return "";
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

Bytes digest(const Bytes& data) {
    unsigned char result[20];
    require(data.size() <= 0xffffffffu, "File exceeds the classroom size limit");
#ifdef __APPLE__
    CC_SHA1(data.data(), static_cast<CC_LONG>(data.size()), result);
#else
    require(EVP_Digest(data.data(), data.size(), result, nullptr, EVP_sha1(), nullptr) == 1,
            "SHA-1 failed");
#endif
    return Bytes(reinterpret_cast<char*>(result), 20);
}

string hexId(const Bytes& raw) {
    ostringstream out;
    for (unsigned char byte : raw) out << hex << setw(2) << setfill('0') << int(byte);
    return out.str();
}

bool hashText(const string& s) {
    return !s.empty() && s.find_first_not_of("0123456789abcdefABCDEF") == string::npos;
}

Bytes rawId(const string& id) {
    require(id.size() == 40 && hashText(id), "Invalid object ID");
    Bytes raw;
    for (size_t i = 0; i < 40; i += 2) raw += char(stoi(id.substr(i, 2), nullptr, 16));
    return raw;
}

bool safeName(const fs::path& p) {
    if (p.empty() || p.is_absolute()) return false;
    for (const auto& part : p) {
        string name = part.string();
        for (char& c : name) c = char(tolower(static_cast<unsigned char>(c)));
        if (name.empty() || name == "." || name == ".." || name == ".git" ||
            name.find('\0') != string::npos) return false;
    }
    return true;
}

// 1. Loose objects. The ID covers the header and payload before compression.
struct Object {
    string type;
    Bytes data;

    Bytes encode() const {
        require(type == "blob" || type == "tree" || type == "commit" || type == "tag",
                "Unknown object type: " + type);
        return type + " " + to_string(data.size()) + '\0' + data;
    }
};

class ObjectStore {
    fs::path directory;

    fs::path location(const string& id) const {
        rawId(id); // Validate before using it as a path.
        return directory / id.substr(0, 2) / id.substr(2);
    }
public:
    explicit ObjectStore(fs::path git) : directory(git / "objects") {}

    string save(const Object& obj) const {
        Bytes raw = obj.encode();
        string id = hexId(digest(raw));
        auto path = location(id);
        if (!fs::exists(path)) {
            uLongf size = compressBound(raw.size());
            Bytes packed(size, '\0');
            require(compress(reinterpret_cast<Bytef*>(packed.data()), &size,
                             reinterpret_cast<const Bytef*>(raw.data()), raw.size()) == Z_OK,
                    "Compression failed");
            packed.resize(size);
            writeFile(path, packed);
        }
        return id;
    }

    Object load(const string& id) const {
        Bytes packed = readFile(location(id));
        z_stream stream{};
        require(packed.size() <= UINT_MAX, "Object too large");
        stream.next_in = reinterpret_cast<Bytef*>(packed.data());
        stream.avail_in = static_cast<uInt>(packed.size());
        require(inflateInit(&stream) == Z_OK, "Cannot start decompression");
        array<unsigned char, 16384> chunk;
        Bytes raw;
        int status;
        do {
            stream.next_out = chunk.data();
            stream.avail_out = chunk.size();
            status = inflate(&stream, Z_NO_FLUSH);
            raw.append(reinterpret_cast<char*>(chunk.data()), chunk.size() - stream.avail_out);
        } while (status == Z_OK);
        inflateEnd(&stream);
        require(status == Z_STREAM_END && hexId(digest(raw)) == id, "Damaged object " + id);
        size_t zero = raw.find('\0');
        size_t space = raw.find(' ');
        require(space < zero && zero < raw.size(), "Invalid object header");
        Object obj{raw.substr(0, space), raw.substr(zero + 1)};
        require(obj.encode() == raw, "Invalid object length or type");
        return obj;
    }
};

// 2. Repository layout and names.
class Repository {
public:
    fs::path root, git;
    ObjectStore objects;

    explicit Repository(fs::path path) : root(fs::canonical(path)), git(root / ".git"), objects(git) {}

    static Repository discover() {
        fs::path p = fs::current_path();
        while (!fs::is_directory(p / ".git")) {
            require(p != p.root_path(), "Run this command inside a RIZZ repository");
            p = p.parent_path();
        }
        return Repository(p);
    }

    static void init(const fs::path& path) {
        fs::create_directories(path);
        Repository repo(path);
        require(!fs::exists(repo.git) || fs::is_empty(repo.git), "Repository already exists");
        for (string dir : {"objects", "refs/heads", "refs/tags", "branches", "info"})
            fs::create_directories(repo.git / dir);
        writeFile(repo.git / "HEAD", "ref: refs/heads/master\n");
        writeFile(repo.git / "config", "[core]\n\trepositoryformatversion = 0\n\tfilemode = false\n\tbare = false\n");
        writeFile(repo.git / "description", "RIZZ classroom repository\n");
        cout << "Initialized RIZZ in " << repo.git << '\n';
    }

    string ref(string name) const {
        set<string> seen;
        while (true) {
            require(safeName(name) && seen.insert(name).second, "Invalid or circular reference");
            if (!fs::is_regular_file(git / name)) return "";
            string value = trim(readFile(git / name));
            if (value.rfind("ref: refs/", 0) != 0) {
                rawId(value);
                return value;
            }
            name = value.substr(5);
        }
    }

    string resolve(const string& name) const {
        require(safeName(name), "Invalid revision name");
        set<string> ids;
        if (name == "HEAD" || name.rfind("refs/", 0) == 0) {
            string id = ref(name);
            require(!id.empty(), "No revision at " + name);
            return id;
        }
        for (string base : {"refs/heads/", "refs/tags/", "refs/remotes/"}) {
            string id = ref(base + name);
            if (!id.empty()) ids.insert(id);
        }
        if (name.size() >= 4 && name.size() <= 40 && hashText(name)) {
            string prefix = name;
            for (char& c : prefix) c = char(tolower(c));
            auto dir = git / "objects" / prefix.substr(0, 2);
            if (fs::is_directory(dir)) {
                for (const auto& item : fs::directory_iterator(dir)) {
                    string id = prefix.substr(0, 2) + item.path().filename().string();
                    if (id.size() == 40 && hashText(id) && id.rfind(prefix, 0) == 0) ids.insert(id);
                }
            }
        }
        require(ids.size() == 1, ids.empty() ? "Unknown revision: " + name : "Ambiguous revision: " + name);
        return *ids.begin();
    }

    string fileName(const fs::path& path) const {
        auto absolute = fs::absolute(path).lexically_normal();
        auto name = absolute.lexically_relative(root);
        require(safeName(name) && fs::weakly_canonical(absolute) == absolute,
                "Expected a file inside the working directory: " + path.string());
        return name.generic_string();
    }
};

// 3. The staging area. Git index version 2 stores numbers most significant byte first.
void putNumber(Bytes& data, uint32_t value, int width) {
    for (int i = width - 1; i >= 0; --i) data += char(value >> (8 * i));
}

uint32_t getNumber(const Bytes& data, size_t& pos, int width) {
    require(pos <= data.size() && size_t(width) <= data.size() - pos, "Truncated index");
    uint32_t value = 0;
    for (int i = 0; i < width; ++i) value = (value << 8) | static_cast<unsigned char>(data[pos++]);
    return value;
}

struct Entry {
    string id;
    // ctime sec/ns, mtime sec/ns, device, inode, mode, uid, gid, size
    array<uint32_t, 10> fields{};
    unsigned flags = 0;
    string mode() const {
        ostringstream out;
        out << oct << fields[6];
        return out.str();
    }
};

class Index {
    fs::path path;
public:
    map<string, Entry> files;
    explicit Index(const Repository& repo) : path(repo.git / "index") {
        if (!fs::exists(path)) return;
        Bytes data = readFile(path);
        require(data.size() >= 12 && data.substr(0, 4) == "DIRC", "Bad index signature");
        // The supplied Python writer omits the checksum. Accept its exact
        // entry-only layout, and always write a checksum on the next save.
        bool checksum = data.size() >= 32 &&
                        digest(data.substr(0, data.size() - 20)) == data.substr(data.size() - 20);
        if (checksum) data.resize(data.size() - 20);
        size_t pos = 4;
        require(getNumber(data, pos, 4) == 2, "RIZZ requires a version 2 index");
        unsigned count = getNumber(data, pos, 4);
        for (unsigned i = 0; i < count; ++i) {
            size_t start = pos;
            Entry entry;
            for (auto& field : entry.fields) field = getNumber(data, pos, 4);
            entry.id = hexId(data.substr(pos, 20));
            pos += 20;
            entry.flags = getNumber(data, pos, 2);
            require(!(entry.flags & 0x7000), "Unsupported extended or conflicted index entry");
            size_t end = data.find('\0', pos);
            require(end != string::npos, "Unterminated index path");
            string name = data.substr(pos, end - pos);
            require(safeName(name) && !files.count(name), "Invalid or duplicate index path");
            files[name] = entry;
            pos = start + ((end + 1 - start + 7) / 8) * 8;
        }
        require(pos <= data.size(), "Missing index padding");
        require(checksum || pos == data.size(), "Bad index checksum");
        // Optional uppercase Git cache extensions can be regenerated by Git.
        while (pos < data.size()) {
            require(data.size() - pos >= 8 && data[pos] >= 'A' && data[pos] <= 'Z',
                    "Unsupported index extension");
            pos += 4;
            unsigned length = getNumber(data, pos, 4);
            require(length <= data.size() - pos, "Truncated index extension");
            pos += length;
        }
    }

    void save() const {
        Bytes data = "DIRC";
        putNumber(data, 2, 4);
        putNumber(data, files.size(), 4);
        for (const auto& [name, entry] : files) {
            size_t start = data.size();
            for (auto field : entry.fields) putNumber(data, field, 4);
            data += rawId(entry.id);
            putNumber(data, (entry.flags & 0x8000) | min<size_t>(name.size(), 4095), 2);
            data += name;
            data += '\0';
            while ((data.size() - start) % 8 != 0) data += '\0';
        }
        data += digest(data);
        writeFile(path, data);
    }

    void stage(const Repository& repo, const string& name) {
        auto path = repo.root / name;
        struct stat info{};
        require(stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode), "Expected regular file: " + name);
        Entry entry;
        entry.id = repo.objects.save({"blob", readFile(path)});
#ifdef __APPLE__
        auto ct = info.st_ctimespec, mt = info.st_mtimespec;
#else
        auto ct = info.st_ctim, mt = info.st_mtim;
#endif
        entry.fields = {uint32_t(ct.tv_sec), uint32_t(ct.tv_nsec), uint32_t(mt.tv_sec),
                        uint32_t(mt.tv_nsec), uint32_t(info.st_dev), uint32_t(info.st_ino),
                        0100644, uint32_t(info.st_uid), uint32_t(info.st_gid), uint32_t(info.st_size)};
        files[name] = entry;
    }
};

// 4. Trees connect names to objects. One Tree represents one directory.
struct Leaf {
    string mode, name, id;
    bool directory() const { return mode == "40000"; }
    string type() const {
        if (directory()) return "tree";
        if (mode == "160000") return "commit";
        return "blob";
    }
};

class Tree {
public:
    vector<Leaf> leaves;

    static Tree parse(const Bytes& data) {
        Tree tree;
        size_t pos = 0;
        set<string> names;
        while (pos < data.size()) {
            size_t space = data.find(' ', pos);
            size_t zero = data.find('\0', space);
            require(space != string::npos && zero != string::npos && data.size() - zero > 20,
                    "Broken tree entry");
            Leaf leaf{data.substr(pos, space - pos), data.substr(space + 1, zero - space - 1),
                      hexId(data.substr(zero + 1, 20))};
            require(safeName(leaf.name) && leaf.name.find('/') == string::npos &&
                    names.insert(leaf.name).second, "Invalid tree filename");
            tree.leaves.push_back(leaf);
            pos = zero + 21;
        }
        return tree;
    }

    Object encode() {
        // Git compares a directory name as though it ends in a slash.
        sort(leaves.begin(), leaves.end(), [](const Leaf& a, const Leaf& b) {
            return a.name + (a.directory() ? "/" : "") < b.name + (b.directory() ? "/" : "");
        });
        Bytes data;
        for (const Leaf& leaf : leaves)
            data += leaf.mode + " " + leaf.name + '\0' + rawId(leaf.id);
        return {"tree", data};
    }
};

class Folder {
    Tree tree;
    map<string, Folder> children;
public:
    void insert(const string& path, const Entry& file) {
        size_t slash = path.find('/');
        if (slash == string::npos) tree.leaves.push_back({file.mode(), path, file.id});
        else children[path.substr(0, slash)].insert(path.substr(slash + 1), file);
    }

    string save(const ObjectStore& store) {
        for (auto& [name, child] : children)
            tree.leaves.push_back({"40000", name, child.save(store)});
        return store.save(tree.encode());
    }
};

// Commits and annotated tags share the same header / blank line / message layout.
vector<string> headers(const Object& obj, const string& key) {
    vector<string> found;
    istringstream lines(obj.data);
    string line;
    while (getline(lines, line) && !line.empty()) {
        if (line.rfind(key + " ", 0) == 0) found.push_back(line.substr(key.size() + 1));
    }
    return found;
}

string header(const Object& obj, const string& key) {
    auto values = headers(obj, key);
    require(values.size() == 1, "Expected one " + key + " header");
    return values.front();
}

string findType(const Repository& repo, string id, const string& type) {
    while (!type.empty()) {
        auto obj = repo.objects.load(id);
        if (obj.type == type) return id;
        if (obj.type == "tag") id = header(obj, "object");
        else if (obj.type == "commit" && type == "tree") id = header(obj, "tree");
        else throw runtime_error("Expected " + type + ", found " + obj.type);
    }
    return id;
}

fs::path configHome() {
    if (const char* p = getenv("XDG_CONFIG_HOME")) return p;
    if (const char* p = getenv("HOME")) return fs::path(p) / ".config";
    return {};
}

string author(const Repository& repo) {
    string name = "RIZZ Student", email = "student@example.com";
    vector<fs::path> configs;
    if (!configHome().empty()) configs.push_back(configHome() / "git/config");
    if (const char* home = getenv("HOME")) configs.push_back(fs::path(home) / ".gitconfig");
    configs.push_back(repo.git / "config");
    for (const auto& p : configs) {
        ifstream in(p);
        string line, section;
        while (getline(in, line)) {
            line = trim(line);
            if (line.empty()) continue;
            if (line.front() == '[') section = line;
            size_t equal = line.find('=');
            if (section != "[user]" || equal == string::npos) continue;
            string key = trim(line.substr(0, equal)), value = trim(line.substr(equal + 1));
            if (key == "name") name = value;
            if (key == "email") email = value;
        }
    }
    return name + " <" + email + "> " + to_string(time(nullptr)) + " +0000";
}

void commit(const Repository& repo, const string& message) {
    require(!trim(message).empty(), "Supply a commit message with -m");
    Index index(repo);
    Folder root;
    for (const auto& [path, entry] : index.files) root.insert(path, entry);
    string data = "tree " + root.save(repo.objects) + "\n";
    string parent = repo.ref("HEAD");
    if (!parent.empty()) data += "parent " + parent + "\n";
    string person = author(repo);
    data += "author " + person + "\ncommitter " + person + "\n\n" + trim(message) + "\n";
    string id = repo.objects.save({"commit", data});
    string head = trim(readFile(repo.git / "HEAD"));
    string target = head.rfind("ref: ", 0) == 0 ? head.substr(5) : "HEAD";
    require(safeName(target), "Invalid HEAD");
    writeFile(repo.git / target, id + "\n");
    cout << id << ' ' << trim(message) << '\n';
}

void logHistory(const Repository& repo, const string& id, set<string>& seen) {
    if (!seen.insert(id).second) return;
    Object obj = repo.objects.load(id);
    require(obj.type == "commit", "Expected commit");
    size_t blank = obj.data.find("\n\n");
    require(blank != string::npos, "Missing commit message");
    string title = obj.data.substr(blank + 2);
    title = title.substr(0, title.find('\n'));
    cout << "  c_" << id << " [label=" << quoted(id.substr(0, 7) + ": " + title) << "]\n";
    for (const string& parent : headers(obj, "parent")) {
        rawId(parent);
        cout << "  c_" << id << " -> c_" << parent << ";\n";
        logHistory(repo, parent, seen);
    }
}

// A traversal supplies each path to a visitor. Each command decides what to do with it.
class TreeVisitor {
public:
    virtual void visit(const Leaf& leaf, const string& path) = 0;
    virtual ~TreeVisitor() = default;
};

void walk(const Repository& repo, const string& id, const string& prefix,
          bool recursive, TreeVisitor& visitor) {
    Object obj = repo.objects.load(id);
    require(obj.type == "tree", "Expected a tree");
    for (const Leaf& leaf : Tree::parse(obj.data).leaves) {
        string path = prefix + leaf.name;
        visitor.visit(leaf, path);
        if (recursive && leaf.directory()) walk(repo, leaf.id, path + "/", true, visitor);
    }
}

class Listing : public TreeVisitor {
    bool recursive;
public:
    explicit Listing(bool recurse) : recursive(recurse) {}
    void visit(const Leaf& leaf, const string& path) override {
        if (recursive && leaf.directory()) return;
        cout << setfill('0') << setw(6) << leaf.mode << " " << leaf.type()
             << " " << leaf.id << "\t" << path << '\n';
    }
};

class Snapshot : public TreeVisitor {
public:
    map<string, string> files;
    void visit(const Leaf& leaf, const string& path) override {
        if (!leaf.directory()) files[path] = leaf.id;
    }
};

class Export : public TreeVisitor {
    const ObjectStore& store;
    fs::path destination;
public:
    Export(const ObjectStore& source, fs::path dest) : store(source), destination(dest) {}
    void visit(const Leaf& leaf, const string& path) override {
        auto target = destination / path;
        if (leaf.directory()) fs::create_directory(target);
        else {
            Object obj = store.load(leaf.id);
            require(obj.type == "blob", "Only file contents can be exported");
            writeFile(target, obj.data);
        }
    }
};

void checkout(const Repository& repo, const string& tree, fs::path path) {
    path = fs::weakly_canonical(fs::absolute(path));
    auto relative = path.lexically_relative(repo.git);
    require(relative.empty() || *relative.begin() == "..", "Choose a folder outside .git");
    require(!fs::exists(path) || (fs::is_directory(path) && fs::is_empty(path)),
            "Checkout needs an empty destination");
    fs::create_directories(path);
    Export output(repo.objects, path);
    walk(repo, tree, "", true, output);
}

// 5. Ignore rules. A later matching line in the same file wins.
class IgnoreRules {
    map<string, Bytes> scoped;
    vector<Bytes> fallback;

    // -1 means no rule matched, 0 means include, 1 means ignore.
    static int match(const Bytes& rules, const string& name) {
        int result = -1;
        istringstream lines(rules);
        string line;
        while (getline(lines, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            bool include = line[0] == '!';
            if (include || line[0] == '\\') line.erase(0, 1);
            if (fnmatch(line.c_str(), name.c_str(), FNM_NOESCAPE) == 0) result = include ? 0 : 1;
        }
        return result;
    }
public:
    IgnoreRules(const Repository& repo, const Index& index) {
        for (const auto& [name, entry] : index.files) {
            fs::path path(name);
            if (path.filename() == ".gitignore")
                scoped[path.parent_path().string()] = repo.objects.load(entry.id).data;
        }
        vector<fs::path> extras{repo.git / "info/exclude"};
        if (!configHome().empty()) extras.push_back(configHome() / "git/ignore");
        for (const auto& path : extras)
            if (fs::is_regular_file(path)) fallback.push_back(readFile(path));
    }

    bool ignores(const string& name) const {
        auto folder = fs::path(name).parent_path();
        while (true) {
            auto it = scoped.find(folder.string());
            if (it != scoped.end()) {
                int result = match(it->second, name);
                if (result >= 0) return result == 1;
            }
            if (folder.empty()) break;
            folder = folder.parent_path();
        }
        for (const Bytes& rules : fallback) {
            int result = match(rules, name);
            if (result >= 0) return result == 1;
        }
        return false;
    }
};

void status(const Repository& repo) {
    Index index(repo);
    IgnoreRules rules(repo, index);
    Snapshot last;
    string head = trim(readFile(repo.git / "HEAD"));
    cout << (head.rfind("ref: refs/heads/", 0) == 0 ? "On branch " + head.substr(16) : "Detached HEAD " + head) << '\n';
    if (!repo.ref("HEAD").empty())
        walk(repo, findType(repo, repo.ref("HEAD"), "tree"), "", true, last);
    cout << "\nChanges to be committed:\n";
    for (const auto& [name, entry] : index.files) {
        if (!last.files.count(name)) cout << "  added: " << name << '\n';
        else if (last.files.at(name) != entry.id) cout << "  modified: " << name << '\n';
    }
    for (const auto& [name, id] : last.files)
        if (!index.files.count(name)) cout << "  deleted: " << name << '\n';

    cout << "\nChanges not staged:\n";
    for (const auto& [name, entry] : index.files) {
        fs::path path = repo.root / name;
        if (!fs::is_regular_file(fs::symlink_status(path))) cout << "  deleted: " << name << '\n';
        else if (hexId(digest(Object{"blob", readFile(path)}.encode())) != entry.id)
            cout << "  modified: " << name << '\n';
    }
    set<string> untracked;
    for (fs::recursive_directory_iterator it(repo.root), end; it != end; ++it) {
        if (it->path().filename() == ".git") { it.disable_recursion_pending(); continue; }
        if (!fs::is_regular_file(it->symlink_status())) continue;
        string name = it->path().lexically_relative(repo.root).string();
        if (!index.files.count(name) && !rules.ignores(name)) untracked.insert(name);
    }
    cout << "\nUntracked files:\n";
    for (const string& name : untracked) cout << "  " << name << '\n';
}

// 6. Command-line interface.
void listRefs(const Repository& repo, bool tagsOnly) {
    auto base = repo.git / (tagsOnly ? "refs/tags" : "refs");
    set<fs::path> paths;
    for (const auto& item : fs::recursive_directory_iterator(base))
        if (item.is_regular_file()) paths.insert(item.path());
    for (const auto& p : paths) {
        string name = p.lexically_relative(repo.git).string();
        if (tagsOnly) cout << p.lexically_relative(base).string() << '\n';
        else cout << repo.ref(name) << " " << name << '\n';
    }
}

void tag(const Repository& repo, const string& name, string id, bool annotated) {
    require(safeName(name) && name.find_first_of(" ~^:?*[\\") == string::npos &&
            name.find("..") == string::npos && name.find("@{") == string::npos, "Invalid tag name");
    auto path = repo.git / "refs/tags" / name;
    require(!fs::exists(path), "Tag already exists");
    if (annotated) {
        string data = "object " + id + "\ntype " + repo.objects.load(id).type + "\ntag " + name +
                      "\ntagger " + author(repo) + "\n\nTag created by RIZZ\n";
        id = repo.objects.save({"tag", data});
    }
    writeFile(path, id + "\n");
}

class Arguments {
    vector<string> args;
public:
    Arguments(int argc, char** argv) : args(argv + 2, argv + argc) {}

    bool flag(const string& key) {
        auto it = find(args.begin(), args.end(), key);
        if (it == args.end()) return false;
        args.erase(it);
        return true;
    }

    string option(const string& key, const string& fallback) {
        auto it = find(args.begin(), args.end(), key);
        if (it == args.end()) return fallback;
        require(it + 1 != args.end(), "Missing value after " + key);
        string value = *(it + 1);
        args.erase(it, it + 2);
        return value;
    }

    const vector<string>& paths(size_t least, size_t most) const {
        require(args.size() >= least && args.size() <= most, "Wrong argument count. See rizz --help");
        return args;
    }
};

// Two RIZZ writers must not edit the same index or reference simultaneously.
class WriteLock {
    fs::path path;
public:
    explicit WriteLock(const Repository& repo) : path(repo.git / "rizz.lock") {
        require(fs::create_directory(path), "Repository is busy (.git/rizz.lock)");
    }
    ~WriteLock() { error_code ignored; fs::remove(path, ignored); }
    WriteLock(const WriteLock&) = delete;
    WriteLock& operator=(const WriteLock&) = delete;
};

void editIndex(const Repository& repo, const vector<string>& args, bool remove) {
    Index index(repo);
    set<string> names;
    for (const string& arg : args) names.insert(repo.fileName(arg));
    for (const string& name : names) {
        if (remove) {
            require(index.files.count(name), "File is not staged: " + name);
            require(!fs::exists(repo.root / name) || fs::is_regular_file(repo.root / name),
                    "Expected a regular file: " + name);
        } else index.stage(repo, name);
    }
    if (remove) {
        for (const string& name : names) {
            fs::remove(repo.root / name);
            index.files.erase(name);
        }
    } else {
        for (const auto& [name, entry] : index.files) {
            auto parent = fs::path(name).parent_path();
            while (!parent.empty()) {
                require(!index.files.count(parent.string()), "Remove conflicting staged file first: " + parent.string());
                parent = parent.parent_path();
            }
        }
    }
    index.save();
}

void help() {
    cout << "RIZZ - a small Git implementation\n"
         << "init [directory]                  add file...\n"
         << "commit -m message                 status\n"
         << "rm file...                        ls-files [--verbose]\n"
         << "hash-object [-w] [-t type] file   cat-file type revision\n"
         << "ls-tree [-r] revision             checkout revision empty-directory\n"
         << "log [revision]                    rev-parse [--wyag-type type] revision\n"
         << "tag [-a] [name [revision]]        show-ref\n"
         << "check-ignore path...\n";
}

void run(const string& command, Arguments args) {
    if (command == "init") {
        auto p = args.paths(0, 1);
        Repository::init(p.empty() ? "." : p[0]);
        return;
    }
    if (command == "hash-object") {
        bool save = args.flag("-w");
        string type = args.option("-t", "blob");
        Object obj{type, readFile(args.paths(1, 1)[0])};
        if (save) cout << Repository::discover().objects.save(obj) << '\n';
        else cout << hexId(digest(obj.encode())) << '\n';
        return;
    }
    Repository repo = Repository::discover();
    if (command == "add" || command == "rm") {
        WriteLock lock(repo);
        editIndex(repo, args.paths(1, SIZE_MAX), command == "rm");
    } else if (command == "commit") {
        string message = args.option("-m", "");
        args.paths(0, 0);
        WriteLock lock(repo);
        commit(repo, message);
    } else if (command == "status") {
        args.paths(0, 0);
        status(repo);
    } else if (command == "cat-file") {
        auto p = args.paths(2, 2);
        cout << repo.objects.load(findType(repo, repo.resolve(p[1]), p[0])).data;
    } else if (command == "rev-parse") {
        string type = args.option("--wyag-type", "");
        cout << findType(repo, repo.resolve(args.paths(1, 1)[0]), type) << '\n';
    } else if (command == "ls-tree") {
        bool recursive = args.flag("-r");
        Listing listing(recursive);
        walk(repo, findType(repo, repo.resolve(args.paths(1, 1)[0]), "tree"), "", recursive, listing);
    } else if (command == "checkout") {
        auto p = args.paths(2, 2);
        checkout(repo, findType(repo, repo.resolve(p[0]), "tree"), p[1]);
    } else if (command == "log") {
        auto p = args.paths(0, 1);
        string id = findType(repo, repo.resolve(p.empty() ? "HEAD" : p[0]), "commit");
        set<string> seen;
        cout << "digraph rizzlog {\n  node[shape=rect]\n";
        logHistory(repo, id, seen);
        cout << "}\n";
    } else if (command == "show-ref") {
        args.paths(0, 0);
        listRefs(repo, false);
    } else if (command == "tag") {
        bool annotated = args.flag("-a");
        auto p = args.paths(annotated ? 1 : 0, 2);
        if (p.empty()) listRefs(repo, true);
        else {
            WriteLock lock(repo);
            tag(repo, p[0], repo.resolve(p.size() == 2 ? p[1] : "HEAD"), annotated);
        }
    } else if (command == "ls-files") {
        bool verbose = args.flag("--verbose");
        args.paths(0, 0);
        Index index(repo);
        if (verbose) cout << "Index v2, " << index.files.size() << " entries\n";
        for (const auto& [name, e] : index.files) {
            cout << name << '\n';
            if (verbose) {
                auto f = e.fields;
                cout << "  mode " << e.mode() << " blob " << e.id << "\n  size " << f[9]
                     << " uid " << f[7] << " gid " << f[8] << " device " << f[4] << " inode " << f[5]
                     << "\n  ctime " << f[0] << "s " << f[1] << "ns mtime " << f[2] << "s " << f[3]
                     << "ns\n  stage 0 assume-valid " << bool(e.flags & 0x8000) << '\n';
            }
        }
    } else if (command == "check-ignore") {
        Index index(repo);
        IgnoreRules rules(repo, index);
        for (const string& p : args.paths(1, SIZE_MAX))
            if (rules.ignores(repo.fileName(p))) cout << p << '\n';
    } else throw runtime_error("Unknown command: " + command);
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || string(argv[1]) == "--help" || string(argv[1]) == "-h") help();
        else run(argv[1], Arguments(argc, argv));
        return 0;
    } catch (const exception& e) {
        cerr << "rizz: " << e.what() << '\n';
        return 1;
    }
}
