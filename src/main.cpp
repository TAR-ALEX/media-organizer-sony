#include "tiff.h"
#include <estd/AnsiEscape.hpp>
#include <estd/filesystem.hpp>
#include <fstream>
#include <iostream>
#include <locale>

using namespace std;
using namespace estd::files;
using namespace estd::string_util;

vector<char> readAll(Path p) {
    std::ifstream file(p, std::ios::binary | std::ios::ate);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) { return {}; }
    return buffer;
}

template <typename TP>
// <date, time>
std::string toTimeStrings(TP tp) {
    string date = "";
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now() + system_clock::now());

    std::time_t tt = system_clock::to_time_t(sctp);
    std::tm* gmt = std::localtime(&tt);
    std::stringstream buffer;

    buffer << std::put_time(gmt, "%Y:%m:%d %H:%M:%S");
    date = buffer.str();

    return date;
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
        if (loExt == "jpg" || loExt == "jpeg" || loExt == "arw" || loExt == "tiff") {
            string datetime = "";
            try {
                if (loExt == "jpg" || loExt == "jpeg") {
                    datetime = getJpegCreationTime(itpath);
                } else {
                    datetime = getTiffCreationTime(itpath);
                }
            } catch (...) { datetime = toTimeStrings(getModificationTime(itpath)); }
            try {
                std::tie(date, timePlusExt) = dateStringToNames(datetime);
                timePlusExt += "." + itpath.getLongExtention();
            } catch (...) { throw runtime_error(from.string() + " could not parse date"); }
        } else if (loExt == "pp3") {
            continue;
        } else {
            try {
                auto path = itpath;
                std::tie(date, timePlusExt) = dateStringToNames(toTimeStrings(getModificationTime(path)));
                timePlusExt += "." + itpath.getLongExtention();
            } catch (...) { throw runtime_error(from.string() + " could not parse date"); }
        }

        createDirectories(to / date);
        Path newPath = to / date / timePlusExt;

        std::string loLongExt = toLower(itpath.getLongExtention());
        bool containsRawSubExt = estd::string_util::hasPrefix(loLongExt, "raw");
        if (loExt == "arw" || exists(itpath + ".pp3")) {
            auto splt = newPath.splitSuffix();
            createDirectories(splt.first / "raw");
            newPath = splt.first / "raw" / splt.second;
        }else if(containsRawSubExt && loExt == "mp4"){
            auto splt = newPath.splitSuffix();
            createDirectories(splt.first / "rawvid");
            newPath = splt.first / "rawvid" / splt.second;
        }else if(containsRawSubExt){
            auto splt = newPath.splitSuffix();
            createDirectories(splt.first / "rawjpg");
            newPath = splt.first / "rawjpg" / splt.second;
        }

        if (exists(newPath)) {
            dupCount++;
            // throw runtime_error(itpath+" duplicate found!");
            for (int i = 0; i < 10000; i++) {
                std::stringstream ss;
                ss << std::setw(2) << std::setfill('0') << i;
                Path deDupPath =
                    newPath.splitLongExtention().first + "dup" + ss.str() + "." + newPath.splitLongExtention().second;
                if (!exists(deDupPath)) {
                    newPath = deDupPath;
                    break;
                }
            }
        }
        cout << estd::moveCursor(0, 2) << estd::clearAfterCursor << estd::moveCursor(0, 2);
        cout << estd::clearSettings << "Progress:   " << estd::setTextColor(0, 255, 0)
             << progress * 100.0 / paths.size() << " %\n";
        cout << estd::clearSettings << "Duplicates: " << estd::setTextColor(255, 100, 100) << dupCount << "\n\n";
        cout << estd::clearSettings << "Dir:  " << estd::setTextColor(255, 255, 0) << date << "\n";
        cout << estd::clearSettings << "From: " << estd::setTextColor(255, 255, 0) << itpath << "\n";
        cout << estd::clearSettings << "To:   " << estd::setTextColor(255, 255, 0) << newPath.normalize() << "\n";

        copy(itpath, newPath.normalize());
        setModificationTime(newPath.normalize(), getModificationTime(itpath));

        if (exists(itpath + ".pp3")) {
            copy(itpath + ".pp3", newPath.normalize() + ".pp3");
            setModificationTime(newPath.normalize() + ".pp3", getModificationTime(itpath + ".pp3"));
        }
    }

    cout << estd::moveCursor(0, 2) << estd::clearAfterCursor << estd::moveCursor(0, 2);
    cout << estd::clearSettings << "Progress:   " << estd::setTextColor(0, 255, 0) << 100 << " %\n";

    if (dupCount) {
        cout << estd::setTextColor(255, 100, 100);
        std::cout << "\n" << dupCount << " potential duplicates\n";
    }

    for (auto it : RecursiveDirectoryIterator(to)) {
        if (it.path().isDirectory()) continue;
        paths.insert(it.path());
        toFileCount++;
    }
    if (fromFileCount != toFileCount) cout << estd::setTextColor(255, 100, 100);
    else
        cout << estd::setTextColor(0, 255, 0);
    std::cout << "\n" << fromFileCount << "    original media count";
    std::cout << "\n" << toFileCount << "      copied media count\n";
}

// #include <chrono>
// #include <thread>
// using namespace std::chrono_literals;

void markRaw(Path p) {
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
        cout << estd::moveCursor(0, 2) << estd::clearAfterCursor << estd::moveCursor(0, 2);
        cout << estd::clearSettings << "Progress:   " << estd::setTextColor(0, 255, 0)
             << progress * 100.0 / paths.size() << " %\n";
        if (path.splitLongExtention().second == "") {
            renamed = path.splitLongExtention().first + ".raw";
        } else if (estd::string_util::hasPrefix(path.splitLongExtention().second, "raw")) {
            continue;
        } else {
            renamed = path.splitLongExtention().first + ".raw." + path.splitLongExtention().second;
        }
        cout << estd::clearSettings << "\nRenaming: " << renamed  << estd::clearSettings << endl;
        // std::this_thread::sleep_for(1000ms);

        rename(path, renamed);
    }
    for (auto it : DirectoryIterator(p)) {
        if (it.path().isDirectory()) continue;
        paths.insert(it.path());
        toFileCount++;
    }
    if (fromFileCount != toFileCount) cout << estd::setTextColor(255, 100, 100);
    else
        cout << estd::setTextColor(0, 255, 0);
    std::cout << "\n" << fromFileCount << "   original media count";
    std::cout << "\n" << toFileCount << "    renamed media count\n";
}

void unmarkRaw(Path p) {
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
        cout << estd::moveCursor(0, 2) << estd::clearAfterCursor << estd::moveCursor(0, 2);
        cout << estd::clearSettings << "Progress:   " << estd::setTextColor(0, 255, 0)
             << progress * 100.0 / paths.size() << " %\n";

        if (!estd::string_util::hasPrefix(path.splitLongExtention().second, "raw")) {
            continue;
        }else{
            auto newext = estd::string_util::replacePrefix(path.getLongExtention(), "raw", "");
            if(estd::string_util::hasPrefix(newext, ".")) newext.substr(1);
            renamed = path.splitLongExtention().first + newext;
        }
        cout << estd::clearSettings << "\nRenaming: " << renamed  << estd::clearSettings << endl;

        rename(path, renamed);
    }
    for (auto it : DirectoryIterator(p)) {
        if (it.path().isDirectory()) continue;
        paths.insert(it.path());
        toFileCount++;
    }
    if (fromFileCount != toFileCount) cout << estd::setTextColor(255, 100, 100);
    else
        cout << estd::setTextColor(0, 255, 0);
    std::cout << "\n" << fromFileCount << "   original media count";
    std::cout << "\n" << toFileCount << "    renamed media count\n";
}

int main(int argc, char* argv[]) {
    // sortDir(from, to);
    // return 0;

    if (argc < 2) {
        std::cout << estd::setTextColor(255, 100, 100)
                  << "Wrong number of arguments, expected option `markraw` or `unmarkraw` or `organize`\n";
        return 1;
    }

    if (estd::string_util::toLower(argv[1]) == "markraw") {
        if (argc < 3) {
            std::cout << estd::setTextColor(255, 100, 100)
                      << "Wrong number of arguments, expected option `markraw` followed by directory to rename.";
            return 1;
        }
        markRaw(argv[2]);
    }else if (estd::string_util::toLower(argv[1]) == "unmarkraw") {
        if (argc < 3) {
            std::cout << estd::setTextColor(255, 100, 100)
                      << "Wrong number of arguments, expected option `unmarkraw` followed by directory to rename.";
            return 1;
        }
        unmarkRaw(argv[2]);
    } else if (estd::string_util::toLower(argv[1]) == "organize") {
        if (argc != 4) {
            std::cout << estd::setTextColor(255, 100, 100)
                      << "Wrong number of arguments, expected 3\nexample: media-organizer organize \"From/Dir/\" "
                         "\"To/Dir/\"\n";
            return 1;
        }

        sortDir(std::string(argv[2]), std::string(argv[3]));
    } else {
        std::cout << estd::setTextColor(255, 100, 100) << "Unknown option `" << argv[1]
                  << "`, expected option `markraw` or `organize`\n";
        return 1;
    }
    return 0;
}