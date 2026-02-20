#include "Config.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace
{
std::string toLower(std::string s)
{
    for (char &c : s)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}
} // namespace

bool Config::loadFromFile(const std::string &path, std::string *error)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        if (error)
        {
            *error = "failed to open config file: " + path;
        }
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return loadFromString(buffer.str(), error);
}

bool Config::loadFromString(const std::string &content, std::string *error)
{
    values_.clear();

    std::istringstream stream(content);
    std::string line;
    std::string section;
    size_t lineNo = 0;

    while (std::getline(stream, line))
    {
        lineNo++;
        std::string cleaned = stripComment(line);
        cleaned = trim(cleaned);
        if (cleaned.empty())
        {
            continue;
        }

        if (cleaned.front() == '[' && cleaned.back() == ']')
        {
            section = trim(cleaned.substr(1, cleaned.size() - 2));
            continue;
        }

        size_t eqPos = cleaned.find('=');
        if (eqPos == std::string::npos)
        {
            if (error)
            {
                *error = "invalid config line " + std::to_string(lineNo) + ": missing '='";
            }
            return false;
        }

        std::string key = trim(cleaned.substr(0, eqPos));
        std::string value = trim(cleaned.substr(eqPos + 1));

        if (key.empty())
        {
            if (error)
            {
                *error = "invalid config line " + std::to_string(lineNo) + ": empty key";
            }
            return false;
        }

        if (!section.empty())
        {
            key = section + "." + key;
        }

        values_[key] = value;
    }

    return true;
}

bool Config::has(const std::string &key) const
{
    return values_.find(key) != values_.end();
}

std::string Config::getString(const std::string &key, const std::string &defaultValue) const
{
    auto it = values_.find(key);
    if (it == values_.end())
    {
        return defaultValue;
    }
    return it->second;
}

int Config::getInt(const std::string &key, int defaultValue) const
{
    auto it = values_.find(key);
    if (it == values_.end())
    {
        return defaultValue;
    }

    int value = 0;
    if (!parseInt(it->second, value))
    {
        return defaultValue;
    }
    return value;
}

size_t Config::getSizeT(const std::string &key, size_t defaultValue) const
{
    auto it = values_.find(key);
    if (it == values_.end())
    {
        return defaultValue;
    }

    size_t value = 0;
    if (!parseSizeT(it->second, value))
    {
        return defaultValue;
    }
    return value;
}

bool Config::getBool(const std::string &key, bool defaultValue) const
{
    auto it = values_.find(key);
    if (it == values_.end())
    {
        return defaultValue;
    }

    bool value = false;
    if (!parseBool(it->second, value))
    {
        return defaultValue;
    }
    return value;
}

std::chrono::milliseconds Config::getDurationMs(const std::string &key, std::chrono::milliseconds defaultValue) const
{
    auto it = values_.find(key);
    if (it == values_.end())
    {
        return defaultValue;
    }

    std::chrono::milliseconds value(0);
    if (!parseDurationMs(it->second, value))
    {
        return defaultValue;
    }
    return value;
}

std::string Config::trim(const std::string &s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
    {
        start++;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
    {
        end--;
    }
    return s.substr(start, end - start);
}

std::string Config::stripComment(const std::string &line)
{
    size_t posHash = line.find('#');
    size_t posSemicolon = line.find(';');
    size_t pos = std::min(posHash, posSemicolon);
    if (pos == std::string::npos)
    {
        pos = (posHash == std::string::npos) ? posSemicolon : posHash;
    }
    if (pos == std::string::npos)
    {
        return line;
    }
    return line.substr(0, pos);
}

bool Config::parseBool(const std::string &s, bool &value)
{
    std::string lower = toLower(trim(s));
    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")
    {
        value = true;
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "no" || lower == "off")
    {
        value = false;
        return true;
    }
    return false;
}

bool Config::parseInt(const std::string &s, int &value)
{
    try
    {
        size_t idx = 0;
        int v = std::stoi(trim(s), &idx);
        if (idx != trim(s).size())
        {
            return false;
        }
        value = v;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool Config::parseSizeT(const std::string &s, size_t &value)
{
    try
    {
        size_t idx = 0;
        unsigned long long v = std::stoull(trim(s), &idx);
        if (idx != trim(s).size())
        {
            return false;
        }
        value = static_cast<size_t>(v);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool Config::parseDurationMs(const std::string &s, std::chrono::milliseconds &value)
{
    std::string raw = trim(s);
    if (raw.empty())
    {
        return false;
    }

    std::string lower = toLower(raw);
    size_t multiplier = 1;

    if (lower.size() >= 2 && lower.substr(lower.size() - 2) == "ms")
    {
        multiplier = 1;
        lower = lower.substr(0, lower.size() - 2);
    }
    else if (!lower.empty() && lower.back() == 's')
    {
        multiplier = 1000;
        lower = lower.substr(0, lower.size() - 1);
    }

    size_t base = 0;
    if (!parseSizeT(lower, base))
    {
        return false;
    }

    value = std::chrono::milliseconds(base * multiplier);
    return true;
}
