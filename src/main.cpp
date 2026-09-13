#include <iostream>
#include <string>


int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--helper-server") {
            // return guchho::helper_server::RunHelperServer();
            return 0;
        }
    }

    std::cout << "Hello from Guchho!\n";
    return 0;
}