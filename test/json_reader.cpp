#include "json_reader.h"
#include <json/json.h>
#include <fstream>
#include <filesystem>

std::filesystem::path RelativePath(const std::string &path)
{
    auto program_dir = std::filesystem::path(__FILE__).remove_filename(),
            executable_path = std::filesystem::current_path(),
            relative_dir = std::filesystem::proximate(program_dir, executable_path);

    return (relative_dir / path);
}

Json::Value JsonFromFile(const std::string &file_path)
{
    Json::Value root;
    Json::Reader reader;

    std::filesystem::path relative_path = RelativePath(file_path);
    std::ifstream file(relative_path, std::ifstream::binary);
    if(! file)
        throw std::runtime_error("Opening file failed");

    bool parsingSuccessful = reader.parse(file, root, false);

    if(!parsingSuccessful)
        throw std::runtime_error("Parsing failed.");

    file.close();
    return root;
}