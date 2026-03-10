#pragma once

#include <string>
#include <unordered_map>
#include <optional>

/**
 * @class UriMapSingleton
 * @brief A singleton class for managing URI data
 *
 * This class provides a singleton instance for managing URI data. It allows saving, deleting,
 * and retrieving data associated with specific URIs. The data is stored in memory and can be
 * serialized to and deserialized from a file. Serialization is expected on server shutdown.
 */
class UriMapSingleton
{
public:
    /**
     * @brief Get the singleton instance of UriMapSingleton
     * @return Reference to the singleton instance
     */
    static UriMapSingleton& getInstance();

    /**
     * @brief Save data associated with a URI
     * @param uri The URI to associate the data with
     * @param data The data to be saved
     * @param content_type The content type of the data
     *
     * We save data in order to be able to retrieve it for GET requests.
     * We also save the payload of POST requests.
     */
    void saveData(const std::string& uri, const std::string& data, const std::string& content_type);

    /**
     * @brief Delete data associated with a URI
     * @param uri The URI to delete data for
     * @return True if data was deleted, false if the URI was not found
     */
    bool deleteData(const std::string& uri);

    /**
     * @brief Get data associated with a URI
     * @param uri The URI to retrieve data for
     * @return An optional containing the data and content type if the URI is found, or an empty optional
     */
    [[nodiscard]]
    std::optional<std::pair<std::string, std::string>> getData(const std::string& uri) const;

    /**
     * @brief Serialize the data to a file
     * @param filename The name of the file to serialize to
     *
     * File is erased before serialization to ensure repair
     */
    void serialize(const std::string& filename) const;

    /**
     * @brief Deserialize data from a file
     * @param filename The name of the file to deserialize from
     */
    void deserialize(const std::string& filename);

private:

    /**
     * @brief Private constructor to enforce singleton pattern.
     */
    UriMapSingleton() = default;

    /**< The hash map of URI data ensures search in O(1) time */
    std::unordered_map<std::string, std::pair<std::string, std::string>> data_;
};
