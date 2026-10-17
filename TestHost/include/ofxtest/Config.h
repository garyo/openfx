// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

// The configuration file ofxtesthost and the test plugins share. It is a
// subset of TOML: [sections] of key = value lines, each value a string,
// integer, float or boolean, with # comments. The host reads [host], the test
// plugins [plugin], and each ignores the other's section. A plugin finds the
// file through the OPENFX_TEST_CONFIG environment variable, which
// ofxtesthost --config sets for the plugins it loads.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "openfx/ofxLog.h"

namespace ofxtest {

inline constexpr const char* kConfigEnv = "OPENFX_TEST_CONFIG";

// "debug", "info", "warning", "error" or "off".
inline std::optional<openfx::Logger::Level> logLevelFromName(std::string_view name) {
  using Level = openfx::Logger::Level;
  static constexpr std::pair<std::string_view, Level> kLevels[] = {
      {"debug", Level::Debug},
      {"info", Level::Info},
      {"warning", Level::Warning},
      {"error", Level::Error},
      {"off", Level::Off}};
  for (const auto& [n, level] : kLevels)
    if (n == name)
      return level;
  return std::nullopt;
}

class Config {
 public:
  using Value = std::variant<std::string, long long, double, bool>;

  // Throws std::runtime_error naming the source and line of the first error.
  static Config parse(std::string_view text, std::string source = "config") {
    Config config;
    config.source_ = std::move(source);
    std::string section;
    int lineNumber = 0;
    std::istringstream lines{std::string(text)};
    for (std::string line; std::getline(lines, line);) {
      ++lineNumber;
      Parser p{line, config.source_ + ":" + std::to_string(lineNumber)};
      p.skipSpace();
      if (p.atEnd())
        continue;
      if (p.peek() == '[') {
        section = p.sectionHeader();
        config.sections_[section];
        continue;
      }
      std::string key = p.key();
      p.skipSpace();
      p.expect('=');
      p.skipSpace();
      Value value = p.value();
      p.skipSpace();
      if (!p.atEnd())
        p.fail("unexpected text after the value");
      if (!config.sections_[section].emplace(key, std::move(value)).second)
        p.fail("duplicate key " + key);
    }
    return config;
  }

  static Config load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in)
      throw std::runtime_error("cannot read config file " + path.string());
    std::ostringstream text;
    text << in.rdbuf();
    return parse(text.str(), path.string());
  }

  // The file OPENFX_TEST_CONFIG gives, if it is set.
  static std::optional<Config> fromEnvironment() {
    const char* path = std::getenv(kConfigEnv);
    if (!path || !*path)
      return std::nullopt;
    return load(path);
  }

  const std::string& source() const { return source_; }

  const Value* find(std::string_view section, std::string_view key) const {
    auto s = sections_.find(section);
    if (s == sections_.end())
      return nullptr;
    auto v = s->second.find(key);
    return v == s->second.end() ? nullptr : &v->second;
  }

  // A value of one type, or nothing if the key is absent. A value of another
  // type throws.
  template <typename T>
  std::optional<T> get(std::string_view section, std::string_view key) const {
    const Value* v = find(section, key);
    if (!v)
      return std::nullopt;
    if (const T* typed = std::get_if<T>(v))
      return *typed;
    throw std::runtime_error(source_ + ": [" + std::string(section) + "] " +
                             std::string(key) + " has the wrong type");
  }

  // The section's log setting, if it has one.
  std::optional<openfx::Logger::Level> logLevel(std::string_view section) const {
    auto name = get<std::string>(section, "log");
    if (!name)
      return std::nullopt;
    if (auto level = logLevelFromName(*name))
      return level;
    throw std::runtime_error(source_ + ": [" + std::string(section) +
                             "] log must be debug, info, warning, error or off");
  }

  // Warns about each key in the section that is not one of these, which is
  // most likely a misspelling.
  void warnUnknownKeys(std::string_view section,
                       std::initializer_list<std::string_view> known) const {
    auto s = sections_.find(section);
    if (s == sections_.end())
      return;
    for (const auto& [key, value] : s->second)
      if (std::find(known.begin(), known.end(), key) == known.end())
        openfx::Logger::warn("{}: [{}] has no setting {}", source_, section, key);
  }

 private:
  // One line, consumed left to right.
  struct Parser {
    std::string_view line;
    std::string where;
    size_t pos = 0;

    [[noreturn]] void fail(const std::string& what) const {
      throw std::runtime_error(where + ": " + what);
    }
    bool atEnd() const { return pos >= line.size() || line[pos] == '#'; }
    char peek() const { return pos < line.size() ? line[pos] : '\0'; }
    void skipSpace() {
      while (pos < line.size() &&
             (line[pos] == ' ' || line[pos] == '\t' || line[pos] == '\r'))
        ++pos;
    }
    void expect(char c) {
      if (peek() != c)
        fail(std::string("expected '") + c + "'");
      ++pos;
    }
    static bool bare(char c) {
      return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    }

    std::string sectionHeader() {
      expect('[');
      skipSpace();
      std::string name;
      while (pos < line.size() && (bare(line[pos]) || line[pos] == '.'))
        name += line[pos++];
      skipSpace();
      expect(']');
      skipSpace();
      if (name.empty() || !atEnd())
        fail("expected [section]");
      return name;
    }

    std::string key() {
      std::string k;
      while (pos < line.size() && bare(line[pos])) k += line[pos++];
      if (k.empty())
        fail("expected a key");
      return k;
    }

    Value value() {
      const char c = peek();
      if (c == '"')
        return basicString();
      if (c == '\'')
        return literalString();
      size_t end = pos;
      while (end < line.size() && line[end] != '#' && line[end] != ' ' &&
             line[end] != '\t' && line[end] != '\r')
        ++end;
      std::string token(line.substr(pos, end - pos));
      pos = end;
      if (token == "true")
        return true;
      if (token == "false")
        return false;
      std::string digits;
      for (char d : token)
        if (d != '_')
          digits += d;
      char* stop = nullptr;
      if (digits.find_first_of(".eE") == std::string::npos && !digits.empty()) {
        const long long i = std::strtoll(digits.c_str(), &stop, 10);
        if (*stop == '\0')
          return i;
      } else if (!digits.empty()) {
        const double d = std::strtod(digits.c_str(), &stop);
        if (*stop == '\0')
          return d;
      }
      fail("expected a string, number, true or false, not '" + token + "'");
    }

    std::string basicString() {
      expect('"');
      std::string s;
      while (pos < line.size() && line[pos] != '"') {
        char c = line[pos++];
        if (c == '\\') {
          if (pos >= line.size())
            break;
          switch (char e = line[pos++]) {
            case 'n':
              c = '\n';
              break;
            case 't':
              c = '\t';
              break;
            case '"':
            case '\\':
              c = e;
              break;
            default:
              fail(std::string("unsupported escape \\") + e);
          }
        }
        s += c;
      }
      expect('"');
      return s;
    }

    std::string literalString() {
      expect('\'');
      const size_t close = line.find('\'', pos);
      if (close == std::string_view::npos)
        fail("unterminated string");
      std::string s(line.substr(pos, close - pos));
      pos = close + 1;
      return s;
    }
  };

  std::map<std::string, std::map<std::string, Value, std::less<>>, std::less<>> sections_;
  std::string source_;
};

}  // namespace ofxtest
