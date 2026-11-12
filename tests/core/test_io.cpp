#include "test_framework.hpp"

#include <firmir/datastream.hpp>
#include <firmir/dir.hpp>
#include <firmir/file.hpp>
#include <firmir/fileinfo.hpp>
#include <firmir/textstream.hpp>

#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#  include <process.h>
#else
#  include <unistd.h>
#endif

using namespace Firmir;

namespace {

int currentPid() {
#ifdef _WIN32
    return ::_getpid();
#else
    return ::getpid();
#endif
}

/// RAII temp directory for IO tests.
struct TempDir {
    TempDir() {
        path = String(
            (std::filesystem::temp_directory_path() /
             ("firmir-test-" + std::to_string(currentPid())))
                .string());
        Dir().mkpath(path);
    }
    ~TempDir() { Dir(path).removeRecursively(); }
    String path;
    String file(const char* name) const { return Dir(path).filePath(name); }
};

} // namespace

TEST_CASE(file_write_and_read) {
    TempDir tmp;
    const String name = tmp.file("data.bin");
    CHECK(!File::exists(name));

    {
        File f(name);
        CHECK(f.open(File::WriteOnly));
        CHECK(f.isOpen());
        CHECK_EQ(f.write("hello"), 5);
        CHECK_EQ(f.write(ByteArray("\x00\x01", 2)), 2);
        f.close();
        CHECK(!f.isOpen());
    }
    CHECK(File::exists(name));

    File f(name);
    CHECK(f.open(File::ReadOnly));
    CHECK_EQ(f.size(), 7);
    CHECK_EQ(f.pos(), 0);
    CHECK_EQ(f.read(5), "hello");
    CHECK_EQ(f.pos(), 5);
    CHECK(!f.atEnd());
    CHECK_EQ(f.readAll(), ByteArray("\x00\x01", 2));
    CHECK(f.atEnd());

    CHECK(f.seek(0));
    CHECK_EQ(f.readAll(), ByteArray("hello\x00\x01", 7));
    f.close();

    // Append mode keeps existing content.
    {
        File g(name);
        CHECK(g.open(File::WriteOnly | File::Append));
        CHECK_EQ(g.write("!"), 1);
    }
    CHECK_EQ(File(name).open(File::ReadOnly), true);
    File reader(name);
    CHECK(reader.open(File::ReadOnly));
    CHECK_EQ(reader.size(), 8);
    reader.close();

    CHECK(File::remove(name));
    CHECK(!File::exists(name));

    File missing(tmp.file("nope"));
    CHECK(!missing.open(File::ReadOnly));
    CHECK(!missing.errorString().isEmpty());
}

TEST_CASE(fileinfo_queries) {
    TempDir tmp;
    const String name = tmp.file("archive.tar.gz");
    {
        File f(name);
        CHECK(f.open(File::WriteOnly));
        f.write("1234567");
    }

    const FileInfo info(name);
    CHECK(info.exists());
    CHECK(info.isFile());
    CHECK(!info.isDir());
    CHECK_EQ(info.fileName(), "archive.tar.gz");
    CHECK_EQ(info.baseName(), "archive");
    CHECK_EQ(info.suffix(), "gz");
    CHECK_EQ(info.size(), 7);
    // Absolute path: "/..." on POSIX, "C:\..." or "C:/..." on Windows.
    CHECK(std::filesystem::path(info.absoluteFilePath().toStdString())
              .is_absolute());
    CHECK(info.absoluteFilePath().endsWith("archive.tar.gz"));
    CHECK(info.lastModified().time_since_epoch().count() > 0);

    const FileInfo dirInfo(tmp.path);
    CHECK(dirInfo.isDir());

    const FileInfo missing(tmp.file("missing"));
    CHECK(!missing.exists());
}

TEST_CASE(dir_operations) {
    TempDir tmp;
    Dir dir(tmp.path);
    CHECK(dir.exists());

    CHECK(dir.mkpath("sub/deep"));
    CHECK(dir.cd("sub"));
    CHECK(dir.cd("deep"));
    CHECK(dir.cdUp());
    CHECK(dir.cdUp());

    {
        File f(dir.filePath("b.txt"));
        CHECK(f.open(File::WriteOnly));
        f.write("b");
    }
    {
        File f(dir.filePath("a.txt"));
        CHECK(f.open(File::WriteOnly));
        f.write("a");
    }

    const auto entries = dir.entryList(); // sorted
    CHECK_EQ(entries.size(), 3u);
    CHECK_EQ(entries[0], "a.txt");
    CHECK_EQ(entries[1], "b.txt");
    CHECK_EQ(entries[2], "sub");

    const auto infos = dir.entryInfoList();
    CHECK_EQ(infos.size(), 3u);

    CHECK(dir.rmdir("sub/deep"));
    CHECK(dir.rmdir("sub"));

    CHECK(!Dir(dir.filePath("missing-sub")).exists());

    // Static helpers.
    CHECK(!Dir::currentPath().isEmpty());
    CHECK(Dir::current().exists());
    CHECK(!Dir::homePath().isEmpty());
    CHECK_EQ(Dir::separator(), '/');
}

TEST_CASE(data_stream_roundtrip) {
    ByteArray buffer;
    {
        DataStream out(&buffer);
        out << std::int32_t(-42);
        out << std::uint32_t(4000000000u);
        out << std::int64_t(-9000000000000LL);
        out << 3.25;
        out << true;
        out << String("hello");
        out << ByteArray("\x00\x01", 2);
        CHECK(out.status() == DataStream::Status::Ok);
    }
    // Big-endian wire format: -42 as 0xFFFFFFD6.
    CHECK_EQ(buffer.left(4), ByteArray("\xff\xff\xff\xd6", 4));

    DataStream in(buffer);
    std::int32_t i32 = 0;
    std::uint32_t u32 = 0;
    std::int64_t i64 = 0;
    double dbl = 0;
    bool flag = false;
    String str;
    ByteArray bytes;
    in >> i32 >> u32 >> i64 >> dbl >> flag >> str >> bytes;
    CHECK(in.status() == DataStream::Status::Ok);
    CHECK_EQ(i32, -42);
    CHECK_EQ(u32, 4000000000u);
    CHECK_EQ(i64, -9000000000000LL);
    CHECK_EQ(dbl, 3.25);
    CHECK(flag);
    CHECK_EQ(str, "hello");
    CHECK_EQ(bytes, ByteArray("\x00\x01", 2));

    // Reading past the end flips the status.
    std::int32_t extra = 0;
    in >> extra;
    CHECK(in.status() == DataStream::Status::ReadPastEnd);
    in.resetStatus();
    CHECK(in.status() == DataStream::Status::Ok);
}

TEST_CASE(data_stream_little_endian) {
    ByteArray buffer;
    DataStream out(&buffer);
    out.setByteOrder(DataStream::ByteOrder::LittleEndian);
    out << std::uint32_t(0x01020304u);
    CHECK_EQ(buffer, ByteArray("\x04\x03\x02\x01", 4));

    DataStream in(buffer);
    in.setByteOrder(DataStream::ByteOrder::LittleEndian);
    std::uint32_t value = 0;
    in >> value;
    CHECK_EQ(value, 0x01020304u);
}

TEST_CASE(text_stream_string) {
    String storage;
    {
        TextStream out(&storage);
        out << "count: " << 42 << " ratio: " << 0.5 << '\n';
        out << "second line";
    }
    CHECK_EQ(storage, "count: 42 ratio: 0.5\nsecond line");

    TextStream in(&storage);
    String word;
    in >> word;
    CHECK_EQ(word, "count:");
    in >> word;
    CHECK_EQ(word, "42");

    TextStream lines(&storage);
    CHECK_EQ(lines.readLine(), "count: 42 ratio: 0.5");
    CHECK(!lines.atEnd());
    CHECK_EQ(lines.readLine(), "second line");
    CHECK(lines.atEnd());
    CHECK_EQ(lines.readLine(), "");
}

TEST_CASE(text_stream_file) {
    TempDir tmp;
    const String name = tmp.file("text.txt");
    {
        File f(name);
        CHECK(f.open(File::WriteOnly));
        TextStream out(&f);
        out << "alpha beta\n" << "gamma\n";
    } // TextStream dtor flushes, File dtor closes

    File f(name);
    CHECK(f.open(File::ReadOnly));
    TextStream in(&f);
    CHECK_EQ(in.readLine(), "alpha beta");
    String word;
    in >> word;
    CHECK_EQ(word, "gamma");
    CHECK(!in.atEnd()); // the trailing newline is still unread data
    CHECK_EQ(in.readAll(), "\n");
    CHECK(in.atEnd());
}

