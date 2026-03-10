#include <fstream>
#include <iostream>

#include <http_server/uri_map_singleton.h>

UriMapSingleton& UriMapSingleton::getInstance()
{
    static UriMapSingleton instance;
    return instance;
}

void UriMapSingleton::saveData(const std::string& uri,
                               const std::string& data,
                               const std::string& content_type)
{
    data_[uri] = std::make_pair(data, content_type);
}

bool UriMapSingleton::deleteData(const std::string& uri)
{
    if (data_.find(uri) != data_.end()) {
        data_.erase(uri);
        return true;
    }
    return false;
}

std::optional<std::pair<std::string, std::string>> UriMapSingleton::getData(const std::string& uri) const
{
    if (const auto it = data_.find(uri); it != data_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void UriMapSingleton::serialize(const std::string& filename) const
{
    std::ofstream file(filename, std::ios::trunc);
    if (file.is_open()) {
        for (const auto& [uri, content_type_data] : data_) {
            const auto& [content_type, data] = content_type_data;

            // Remove trailing whitespaces and newline characters
            std::string cleaned_data = data;
            size_t last_char = cleaned_data.find_last_not_of(" \n\r\t");
            if (last_char != std::string::npos) {
                cleaned_data.erase(last_char + 1);
            }

            file << "URI: " << uri << "\n"
                 << "Content-Type: " << content_type << "\n"
                 << "Data: " << cleaned_data << "\n"
                 << "RecordEnd\n"; // Use a unique marker for the end of each record
        }
        std::cout << "Data serialized to " << filename << '\n';
    }
    else {
        std::cerr << "Error opening file for serialization: " << filename << '\n';
    }
}

void UriMapSingleton::deserialize(const std::string& filename)
{
    if (std::ifstream file(filename); file.is_open()) {
        data_.clear(); // Clear existing data before loading from file

        std::string line;
        std::string uri, content_type, data;
        while (std::getline(file, line)) {
            if (line.compare(0, 4, "URI:") == 0) {
                uri = line.substr(4);
            }
            else if (line.compare(0, 13, "Content-Type:") == 0) {
                content_type = line.substr(13);
            }
            else if (line.compare(0, 5, "Data:") == 0) {
                data = line.substr(5);
            }
            else if (line == "RecordEnd") {
                // Check if all necessary components are present
                if (!uri.empty() && !content_type.empty()) {
                    data_[uri] = std::make_pair(content_type, data);
                }
                else {
                    std::cerr << "Error in deserialization: Incomplete record\n";
                    std::exit(EXIT_FAILURE);
                }
                // Reset variables for the next record
                uri.clear();
                content_type.clear();
                data.clear();
            }
        }
        std::cout << "Data deserialized from " << filename << '\n';
    }
    else {
        std::cerr << "Error opening file for deserialization: " << filename << '\n';
    }
}
