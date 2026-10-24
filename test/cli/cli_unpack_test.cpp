#include "guchho/cli.hpp"
#include "guchho_test.hpp"

#include <cstdlib>
#include <filesystem>

using namespace guchho::cli;

TEST(CLIUnpackHelp)
{
    std::vector<std::string> args = {"guchho", "unpack", "--help"};
    int code = runUnpack(args);
    ASSERT_EQ(code, 0);
}
