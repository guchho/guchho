#include "guchho/cli.hpp"
#include "test/guchho_test.hpp"

#include <cstdlib>
#include <filesystem>

using namespace guchho::cli;

TEST(CLIUnpack, Help)
{
    std::vector<std::string> args = {"guchho", "unpack", "--help"};
    int code = runUnpack(args);
    ASSERT_EQ(code, 0);
}
