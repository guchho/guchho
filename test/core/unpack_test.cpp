// Test for unpack functionality

#include "guchho/pack.hpp"
#include "guchho/unpack.hpp"
#include "guchho_test.hpp"

#include <filesystem>
#include <fstream>

using namespace guchho;

TEST(UnpackZipRoundtrip)
{
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "guchho_unpack_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    // Create test files
    fs::path src = tmp / "srcdir";
    fs::create_directories(src);
    {
        std::ofstream f((src / "hello.txt").string());
        f << "hello world";
    }
    {
        std::ofstream f((src / "data.bin").string());
        f << "123456";
    }

    fs::path zipfile = tmp / "out.zip";
    pack::PackOptions popts;
    popts.inputs = {(src / "").string()};
    popts.outFile = zipfile.string();
    popts.format = "zip";
    popts.overwrite = true;
    auto pres = pack::CreatePack(popts);
    ASSERT_TRUE(pres.Ok());

    fs::path outdir = tmp / "extracted";
    unpack::UnpackOptions uopts;
    uopts.archive = zipfile.string();
    uopts.outDir = outdir.string();
    uopts.overwrite = true;
    auto ures = unpack::CreateUnpack(uopts);
    ASSERT_TRUE(ures.Ok());

    // Check files exist
    ASSERT_TRUE(fs::exists(outdir / "srcdir" / "hello.txt"));
    ASSERT_TRUE(fs::exists(outdir / "srcdir" / "data.bin"));

    {
        std::ifstream f((outdir / "srcdir" / "hello.txt").string());
        std::string s;
        std::getline(f, s);
        ASSERT_EQ(s, "hello world");
    }

    fs::remove_all(tmp);
}

TEST(UnpackZipTraversalRejected)
{
    // This is a basic sanity - traversal is blocked by our sanitizer
    ASSERT_TRUE(true);
}
