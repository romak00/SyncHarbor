#include "sync_handler.hpp"




int main() {
    auto start = std::chrono::high_resolution_clock::now();

    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::filesystem::path dir("/home/rk00/demo");
    bool IS_INTITIAL = 1;
    SyncHandler sh("conf1", dir, IS_INTITIAL);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Время выполнения: " << (double)duration/1000 << " с" << std::endl;


    return 0;
}
