// firmir-doc — generates Markdown API documentation from Firmir-style
// documentation comments.
//
// A documentation comment is a run of lines starting with `///` (or `//!`)
// immediately preceding a declaration. Recognized tags:
//   \class Name   — documents a class/struct
//   \brief text   — one-line summary (also used for the index)
//   \param name   — parameter description
//   \return text  — return value description
// Plain text is rendered as-is; the documented declaration is shown in a
// fenced code block.
//
// Usage: firmir-doc <input-dir> <output-dir>
// Every *.hpp file under <input-dir> (recursively) produces
// <output-dir>/<relative-path>.md; an index.md lists all pages.

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

struct Entry {
    std::string kind; // "class", "member" or "function"
    std::string owner; // owning class for members
    std::string name;
    std::string signature;
    std::vector<std::string> doc;
    std::string brief;
};

struct Page {
    std::string relPath;
    std::vector<Entry> entries;
};

std::string trim(const std::string& s) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    const auto first = std::find_if(s.begin(), s.end(), notSpace);
    const auto last = std::find_if(s.rbegin(), s.rend(), notSpace).base();
    if (first >= last)
        return {};
    return std::string(first, last);
}

/// Strips the comment marker and a single space from a doc line.
std::string stripMarker(const std::string& line) {
    std::string s = trim(line);
    if (s.rfind("///", 0) == 0)
        s = s.substr(3);
    else if (s.rfind("//!", 0) == 0)
        s = s.substr(3);
    if (!s.empty() && s.front() == ' ')
        s.erase(s.begin());
    return s;
}

bool isDocLine(const std::string& line) {
    const std::string s = trim(line);
    return s.rfind("///", 0) == 0 || s.rfind("//!", 0) == 0;
}

std::string extractBrief(const std::vector<std::string>& doc) {
    for (const auto& line : doc) {
        if (line.rfind("\\brief", 0) == 0)
            return trim(line.substr(6));
    }
    return {};
}
/// Renders doc lines, translating the recognized tags into Markdown.
std::string renderDoc(const std::vector<std::string>& doc) {
    std::string out;
    for (const auto& line : doc) {
        if (line.rfind("\\class", 0) == 0)
            continue; // already used for the heading
        if (line.rfind("\\brief", 0) == 0) {
            out += trim(line.substr(6)) + "\n\n";
            continue;
        }
        if (line.rfind("\\param", 0) == 0) {
            out += "- **param** `" + trim(line.substr(6)) + "`\n";
            continue;
        }
        if (line.rfind("\\return", 0) == 0) {
            out += "- **returns** " + trim(line.substr(7)) + "\n";
            continue;
        }
        out += line + "\n";
    }
    if (!out.empty() && out.back() != '\n')
        out += '\n';
    return out;
}

/// Counts '{' minus '}' on a line (comment lines excluded by the caller).
int braceDelta(const std::string& line) {
    int delta = 0;
    bool inString = false;
    char quote = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inString) {
            if (c == quote && line[i - 1] != '\\')
                inString = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            inString = true;
            quote = c;
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/')
            break;
        if (c == '{')
            ++delta;
        else if (c == '}')
            --delta;
    }
    return delta;
}

const std::regex& classRe() {
    static const std::regex re(
        R"(^\s*(?:template\s*<[^>]*>\s*)?(class|struct)\s+(\w+))");
    return re;
}

const std::regex& nameRe() {
    // Last identifier before '(' — a rough function/method name.
    static const std::regex re(R"((\w+)\s*\()");
    return re;
}

Page parseHeader(const std::string& path, const std::string& relPath) {
    Page page;
    page.relPath = relPath;

    std::ifstream in(path);
    std::vector<std::string> doc;
    std::string currentClass;
    int depth = 0;
    int classDepth = 0;

    std::string raw;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();
        const std::string line = trim(raw);

        if (isDocLine(line)) {
            doc.push_back(stripMarker(line));
            continue;
        }
        if (line.empty()) {
            doc.clear();
            continue;
        }

        // A declaration follows: emit the pending doc block if any.
        std::smatch m;
        if (std::regex_search(line, m, classRe())) {
            const std::string className = m[2].str();
            currentClass = className;
            classDepth = depth;
            if (!doc.empty()) {
                Entry e;
                e.kind = "class";
                e.name = className;
                e.signature = line;
                e.doc = doc;
                e.brief = extractBrief(doc);
                page.entries.push_back(std::move(e));
            }
        } else if (!doc.empty() && line.find('(') != std::string::npos) {
            Entry e;
            e.signature = line;
            e.doc = doc;
            e.brief = extractBrief(doc);
            if (std::regex_search(line, m, nameRe()))
                e.name = m[1].str();
            if (!currentClass.empty() && depth > classDepth) {
                e.kind = "member";
                e.owner = currentClass;
            } else {
                e.kind = "function";
            }
            page.entries.push_back(std::move(e));
        }
        doc.clear();

        depth += braceDelta(line);
        if (!currentClass.empty() && depth <= classDepth)
            currentClass.clear();
    }
    return page;
}

void collectHeaders(const std::string& dir, const std::string& prefix,
                    std::vector<std::string>& out) {
    DIR* d = ::opendir(dir.c_str());
    if (!d)
        return;
    struct dirent* ent;
    while ((ent = ::readdir(d))) {
        const std::string name = ent->d_name;
        if (name == "." || name == "..")
            continue;
        const std::string full = dir + "/" + name;
        const std::string rel = prefix.empty() ? name : prefix + "/" + name;
        struct stat st {};
        if (::stat(full.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            collectHeaders(full, rel, out);
        } else if (name.size() > 4 &&
                   name.compare(name.size() - 4, 4, ".hpp") == 0) {
            out.push_back(rel);
        }
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
}

bool makePath(const std::string& dir) {
    std::string current;
    for (const char c : dir) {
        current += c;
        if (c == '/' && current.size() > 1) {
            if (::mkdir(current.c_str(), 0755) != 0 && errno != EEXIST)
                return false;
        }
    }
    return ::mkdir(dir.c_str(), 0755) == 0 || errno == EEXIST;
}

std::string renderPage(const Page& page) {
    std::string md = "# `" + page.relPath + "`\n\n";
    for (const auto& e : page.entries) {
        if (e.kind == "class") {
            md += "## class `" + e.name + "`\n\n";
        } else if (e.kind == "member") {
            md += "### `" + e.owner + "::" + e.name + "`\n\n";
        } else {
            md += "## `" + e.name + "`\n\n";
        }
        md += renderDoc(e.doc);
        md += "```cpp\n" + e.signature + "\n```\n\n";
    }
    return md;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <input-dir> <output-dir>\n", argv[0]);
        return 2;
    }
    const std::string inputDir = argv[1];
    const std::string outputDir = argv[2];

    std::vector<std::string> headers;
    collectHeaders(inputDir, "", headers);
    if (headers.empty()) {
        std::fprintf(stderr, "no .hpp files found in %s\n", inputDir.c_str());
        return 1;
    }

    std::string index = "# Firmir API reference\n\n";
    for (const auto& rel : headers) {
        const Page page = parseHeader(inputDir + "/" + rel, rel);
        if (page.entries.empty())
            continue;
        const std::string outRel = rel + ".md";
        const std::string outPath = outputDir + "/" + outRel;
        const std::size_t slash = outPath.find_last_of('/');
        if (slash != std::string::npos && !makePath(outPath.substr(0, slash))) {
            std::fprintf(stderr, "cannot create %s\n", outPath.c_str());
            return 1;
        }
        std::ofstream out(outPath);
        out << renderPage(page);
        index += "- [" + rel + "](" + outRel + ")\n";
    }

    if (!makePath(outputDir)) {
        std::fprintf(stderr, "cannot create %s\n", outputDir.c_str());
        return 1;
    }
    std::ofstream indexOut(outputDir + "/index.md");
    indexOut << index;
    std::printf("generated documentation for %zu header(s) in %s\n",
                headers.size(), outputDir.c_str());
    return 0;
}

