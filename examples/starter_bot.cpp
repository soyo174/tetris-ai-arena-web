#include <iostream>
#include <regex>
#include <string>

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    const std::regex request_id(R"ID("request_id"\s*:\s*"([0-9]+)")ID");
    std::string line;
    while (std::getline(std::cin, line)) {
        std::smatch match;
        if (!std::regex_search(line, match, request_id)) continue;
        std::cout << "{\"type\":\"PLAY\",\"request_id\":\"" << match[1]
                  << "\",\"commands\":[\"HARD_DROP\"]}" << std::endl;
    }
}
