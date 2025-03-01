#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <queue>
#include <unordered_map>
#include <filesystem>

void get_folder_id_by_path(const std::string& path) {
    std::istringstream ss(path);
    std::string folder{};
    std::string parent_id = "root";
    while (std::getline(ss, folder, '/')) {
        std::cout << "beb" << '\n';
    }
}





int main() {
    std::filesystem::path path("/asdf/asdfa/sadf/sdf");
    std::cout << path.filename() << "  sdfsdf";
    int y;

    return 0;
}