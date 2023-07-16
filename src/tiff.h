#include <estd/filesystem.hpp>
#include <estd/isubstream.hpp>
#include <fstream>
#include <iostream>
#include <locale>
#include <map>
#include <sstream>

using namespace std;
using namespace estd::files;
using namespace estd::string_util;

struct TiffIDF {
    uint16_t tag = 0;
    uint16_t dataType = 0;
    uint32_t length = 0;
    uint32_t value = 0;
};

template <typename T>
T swap_endian(T u) {
    union {
        T u;
        unsigned char u8[sizeof(T)];
    } source, dest;

    source.u = u;

    for (size_t k = 0; k < sizeof(T); k++) dest.u8[k] = source.u8[sizeof(T) - k - 1];

    return dest.u;
}

template <class datatype = uint8_t>
datatype seekAndRead(istream& file, size_t pos, bool nonIntel = false) {
    file.seekg(pos, ios::beg);
    datatype val = 0;
    if (!file.read((char*)(&val), sizeof(val))) throw runtime_error("read error");
    if (nonIntel) val = swap_endian(val);
    return val;
}

std::string seekAndRead(istream& file, size_t pos, size_t len) {
    file.seekg(pos, ios::beg);
    std::string val = "";
    val.resize(len, '\0');
    if (!file.read(val.data(), len)) throw runtime_error("read error");
    return val;
}

std::map<uint16_t, TiffIDF> parseTiffHeader(istream& data, size_t tagPtr, bool byteOrder) {
    std::map<uint16_t, TiffIDF> result;
    uint16_t numDirEntries = seekAndRead<uint16_t>(data, tagPtr, byteOrder);
    // std::cout << numDirEntries << endl;
    // std::cout << estd::string_util::escape_string(seekAndRead(data, tagPtr, 5)) << endl;
    tagPtr += 2;
    int j = 0;
    // try{
    for (int i = 0; i < numDirEntries; i++) {
        auto ref = tagPtr + 12 * i;

        TiffIDF idf;
        idf.tag = seekAndRead<uint16_t>(data, ref, byteOrder);
        idf.dataType = seekAndRead<uint16_t>(data, ref + 2, byteOrder);
        idf.length = seekAndRead<uint32_t>(data, ref + 4, byteOrder);

        idf.value = seekAndRead<uint32_t>(data, ref + 8, byteOrder);

        if (idf.length <= 1) {
            if (idf.dataType == 1 || idf.dataType == 6 || idf.dataType == 7) idf.value &= 0xFF;
            else if (idf.dataType == 3 || idf.dataType == 8)
                idf.value &= 0xFFFF;
        }
        result[idf.tag] = idf;
        // std::cout <<  hex << idf.tag << endl;
    }
    // }catch(...){}

    return result;
}

std::pair<std::string, std::string> getTiffCreationTimeAndCameraModel(istream& data, std::string filename) {
    std::string dateTime = "";
    std::string sequenceNumber = "";
    std::string manufacturer = "";
    std::string model = ""; // Camera model

    bool byteOrder = false;

    if (seekAndRead(data, 0) == 'I' && seekAndRead(data, 1) == 'I' && seekAndRead<uint16_t>(data, 2) == 42)
        byteOrder = false;
    else if (seekAndRead(data, 0) == 'M' && seekAndRead(data, 1) == 'M' && seekAndRead<uint16_t>(data, 2, true) == 42)
        byteOrder = true;
    else
        throw runtime_error(filename + " is not a tiff.");

    auto tagPtr = seekAndRead<uint32_t>(data, 4, byteOrder);
    auto idfs = parseTiffHeader(data, tagPtr, byteOrder);
    if (idfs.count(0x132)) {
        dateTime = seekAndRead(data, idfs[0x132].value, idfs[0x132].length);
        while (dateTime.back() == '\0' && dateTime.size() > 0) { dateTime.resize(dateTime.size() - 1); }
    } else {
        throw runtime_error(filename + " tiff does not have a datetime.");
    }
    if (idfs.count(0x8769)) {
        auto exif = parseTiffHeader(data, idfs[0x8769].value, byteOrder);
        if (idfs.count(0x010f)) manufacturer = seekAndRead(data, idfs[0x010f].value, idfs[0x010f].length);
        manufacturer.resize(4);
        if (idfs.count(0x0110))
            model = seekAndRead(data, idfs[0x0110].value, idfs[0x0110].length); // Reading camera model

        if (estd::string_util::toUpper(manufacturer) == "SONY" && exif.count(0x927c)) {
            uint8_t extraOffset = 0;
            if (std::string("SONY DSC", 8) == seekAndRead(data, exif[0x927c].value, 8)) extraOffset = 12;

            auto maker = parseTiffHeader(data, exif[0x927c].value + extraOffset, byteOrder);

            if (maker.count(0xb04a) && maker[0xb04a].dataType == 3 && maker[0xb04a].value != 0) {
                std::stringstream ss;
                ss << std::setw(3) << std::setfill('0') << maker[0xb04a].value;
                sequenceNumber = "bur" + ss.str();
            }
        }
    }

    return {dateTime + sequenceNumber, model};
}

std::string getTiffCreationTime(istream& data, std::string filename) {
    return getTiffCreationTimeAndCameraModel(data, filename).first;
}

std::pair<std::string, std::string> getTiffCreationTimeAndCameraModel(Path p) {
    ifstream data(p.string(), ios::binary);
    return getTiffCreationTimeAndCameraModel(data, p);
}

std::string getTiffCreationTime(Path p) {
    ifstream data(p.string(), ios::binary);
    return getTiffCreationTime(data, p);
}

estd::isubstream jpegToTiff(ifstream& data, Path p) {
    std::string dateTime = "";
    std::string sequenceNumber = "";

    if (seekAndRead(data, 0) != 0xff || seekAndRead(data, 1) != 0xd8)
        throw runtime_error(p.string() + " is not a jpeg.");

    size_t headerOffset = 2;
    uint16_t dataSize = 0;
    while (seekAndRead(data, headerOffset) != 0xff || seekAndRead(data, headerOffset + 1) != 0xE1 ||
           seekAndRead(data, headerOffset + 4, 4) != "Exif") {
        dataSize = seekAndRead<uint16_t>(data, headerOffset + 2);
        headerOffset += 1; //dataSize + 4;
        // std::cout << dataSize << " skip\n";
    }

    if (seekAndRead(data, headerOffset + 4, 6) != std::string("Exif\0\0", 6))
        throw runtime_error(p.string() + " jpeg has invalid exif.");
    estd::isubstream substr(data.rdbuf(), headerOffset + 10, 100000000); //dataSize
    return substr;
}

std::string getJpegCreationTime(Path p) {
    ifstream data(p.string(), ios::binary);
    auto substr = jpegToTiff(data, p);
    return getTiffCreationTime(substr, p);
}

std::pair<std::string, std::string> getJpegCreationAndCameraMode(Path p) {
    ifstream data(p.string(), ios::binary);
    auto substr = jpegToTiff(data, p);
    return getTiffCreationTimeAndCameraModel(substr, p);
}
