#include <estd/filesystem.hpp>
#include <estd/isubstream.hpp>
#include <fstream>
#include <iostream>
#include <locale>
#include <map>
#include <sstream>
#include <regex>

using namespace std;
using namespace estd::files;
using namespace estd::string_util;

inline std::string getHeicCreationTime(Path p) {
    // HEIC files use the ISOBMFF container format.
    // EXIF data is stored as an item — we find it via iinf, locate it via iloc,
    // then parse the embedded EXIF for the creation datetime.

    std::ifstream file(std::string(p), std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Could not open HEIC file: " + std::string(p));

    auto readU32 = [&]() -> uint32_t {
        uint8_t b[4];
        file.read(reinterpret_cast<char*>(b), 4);
        return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
               (uint32_t(b[2]) << 8) | b[3];
    };
    auto readU16 = [&]() -> uint16_t {
        uint8_t b[2];
        file.read(reinterpret_cast<char*>(b), 2);
        return (uint16_t(b[0]) << 8) | b[1];
    };
    auto readSized = [&](uint8_t s) -> uint64_t {
        if (s == 0) return 0;
        if (s == 2) return readU16();
        if (s == 4) return readU32();
        if (s == 8) {
            uint64_t hi = readU32();
            uint64_t lo = readU32();
            return (hi << 32) | lo;
        }
        return 0;
    };

    struct ItemInfo { uint16_t id; std::string type; };
    struct ItemLoc  { uint16_t id; uint64_t offset; uint64_t length; };
    std::vector<ItemInfo> itemInfos;
    std::vector<ItemLoc>  itemLocs;

    // Walk top-level ISOBMFF boxes
    while (file.peek() != EOF) {
        uint64_t boxPos = file.tellg();
        uint32_t size32 = readU32();
        char typeBuf[4];
        file.read(typeBuf, 4);
        std::string boxType(typeBuf, 4);

        uint64_t boxEnd;
        if (size32 == 1) {
            uint64_t hi = readU32();
            uint64_t lo = readU32();
            boxEnd = boxPos + (hi << 32 | lo);
        } else if (size32 == 0) {
            file.seekg(0, std::ios::end);
            boxEnd = file.tellg();
            file.seekg(boxPos + 8);
        } else {
            boxEnd = boxPos + size32;
        }

        if (boxType == "meta") {
            // Full box: skip version(1) + flags(3)
            file.seekg(4, std::ios::cur);

            // Parse sub-boxes inside meta
            while (uint64_t(file.tellg()) < boxEnd) {
                uint64_t subPos = file.tellg();
                uint32_t subSz = readU32();
                char subBuf[4];
                file.read(subBuf, 4);
                std::string subType(subBuf, 4);

                uint64_t subEnd;
                if (subSz == 1) {
                    uint64_t hi = readU32();
                    uint64_t lo = readU32();
                    subEnd = subPos + (hi << 32 | lo);
                } else if (subSz == 0) {
                    subEnd = boxEnd;
                } else {
                    subEnd = subPos + subSz;
                }

                // ── iinf: item information ──────────────────────────
                if (subType == "iinf") {
                    uint8_t ver;
                    file.read(reinterpret_cast<char*>(&ver), 1);
                    file.seekg(3, std::ios::cur); // flags
                    uint32_t count = (ver == 0) ? readU16() : readU32();

                    for (uint32_t i = 0; i < count && uint64_t(file.tellg()) < subEnd; i++) {
                        uint64_t infePos = file.tellg();
                        uint32_t infeSz = readU32();
                        char infeBuf[4];
                        file.read(infeBuf, 4);
                        uint64_t infeEnd = infePos + infeSz;

                        uint8_t infeVer;
                        file.read(reinterpret_cast<char*>(&infeVer), 1);
                        file.seekg(3, std::ios::cur);

                        ItemInfo info{};
                        if (infeVer >= 2) {
                            info.id = (infeVer == 2) ? readU16()
                                                     : static_cast<uint16_t>(readU32());
                            readU16(); // item_protection_index
                            char it[4];
                            file.read(it, 4);
                            info.type = std::string(it, 4);
                        } else {
                            // v0/v1: item_name + content_type as null-terminated strings
                            info.id = readU16();
                            readU16(); // item_protection_index
                            char c;
                            while (file.get(c) && c != '\0') {}       // item_name
                            while (file.get(c) && c != '\0') info.type += c; // content_type
                        }
                        itemInfos.push_back(info);
                        file.seekg(infeEnd);
                    }
                }
                // ── iloc: item locations ────────────────────────────
                else if (subType == "iloc") {
                    uint8_t ver;
                    file.read(reinterpret_cast<char*>(&ver), 1);
                    file.seekg(3, std::ios::cur); // flags

                    uint8_t szs;
                    file.read(reinterpret_cast<char*>(&szs), 1);
                    uint8_t offSz = (szs >> 4) & 0xF;
                    uint8_t lenSz = szs & 0xF;

                    uint8_t boByte;
                    file.read(reinterpret_cast<char*>(&boByte), 1);
                    uint8_t boSz  = (boByte >> 4) & 0xF;
                    uint8_t idxSz = boByte & 0xF;   // used in v1/v2

                    uint16_t cnt = (ver < 2)
                        ? readU16()
                        : static_cast<uint16_t>(readU32());

                    for (uint16_t i = 0; i < cnt; i++) {
                        ItemLoc loc{};
                        loc.id = (ver < 2) ? readU16()
                                           : static_cast<uint16_t>(readU32());
                        if (ver >= 1) readU16(); // construction_method
                        readU16();               // data_reference_index
                        uint64_t baseOff = readSized(boSz);
                        uint16_t extCnt = readU16();

                        for (uint16_t e = 0; e < extCnt; e++) {
                            if (ver >= 1 && idxSz > 0)
                                readSized(idxSz);    // extent_index
                            uint64_t eOff = readSized(offSz);
                            uint64_t eLen = readSized(lenSz);
                            if (e == 0) {
                                loc.offset = baseOff + eOff;
                                loc.length = eLen;
                            }
                        }
                        itemLocs.push_back(loc);
                    }
                }

                file.seekg(subEnd);
            }
            break; // meta processed — done with top-level scan
        }

        file.seekg(boxEnd);
    }

    // ── Correlate: find the Exif item, then its location ──────────
    uint16_t exifId = 0xFFFF;
    bool foundItem = false;
    for (const auto& it : itemInfos) {
        if (it.type == "Exif") { exifId = it.id; foundItem = true; break; }
    }
    if (!foundItem)
        throw std::runtime_error("No Exif item in HEIC file");

    uint64_t offset = 0, length = 0;
    bool foundLoc = false;
    for (const auto& loc : itemLocs) {
        if (loc.id == exifId) {
            offset = loc.offset;
            length = loc.length;
            foundLoc = true;
            break;
        }
    }
    if (!foundLoc)
        throw std::runtime_error("No Exif location in HEIC file");

    // ── Read the raw EXIF data ─────────────────────────────────────
    file.seekg(offset);
    std::vector<char> buf(length);
    file.read(buf.data(), length);
    file.close();

    // The EXIF item payload starts with a 4-byte big-endian offset to
    // the TIFF header, followed by "Exif\0\0" and then TIFF/EXIF data.
    // A pragmatic approach: search for the datetime pattern directly.
    // EXIF DateTimeOriginal format: "YYYY:MM:DD HH:MM:SS"
    std::string exif(buf.begin(), buf.end());
    static const std::regex dtRe(R"((\d{4}:\d{2}:\d{2} \d{2}:\d{2}:\d{2}))");
    std::smatch m;
    if (std::regex_search(exif, m, dtRe))
        return m[1].str();

    throw std::runtime_error("No creation time found in HEIC EXIF data");
}