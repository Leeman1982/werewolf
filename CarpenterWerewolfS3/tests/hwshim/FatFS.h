#pragma once
#include <Arduino.h>
// Matches the real fs::File/fs::Dir/fs::FS surface (cores/rp2040/FS.h) that
// FatFS is built on, closely enough for -fsyntax-only: same method names,
// same argument/return types, so a real signature mismatch in voicebank.cpp
// or storage.cpp still fails here. Bodies are trivial -- nothing here is
// ever linked or run, see the banner in Arduino.h.
#define FILE_READ  "r"
#define FILE_WRITE "w"
enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };
class FileShim {
public:
    operator bool() const { return true; }
    int read(uint8_t*, size_t) { return 0; }
    size_t write(const uint8_t*, size_t) { return 0; }
    void close() {}
    size_t size() const { return 0; }
    bool seek(uint32_t, SeekMode) { return true; }
    bool seek(uint32_t) { return true; }
    const char* name() const { return ""; }
    FileShim openNextFile() { return FileShim(); }
    bool isDirectory() const { return false; }
};
typedef FileShim File;
class DirShim {
public:
    File   openFile(const char*) { return File(); }
    String fileName() { return String(""); }
    size_t fileSize() { return 0; }
    bool   isDirectory() const { return false; }
    bool   next() { return false; }
    bool   rewind() { return true; }
};
typedef DirShim Dir;
class FatFSShim {
public:
    bool begin() { return true; }
    void end() {}
    bool format() { return true; }
    File open(const char*, const char*) { return File(); }
    bool exists(const char*) { return false; }
    bool remove(const char*) { return true; }
    bool mkdir(const char*) { return true; }
    Dir  openDir(const char*) { return Dir(); }
};
extern FatFSShim FatFS;
