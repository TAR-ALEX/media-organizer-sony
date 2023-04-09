#include "tiff.h"
#include <estd/AnsiEscape.hpp>
#include <estd/filesystem.hpp>
#include <estd/ostream_proxy.hpp>
#include <fstream>
#include <iostream>
#include <locale>
#include <regex>

using namespace std;
using namespace estd::files;
using namespace estd::string_util;

estd::ostream_proxy info{&std::cout};

//selects the time in the label if the modification time is less than 2 hours from it
std::string selectBestTime(std::string label, std::string modification) {
    auto dateToInt = [](std::string s) {
        static const regex rex{R"regex(^([\d]{2,})--([\d]{2,})-([\d]{2,})-([\d]{2,}))regex"};
        smatch m;
        regex_search(s, m, rex);
        if (m.size() != 5) return int64_t{-1};
        else {
            int64_t result = 0;
            result += stoi(m[1]);
            result *= 24;
            result += stoi(m[2]);
            result *= 60;
            result += stoi(m[3]);
            result *= 60;
            result += stoi(m[4]);
            return result;
        }
    };

    int64_t l = dateToInt(label);
    int64_t m = dateToInt(modification);
    if (l == -1) return modification;

    if (abs(l - m) < 2 * 60 * 60) return label;
    return modification;
}

std::string getPathTimeString(Path p) {
    Path suffix = p.getSuffix();
    string suffixStr = suffix.splitLongExtention().first;
    static const regex rex{R"regex(^[\d]{2,}--[\d]{2,}-[\d]{2,}-[\d]{2,})regex"};
    smatch m;
    regex_search(suffixStr, m, rex);
    if (m.size() != 1) return "";
    return m[0];
}

std::string toTimeStrings(FileTime tp) {
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(tp - FileTime::clock::now() + system_clock::now());

    std::time_t tt = system_clock::to_time_t(sctp);
    std::tm* gmt = std::localtime(&tt);
    std::stringstream buffer;

    buffer << std::put_time(gmt, "%Y:%m:%d %H:%M:%S");

    return buffer.str();
}

//folder, filename
std::pair<std::string, std::string> dateStringToNames(std::string datetime) {
    string date = "";
    string time = "";
    datetime = replace_all(datetime, " ", ":");
    auto tokens = splitAll(datetime, ":");
    if (tokens.size() >= 6) {
        date = tokens[0] + "-" + tokens[1];
        time = tokens[2] + "--" + tokens[3] + "-" + tokens[4] + "-" + tokens[5];
        if (tokens.size() >= 7) time += "bur" + tokens[6];
    } else {
        throw runtime_error("could not parse date");
    }
    return {date, time};
}

void sortDir(Path from, Path to) {
    createDirectories(to);
    uint64_t dupCount = 0;
    std::set<Path> paths;
    uint64_t fromFileCount = 0;
    uint64_t toFileCount = 0;
    for (auto it : RecursiveDirectoryIterator(from)) {
        if (it.path().isDirectory()) continue;
        paths.insert(it.path());
        fromFileCount++;
    }
    int progress = 0;
    for (auto itpath : paths) {
        progress++;
        if (itpath.isDirectory()) continue;

        std::string date = "";
        std::string timePlusExt = "";
        std::string extention = "";

        string loExt = toLower(itpath.getExtention());
        if (loExt == ".jpg" || loExt == ".jpeg" || loExt == ".arw" || loExt == ".tiff") {
            string datetime = "";
            try {
                if (loExt == ".jpg" || loExt == ".jpeg") {
                    datetime = getJpegCreationTime(itpath);
                } else {
                    datetime = getTiffCreationTime(itpath);
                }
                std::tie(date, timePlusExt) = dateStringToNames(datetime);
                timePlusExt += itpath.getLongExtention();
            } catch (...) {
                try {
                    std::tie(date, timePlusExt) = dateStringToNames(toTimeStrings(getModificationTime(itpath)));
                    std::string oldName = getPathTimeString(itpath);
                    timePlusExt = selectBestTime(oldName, timePlusExt);
                    timePlusExt += itpath.getLongExtention();
                } catch (...) { throw runtime_error(itpath + " could not parse date"); }
            }

        } else if (loExt == ".pp3") {
            continue;
        } else {
            try {
                std::tie(date, timePlusExt) = dateStringToNames(toTimeStrings(getModificationTime(itpath)));
                std::string oldName = getPathTimeString(itpath);
                timePlusExt = selectBestTime(oldName, timePlusExt);
                if (itpath.hasExtention()) timePlusExt += itpath.getLongExtention();
            } catch (...) { throw runtime_error(itpath + " could not parse date"); }
        }

        createDirectories(to / date);
        Path newPath = to / date / timePlusExt;

        std::string loLongExt = toLower(itpath.getLongExtention());
        bool containsRawSubExt = estd::string_util::contains(loLongExt, ".raw");
        bool containsPrivSubExt = estd::string_util::contains(loLongExt, ".priv");
        if (loExt == ".arw" || exists(itpath + ".pp3")) {
            auto splt = newPath.splitSuffix();
            createDirectories(splt.first / "raw");
            newPath = splt.first / "raw" / splt.second;
        } else if (containsRawSubExt && loExt == ".mp4") {
            auto splt = newPath.splitSuffix();
            createDirectories(splt.first / "rawvid");
            newPath = splt.first / "rawvid" / splt.second;
        } else if (containsRawSubExt) {
            auto splt = newPath.splitSuffix();
            createDirectories(splt.first / "rawjpg");
            newPath = splt.first / "rawjpg" / splt.second;
        } else if (containsPrivSubExt) {
            auto splt = newPath.splitSuffix();
            createDirectories(splt.first / "private");
            newPath = splt.first / "private" / splt.second;
        }

        if (exists(newPath)) {
            dupCount++;
            // throw runtime_error(itpath+" duplicate found!");
            for (int i = 0; i < 10000; i++) {
                std::stringstream ss;
                ss << std::setw(2) << std::setfill('0') << i;
                Path deDupPath =
                    newPath.splitLongExtention().first + "dup" + ss.str() + newPath.splitLongExtention().second;
                if (!exists(deDupPath)) {
                    newPath = deDupPath;
                    break;
                }
            }
        }
        info << estd::moveCursor(0, 2) << estd::clearAfterCursor << estd::moveCursor(0, 2);
        info << estd::clearSettings << "Progress:   " << estd::setTextColor(0, 255, 0)
             << progress * 100.0 / paths.size() << " %\n";
        info << estd::clearSettings << "Duplicates: " << estd::setTextColor(255, 100, 100) << dupCount << "\n\n";
        info << estd::clearSettings << "Dir:  " << estd::setTextColor(255, 255, 0) << date << "\n";
        info << estd::clearSettings << "From: " << estd::setTextColor(255, 255, 0) << itpath << "\n";
        info << estd::clearSettings << "To:   " << estd::setTextColor(255, 255, 0) << newPath.normalize() << "\n";

        copy(itpath, newPath.normalize());
        setModificationTime(newPath.normalize(), getModificationTime(itpath));

        if (exists(itpath + ".pp3")) {
            copy(itpath + ".pp3", newPath.normalize() + ".pp3");
            setModificationTime(newPath.normalize() + ".pp3", getModificationTime(itpath + ".pp3"));
        }
    }

    info << estd::moveCursor(0, 2) << estd::clearAfterCursor << estd::moveCursor(0, 2);
    info << estd::clearSettings << "Progress:   " << estd::setTextColor(0, 255, 0) << 100 << " %\n";

    if (dupCount) {
        info << estd::setTextColor(255, 100, 100);
        info << "\n" << dupCount << " potential duplicates\n";
    }

    for (auto it : RecursiveDirectoryIterator(to)) {
        if (it.path().isDirectory()) continue;
        paths.insert(it.path());
        toFileCount++;
    }
    if (fromFileCount != toFileCount) info << estd::setTextColor(255, 100, 100);
    else
        info << estd::setTextColor(0, 255, 0);
    info << "\n" << fromFileCount << "    original media count";
    info << "\n" << toFileCount << "      copied media count\n";
}

// #include <chrono>
// #include <thread>
// using namespace std::chrono_literals;

void markExt(Path p, string cext) {
    cext = std::string(".") + cext;
    std::set<Path> paths;
    uint64_t fromFileCount = 0;
    uint64_t toFileCount = 0;
    uint64_t progress = 0;
    for (auto it : DirectoryIterator(p)) {
        if (it.path().isDirectory()) continue;
        paths.insert(it.path());
        fromFileCount++;
    }
    for (auto path : paths) {
        Path renamed = "";
        progress++;
        info << estd::moveCursor(0, 2) << estd::clearAfterCursor << estd::moveCursor(0, 2);
        info << estd::clearSettings << "Progress:   " << estd::setTextColor(0, 255, 0)
             << progress * 100.0 / paths.size() << " %\n";
        if (estd::string_util::hasPrefix(path.splitLongExtention().second, cext)) {
            continue;
        } else {
            renamed = path.splitLongExtention().first + cext + path.splitLongExtention().second;
        }
        info << estd::clearSettings << "\nRenaming: " << renamed << estd::clearSettings << endl;
        // std::this_thread::sleep_for(1000ms);

        rename(path, renamed);
    }
    for (auto it : DirectoryIterator(p)) {
        if (it.path().isDirectory()) continue;
        paths.insert(it.path());
        toFileCount++;
    }
    if (fromFileCount != toFileCount) info << estd::setTextColor(255, 100, 100);
    else
        info << estd::setTextColor(0, 255, 0);
    info << "\n" << fromFileCount << "   original media count";
    info << "\n" << toFileCount << "    renamed media count\n";
}

void unmarkExt(Path p, string cext) {
    std::set<Path> paths;
    uint64_t fromFileCount = 0;
    uint64_t toFileCount = 0;
    uint64_t progress = 0;
    for (auto it : DirectoryIterator(p)) {
        if (it.path().isDirectory()) continue;
        paths.insert(it.path());
        fromFileCount++;
    }
    for (auto path : paths) {
        Path renamed = "";
        progress++;
        info << estd::moveCursor(0, 2) << estd::clearAfterCursor << estd::moveCursor(0, 2);
        info << estd::clearSettings << "Progress:   " << estd::setTextColor(0, 255, 0)
             << progress * 100.0 / paths.size() << " %\n";

        if (!estd::string_util::hasPrefix(path.splitLongExtention().second, cext)) {
            continue;
        } else {
            auto newext = estd::string_util::replacePrefix(path.getLongExtention(), cext, "");
            renamed = path.splitLongExtention().first + newext;
        }
        info << estd::clearSettings << "\nRenaming: " << renamed << estd::clearSettings << endl;

        rename(path, renamed);
    }
    for (auto it : DirectoryIterator(p)) {
        if (it.path().isDirectory()) continue;
        paths.insert(it.path());
        toFileCount++;
    }
    if (fromFileCount != toFileCount) info << estd::setTextColor(255, 100, 100);
    else
        info << estd::setTextColor(0, 255, 0);
    info << "\n" << fromFileCount << "   original media count";
    info << "\n" << toFileCount << "    renamed media count\n";
}

int main(int argc, char* argv[]) {
    // sortDir(from, to);
    // return 0;

    if (argc < 2) {
        info << estd::setTextColor(255, 100, 100)
             << "Wrong number of arguments, expected option `markraw` or `unmarkraw` or `organize`\n";
        return 1;
    }

    if (estd::string_util::toLower(argv[1]) == "markraw") {
        if (argc < 3) {
            info << estd::setTextColor(255, 100, 100)
                 << "Wrong number of arguments, expected option `markraw` followed by directory to rename.";
            return 1;
        }
        markExt(argv[2], "raw");
    } else if (estd::string_util::toLower(argv[1]) == "unmarkraw") {
        if (argc < 3) {
            info << estd::setTextColor(255, 100, 100)
                 << "Wrong number of arguments, expected option `unmarkraw` followed by directory to rename.";
            return 1;
        }
        markExt(argv[2], "raw");
    } else if (estd::string_util::toLower(argv[1]) == "markpriv") {
        if (argc < 3) {
            info << estd::setTextColor(255, 100, 100)
                 << "Wrong number of arguments, expected option `markpriv` followed by directory to rename.";
            return 1;
        }
        markExt(argv[2], "priv");
    } else if (estd::string_util::toLower(argv[1]) == "unmarkpriv") {
        if (argc < 3) {
            info << estd::setTextColor(255, 100, 100)
                 << "Wrong number of arguments, expected option `markpriv` followed by directory to rename.";
            return 1;
        }
        markExt(argv[2], "priv");
    } else if (estd::string_util::toLower(argv[1]) == "organize" || estd::string_util::toLower(argv[1]) == "sort") {
        if (argc != 4) {
            info << estd::setTextColor(255, 100, 100)
                 << "Wrong number of arguments, expected 3\nexample: media-organizer organize \"From/Dir/\" "
                    "\"To/Dir/\"\n";
            return 1;
        }

        sortDir(std::string(argv[2]), std::string(argv[3]));
    } else {
        info << estd::setTextColor(255, 100, 100) << "Unknown option `" << argv[1]
             << "`, expected option `markraw` or `organize`\n";
        return 1;
    }
    return 0;
}