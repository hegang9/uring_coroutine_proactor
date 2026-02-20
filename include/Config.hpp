#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

class Config
{
  public:
    bool loadFromFile(const std::string &path, std::string *error = nullptr);
    bool loadFromString(const std::string &content, std::string *error = nullptr);

    bool has(const std::string &key) const;

    std::string getString(const std::string &key, const std::string &defaultValue = "") const;
    int getInt(const std::string &key, int defaultValue = 0) const;
    size_t getSizeT(const std::string &key, size_t defaultValue = 0) const;
    bool getBool(const std::string &key, bool defaultValue = false) const;
    std::chrono::milliseconds getDurationMs(const std::string &key, std::chrono::milliseconds defaultValue) const;

    const std::unordered_map<std::string, std::string> &all() const
    {
        return values_;
    }

  private:
    static std::string trim(const std::string &s);
    static std::string stripComment(const std::string &line);
    static bool parseBool(const std::string &s, bool &value);
    static bool parseInt(const std::string &s, int &value);
    static bool parseSizeT(const std::string &s, size_t &value);
    static bool parseDurationMs(const std::string &s, std::chrono::milliseconds &value);

    std::unordered_map<std::string, std::string> values_;
};
