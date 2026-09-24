// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstring>
#include <ostream>
#include <string>
#include <string_view>

namespace openfx {

// A null-terminated C string that compares by content: what the string and
// enum getters return. The const char* the property suite hands out compares
// by address, so `pixelDepth() == kOfxBitDepthFloat` would be false for the
// same text in two places; with CStringView it is true.
//
// It converts implicitly to const char* and to std::string_view, so it goes
// to a C call or a string_view parameter as it is, and std::string(s) copies
// it. c_str() is the plain pointer, for printf and other varargs, which take
// no class type. It never holds null: a null from the host reads as "".
//
// It owns nothing. The characters belong to whoever handed them out, the host
// for a property value, and last only as long as they keep them: until the
// property is next written, or its set goes. Copy it into a std::string to
// keep it longer.
class CStringView {
 public:
  constexpr CStringView() noexcept = default;
  constexpr CStringView(const char* str) noexcept : str_(str ? str : "") {}

  constexpr operator const char*() const noexcept { return str_; }
  constexpr operator std::string_view() const noexcept { return str_; }

  constexpr const char* c_str() const noexcept { return str_; }
  constexpr const char* data() const noexcept { return str_; }
  constexpr bool empty() const noexcept { return *str_ == '\0'; }

  // == and != compare the characters. A null const char* compares as "".
  friend bool operator==(CStringView a, CStringView b) noexcept {
    return std::strcmp(a.str_, b.str_) == 0;
  }
  friend bool operator==(CStringView a, const char* b) noexcept {
    return a == CStringView(b);
  }
  friend bool operator==(const char* a, CStringView b) noexcept {
    return CStringView(a) == b;
  }
  friend bool operator==(CStringView a, std::string_view b) noexcept {
    return std::string_view(a.str_) == b;
  }
  friend bool operator==(std::string_view a, CStringView b) noexcept {
    return a == std::string_view(b.str_);
  }
  friend bool operator==(CStringView a, const std::string& b) noexcept {
    return std::string_view(a.str_) == b;
  }
  friend bool operator==(const std::string& a, CStringView b) noexcept {
    return a == std::string_view(b.str_);
  }
  friend bool operator!=(CStringView a, CStringView b) noexcept { return !(a == b); }
  friend bool operator!=(CStringView a, const char* b) noexcept { return !(a == b); }
  friend bool operator!=(const char* a, CStringView b) noexcept { return !(a == b); }
  friend bool operator!=(CStringView a, std::string_view b) noexcept { return !(a == b); }
  friend bool operator!=(std::string_view a, CStringView b) noexcept { return !(a == b); }
  friend bool operator!=(CStringView a, const std::string& b) noexcept {
    return !(a == b);
  }
  friend bool operator!=(const std::string& a, CStringView b) noexcept {
    return !(a == b);
  }

  // Two of them order by content too, so a sort or a std::set of them does;
  // left alone, the conversion would have them order by address.
  friend bool operator<(CStringView a, CStringView b) noexcept {
    return std::strcmp(a.str_, b.str_) < 0;
  }
  friend bool operator>(CStringView a, CStringView b) noexcept { return b < a; }
  friend bool operator<=(CStringView a, CStringView b) noexcept { return !(b < a); }
  friend bool operator>=(CStringView a, CStringView b) noexcept { return !(a < b); }

  // The characters, as for a const char*; openfx::format and the Logger use it,
  // so either takes a CStringView as an argument.
  friend std::ostream& operator<<(std::ostream& os, CStringView s) {
    return os << s.str_;
  }

 private:
  const char* str_ = "";
};

}  // namespace openfx
