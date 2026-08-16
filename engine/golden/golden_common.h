#pragma once
#include "bitboard.h"
#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>

namespace golden {

struct GoldenPair {
    std::string id;
    std::string description;
    std::array<std::string, 9> boardA{};
    std::array<std::string, 9> boardB{};
    int line_number = 0;
    std::string file;
};

inline std::string trim(const std::string &s) {
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline std::string toLower(const std::string &s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c){ return std::tolower(c); });
    return r;
}

inline bool isBoardLine(const std::string &raw) {
    std::string s = trim(raw);
    if (s.size() != 9) return false;
    for (char c : s) {
        if (c == '.' || c == '#') continue;
        return false;
    }
    return true;
}

inline bool isSeparatorLine(const std::string &raw) {
    std::string s = trim(raw);
    if (s.empty()) return false;
    std::string lower = toLower(s);
    if (lower == ">" || lower == ">>" || lower == "->" || lower == "-->" ||
        lower == "vs" || lower == "v" || lower == "=>" || lower == "—>" ) return true;
    std::string stripped;
    for (char c : s) if (c != '-' && c != '=' && !std::isspace((unsigned char)c)) stripped.push_back(c);
    std::string sl = toLower(stripped);
    if (sl == ">" || sl == ">>" || sl == "->" || sl == "vs") return true;
    return false;
}

inline bool isEmptyLine(const std::string &raw) {
    return trim(raw).empty();
}

inline bool isCommentLine(const std::string &raw) {
    std::string s = trim(raw);
    return !s.empty() && s[0] == '#';
}

inline bool isDecorativeLine(const std::string &raw) {
    std::string s = trim(raw);
    if (s.empty()) return false;
    bool allDeco = true;
    for (char c : s) if (c != '-' && c != '=' && c != '*' && c != '_') { allDeco = false; break; }
    return allDeco && s.size() >= 3;
}

inline BitBoard boardFromLines(const std::array<std::string,9> &lines) {
    BitBoard bb = BitBoard::empty();
    for (unsigned r = 0; r < 9; ++r) {
        std::string s = trim(lines[r]);
        for (unsigned c = 0; c < 9; ++c) {
            if (s[c] == '#') {
                bb = bb | (BitBoard::row(r) & BitBoard::column(c));
            }
        }
    }
    return bb;
}

inline std::string boardToAscii(BitBoard bb) {
    std::string out;
    for (unsigned r = 0; r < 9; ++r) {
        for (unsigned c = 0; c < 9; ++c) {
            out += bb.at(r,c) ? '#' : '.';
        }
        if (r != 8) out += "\n";
    }
    return out;
}

inline bool validateBoardStrings(const std::array<std::string,9> &arr, std::string &err, const std::string &path, int idx) {
    for (int i = 0; i < 9; ++i) {
        std::string s = trim(arr[i]);
        if (s.size() != 9) {
            err = path + ": entry " + std::to_string(idx) + " board row " + std::to_string(i) + " must be 9 chars, got '" + s + "' length " + std::to_string(s.size());
            return false;
        }
        for (char c : s) {
            if (c == '.' || c == '#') continue;
            err = path + ": entry " + std::to_string(idx) + " board row " + std::to_string(i) + " has invalid char '" + std::string(1,c) + "' in '" + s + "'";
            return false;
        }
    }
    return true;
}

// Forward declarations
inline std::vector<GoldenPair> parseGoldenFile(const std::string &path, std::string &error);
inline std::vector<GoldenPair> parseGoldenJsonFile(const std::string &path, std::string &error);
inline std::vector<GoldenPair> parseGoldenTxtFile(const std::string &path, std::string &error);

inline std::vector<GoldenPair> parseGoldenJsonFile(const std::string &path, std::string &error) {
    std::ifstream in(path);
    if (!in) { error = "cannot open file: " + path; return {}; }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string content = buf.str();

    size_t pos = 0;
    auto skipWs = [&]() {
        while (pos < content.size() && std::isspace((unsigned char)content[pos])) ++pos;
    };
    std::function<std::string(std::string&)> parseString = [&](std::string &err) -> std::string {
        skipWs();
        if (pos >= content.size() || content[pos] != '"') { err = "expected string opening quote"; return ""; }
        ++pos;
        std::string res;
        while (pos < content.size()) {
            char c = content[pos];
            if (c == '\\') {
                ++pos;
                if (pos >= content.size()) { err = "unterminated escape"; return ""; }
                char esc = content[pos];
                if (esc == '"') res.push_back('"');
                else if (esc == '\\') res.push_back('\\');
                else if (esc == 'n') res.push_back('\n');
                else if (esc == 't') res.push_back('\t');
                else if (esc == 'r') res.push_back('\r');
                else if (esc == 'b') res.push_back('\b');
                else if (esc == 'f') res.push_back('\f');
                else res.push_back(esc);
                ++pos;
            } else if (c == '"') {
                ++pos;
                break;
            } else {
                res.push_back(c);
                ++pos;
            }
        }
        return res;
    };

    auto parseStringArray = [&](std::string &err) -> std::vector<std::string> {
        std::vector<std::string> arr;
        skipWs();
        if (pos >= content.size() || content[pos] != '[') { err = "expected [ for array"; return {}; }
        ++pos;
        skipWs();
        if (pos < content.size() && content[pos] == ']') { ++pos; return arr; }
        while (true) {
            skipWs();
            std::string s = parseString(err);
            if (!err.empty()) return {};
            arr.push_back(s);
            skipWs();
            if (pos >= content.size()) { err = "unterminated array"; return {}; }
            if (content[pos] == ',') { ++pos; continue; }
            else if (content[pos] == ']') { ++pos; break; }
            else { err = std::string("expected , or ] in array, got '") + content[pos] + "'"; return {}; }
        }
        return arr;
    };

    skipWs();
    if (pos >= content.size() || content[pos] != '[') { error = path + ": top level must be an array starting with ["; return {}; }
    ++pos;
    std::vector<GoldenPair> pairs;
    int idx = 0;
    skipWs();
    if (pos < content.size() && content[pos] == ']') { ++pos; return pairs; }
    while (true) {
        skipWs();
        if (pos >= content.size()) { error = path + ": unexpected EOF in array"; return {}; }
        if (content[pos] == ']') { ++pos; break; }
        if (content[pos] != '{') { error = path + ": expected object { at entry " + std::to_string(idx+1); return {}; }
        ++pos;
        GoldenPair p;
        p.file = path;
        p.line_number = idx+1;
        std::string pendingId;
        std::string pendingDesc;
        std::array<std::string,9> arrA{}; bool hasA=false;
        std::array<std::string,9> arrB{}; bool hasB=false;
        while (true) {
            skipWs();
            if (pos >= content.size()) { error = path + ": unterminated object"; return {}; }
            if (content[pos] == '}') { ++pos; break; }
            std::string keyErr;
            std::string key = parseString(keyErr);
            if (!keyErr.empty()) { error = path + ": entry " + std::to_string(idx+1) + " key parse error: " + keyErr; return {}; }
            skipWs();
            if (pos >= content.size() || content[pos] != ':') { error = path + ": entry " + std::to_string(idx+1) + " expected : after key '" + key + "'"; return {}; }
            ++pos;
            skipWs();
            if (key == "id" || key == "name" || key == "pair") {
                std::string valErr;
                std::string val = parseString(valErr);
                if (!valErr.empty()) { error = path + ": entry " + std::to_string(idx+1) + " value for '" + key + "' parse error: " + valErr; return {}; }
                if (pendingId.empty()) pendingId = val;
            } else if (key == "description" || key == "desc" || key == "comment") {
                std::string valErr;
                std::string val = parseString(valErr);
                if (!valErr.empty()) { error = path + ": entry " + std::to_string(idx+1) + " value for '" + key + "' parse error: " + valErr; return {}; }
                if (pendingDesc.empty()) pendingDesc = val;
            } else if (key == "a" || key == "A" || key == "boardA" || key == "preferred" || key == "board_a") {
                std::string arrErr;
                auto arr = parseStringArray(arrErr);
                if (!arrErr.empty()) { error = path + ": entry " + std::to_string(idx+1) + " array '" + key + "' error: " + arrErr; return {}; }
                if (arr.size() != 9) { error = path + ": entry " + std::to_string(idx+1) + " (" + pendingId + ") '" + key + "' must have 9 strings, got " + std::to_string(arr.size()); return {}; }
                for (int i=0;i<9;++i) arrA[i]=arr[i];
                hasA=true;
            } else if (key == "b" || key == "B" || key == "boardB" || key == "other" || key == "board_b") {
                std::string arrErr;
                auto arr = parseStringArray(arrErr);
                if (!arrErr.empty()) { error = path + ": entry " + std::to_string(idx+1) + " array '" + key + "' error: " + arrErr; return {}; }
                if (arr.size() != 9) { error = path + ": entry " + std::to_string(idx+1) + " (" + pendingId + ") '" + key + "' must have 9 strings, got " + std::to_string(arr.size()); return {}; }
                for (int i=0;i<9;++i) arrB[i]=arr[i];
                hasB=true;
            } else {
                skipWs();
                if (pos < content.size() && content[pos] == '"') { std::string dummyErr; parseString(dummyErr); }
                else if (pos < content.size() && content[pos] == '[') { std::string dummyErr; parseStringArray(dummyErr); }
                else if (pos < content.size() && content[pos] == '{') {
                    int depth=0;
                    while (pos < content.size()) {
                        if (content[pos]=='"') { std::string dummyErr; parseString(dummyErr); continue; }
                        if (content[pos]=='{') ++depth;
                        else if (content[pos]=='}') { --depth; ++pos; if (depth==0) break; else continue; }
                        ++pos;
                    }
                } else {
                    while (pos < content.size() && content[pos]!=',' && content[pos]!='}') ++pos;
                }
            }
            skipWs();
            if (pos < content.size() && content[pos] == ',') { ++pos; continue; }
            else if (pos < content.size() && content[pos] == '}') { continue; }
        }
        ++idx;
        if (pendingId.empty()) p.id = "pair_" + std::to_string(idx);
        else p.id = pendingId;
        p.description = pendingDesc;
        if (!hasA) { error = path + ": entry " + std::to_string(idx) + " (" + p.id + ") missing 'a' board array"; return {}; }
        if (!hasB) { error = path + ": entry " + std::to_string(idx) + " (" + p.id + ") missing 'b' board array"; return {}; }
        p.boardA = arrA;
        p.boardB = arrB;
        std::string vErr;
        if (!validateBoardStrings(p.boardA, vErr, path, idx)) { error = vErr; return {}; }
        if (!validateBoardStrings(p.boardB, vErr, path, idx)) { error = vErr; return {}; }
        pairs.push_back(std::move(p));
        skipWs();
        if (pos < content.size() && content[pos] == ',') { ++pos; continue; }
        else if (pos < content.size() && content[pos] == ']') { ++pos; break; }
    }
    return pairs;
}

inline std::vector<GoldenPair> parseGoldenTxtFile(const std::string &path, std::string &error) {
    std::ifstream in(path);
    if (!in) { error = "cannot open file: " + path; return {}; }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    std::vector<GoldenPair> pairs;
    std::vector<std::string> pendingComments;
    std::string pendingId;
    size_t i = 0;
    while (i < lines.size()) {
        std::string raw = lines[i];
        std::string t = trim(raw);
        if (isEmptyLine(raw) || isDecorativeLine(raw)) {
            if (isDecorativeLine(raw)) { pendingComments.clear(); pendingId.clear(); }
            ++i; continue;
        }
        if (isBoardLine(raw)) {
            GoldenPair p;
            p.file = path;
            p.line_number = (int)i + 1;
            p.id = pendingId;
            if (!pendingComments.empty()) {
                std::string desc;
                for (size_t k = 0; k < pendingComments.size(); ++k) { if (k) desc += " "; desc += pendingComments[k]; }
                p.description = desc;
            }
            std::array<std::string,9> a{};
            for (int r = 0; r < 9; ++r) {
                if (i >= lines.size()) { error = path + ":" + std::to_string(p.line_number) + ": unexpected EOF while reading board A"; return {}; }
                std::string cur = lines[i];
                if (!isBoardLine(cur)) { error = path + ":" + std::to_string(i+1) + ": expected board line, got '" + trim(cur) + "'"; return {}; }
                a[r] = trim(cur); ++i;
            }
            p.boardA = a;
            while (i < lines.size() && !isBoardLine(lines[i]) && (isEmptyLine(lines[i]) || isCommentLine(lines[i]) || isDecorativeLine(lines[i]))) ++i;
            if (i >= lines.size()) { error = path + ":" + std::to_string(p.line_number) + ": missing separator '>' after board A"; return {}; }
            std::string sep = lines[i];
            if (!isSeparatorLine(sep)) { error = path + ":" + std::to_string(i+1) + ": expected separator '>' got '" + trim(sep) + "'"; return {}; }
            ++i;
            while (i < lines.size() && !isBoardLine(lines[i]) && (isEmptyLine(lines[i]) || isCommentLine(lines[i]) || isDecorativeLine(lines[i]))) ++i;
            if (i + 8 >= lines.size()) { error = path + ":" + std::to_string(p.line_number) + ": not enough lines for board B"; return {}; }
            std::array<std::string,9> b{};
            for (int r = 0; r < 9; ++r) {
                if (i >= lines.size()) { error = path + ":" + std::to_string(p.line_number) + ": unexpected EOF while reading board B"; return {}; }
                std::string cur = lines[i];
                if (!isBoardLine(cur)) { error = path + ":" + std::to_string(i+1) + ": expected board line for B, got '" + trim(cur) + "'"; return {}; }
                b[r] = trim(cur); ++i;
            }
            p.boardB = b;
            if (p.id.empty()) p.id = "pair_" + std::to_string(pairs.size() + 1);
            pairs.push_back(std::move(p));
            pendingComments.clear(); pendingId.clear(); continue;
        }
        if (isCommentLine(raw)) {
            std::string content = trim(t.substr(1));
            pendingComments.push_back(content);
            std::string lower = toLower(content);
            auto findId = [&](const std::string &prefix) -> std::string {
                if (lower.rfind(prefix, 0) == 0) {
                    std::string afterRaw = content.substr(prefix.size());
                    std::string after = trim(afterRaw);
                    if (after.empty() || after[0] != ':') return "";
                    after = trim(after.substr(1));
                    std::istringstream iss(after);
                    std::string token; iss >> token; return token;
                }
                return "";
            };
            std::string idTry = findId("id");
            if (idTry.empty()) idTry = findId("pair");
            if (idTry.empty()) idTry = findId("name");
            if (!idTry.empty() && pendingId.empty()) pendingId = idTry;
            ++i; continue;
        }
        error = path + ":" + std::to_string(i+1) + ": unexpected line '" + t + "'"; return {};
    }
    return pairs;
}

inline std::vector<GoldenPair> parseGoldenFile(const std::string &path, std::string &error) {
    std::string lower = toLower(path);
    bool isJsonPath = lower.size() >= 5 && lower.substr(lower.size()-5) == ".json";
    std::ifstream in(path);
    if (!in) { error = "cannot open file: " + path; return {}; }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string content = buf.str();
    std::string trimmed = trim(content);
    bool looksJson = !trimmed.empty() && (trimmed[0] == '[' || trimmed[0] == '{');
    if (isJsonPath || looksJson) {
        return parseGoldenJsonFile(path, error);
    } else {
        return parseGoldenTxtFile(path, error);
    }
}

inline std::string findDefaultGoldenFile() {
    std::vector<std::string> candidates = {
        "engine/golden/golden.json",
        "../engine/golden/golden.json",
        "../../engine/golden/golden.json",
        "engine/golden/golden.txt",
        "../engine/golden/golden.txt",
        "../../engine/golden/golden.txt",
        "engine/cpp/golden.json",
        "engine/cpp/golden.txt",
    };
    for (auto &c : candidates) {
        std::ifstream f(c);
        if (f) return c;
    }
    return "engine/golden/golden.json";
}

}
