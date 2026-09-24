// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// openfx::CStringView, the C string the string getters return, which compares
// by content where a const char* compares by address.

#include <openfx/ofxCStringView.h>
#include <openfx/ofxLog.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "harness.h"

using openfx::CStringView;

namespace {

const char kText[] = "OfxBitDepthFloat";

// The same characters as kText at an address of their own, so that a compare
// by address would fail.
struct Copy {
  Copy() { std::memcpy(text, kText, sizeof kText); }
  char text[sizeof kText];
};

std::size_t viewLength(std::string_view s) { return s.size(); }

}  // namespace

static_assert(std::is_trivially_copyable_v<CStringView>);
static_assert(sizeof(CStringView) == sizeof(const char*));

TEST_CASE(cstring_view_equals_the_same_text_as_each_operand_type) {
  const Copy copy;
  const CStringView s(copy.text);
  const char* pointer = kText;
  const std::string string(kText);
  const std::string_view view(kText);
  const CStringView other(kText);
  CHECK(s.c_str() != pointer);

  CHECK(s == "OfxBitDepthFloat");
  CHECK("OfxBitDepthFloat" == s);
  CHECK(s == pointer);
  CHECK(pointer == s);
  CHECK(s == string);
  CHECK(string == s);
  CHECK(s == view);
  CHECK(view == s);
  CHECK(s == other);
  CHECK(other == s);

  CHECK(!(s != "OfxBitDepthFloat"));
  CHECK(!("OfxBitDepthFloat" != s));
  CHECK(!(s != pointer));
  CHECK(!(pointer != s));
  CHECK(!(s != string));
  CHECK(!(string != s));
  CHECK(!(s != view));
  CHECK(!(view != s));
  CHECK(!(s != other));
  CHECK(!(other != s));
}

TEST_CASE(cstring_view_differs_from_other_text_as_each_operand_type) {
  const CStringView s("OfxBitDepthByte");
  const char* pointer = kText;
  const std::string string(kText);
  const std::string_view view(kText);
  const CStringView other(kText);

  CHECK(s != "OfxBitDepthFloat");
  CHECK("OfxBitDepthFloat" != s);
  CHECK(s != pointer);
  CHECK(pointer != s);
  CHECK(s != string);
  CHECK(string != s);
  CHECK(s != view);
  CHECK(view != s);
  CHECK(s != other);
  CHECK(other != s);

  CHECK(!(s == pointer));
  CHECK(!(pointer == s));
  CHECK(!(s == string));
  CHECK(!(string == s));
  CHECK(!(s == view));
  CHECK(!(view == s));
  CHECK(!(s == other));
  CHECK(!(other == s));

  // A prefix is not the whole.
  CHECK(CStringView("OfxBitDepth") != pointer);
  CHECK(CStringView("OfxBitDepth") != view);
}

TEST_CASE(cstring_view_never_holds_null) {
  const CStringView fromNull(nullptr);
  CHECK(fromNull.c_str() != nullptr);
  CHECK(fromNull.data() == fromNull.c_str());
  CHECK(fromNull.empty());
  CHECK(fromNull == "");
  const CStringView byDefault;
  CHECK(byDefault.c_str() != nullptr);
  CHECK(byDefault.empty());

  // A null const char* compares as "".
  const char* null = nullptr;
  CHECK(fromNull == null);
  CHECK(CStringView("x") != null);
  CHECK(!CStringView("x").empty());
}

TEST_CASE(cstring_view_streams_its_characters) {
  std::ostringstream stream;
  stream << CStringView(kText) << '|' << CStringView(nullptr) << '|';
  CHECK(stream.str() == "OfxBitDepthFloat||");
  CHECK(openfx::format("depth {}", CStringView(kText)) == "depth OfxBitDepthFloat");
}

TEST_CASE(cstring_view_copies_into_a_std_string) {
  const Copy copy;
  const CStringView s(copy.text);
  const std::string constructed(s);
  const std::string braced{s};
  std::string assigned;
  assigned = s;
  std::string appended = "depth ";
  appended += s;
  CHECK(constructed == kText);
  CHECK(braced == kText);
  CHECK(assigned == kText);
  CHECK(appended == std::string("depth ") + kText);
  CHECK(constructed.data() != s.data());
}

TEST_CASE(cstring_view_converts_to_a_string_view_of_the_same_characters) {
  const CStringView s(kText);
  const std::string_view view = s;
  CHECK(view.data() == s.data());
  CHECK(view.size() == sizeof kText - 1);
  CHECK(viewLength(s) == view.size());
}

TEST_CASE(cstring_view_goes_to_a_c_function_as_its_pointer) {
  const CStringView s(kText);
  const char* pointer = s;
  CHECK(pointer == s.c_str());
  CHECK(std::strlen(s) == sizeof kText - 1);
  CHECK(std::strcmp(s, kText) == 0);
}

TEST_CASE(cstring_view_is_what_auto_deduces) {
  const auto s = CStringView(kText);
  static_assert(std::is_same_v<decltype(s), const CStringView>);
  std::vector<CStringView> list{CStringView(kText)};
  for (auto item : list) {
    static_assert(std::is_same_v<decltype(item), CStringView>);
    CHECK(item == s);
  }
}

TEST_CASE(cstring_view_looks_up_a_map_keyed_by_string_view) {
  const Copy copy;
  const CStringView s(copy.text);
  const std::map<std::string_view, int> bits{{"OfxBitDepthByte", 8}, {kText, 32}};
  CHECK(bits.count(s) == 1);
  CHECK(bits.at(s) == 32);
  CHECK(bits.find(s) != bits.end() && bits.find(s)->second == 32);
  CHECK(bits.find(CStringView("OfxBitDepthHalf")) == bits.end());
  const std::unordered_map<std::string_view, int> hashed(bits.begin(), bits.end());
  CHECK(hashed.at(s) == 32);
}

TEST_CASE(cstring_views_order_by_content_not_address) {
  // "b" sits below "a" in memory, so an order by address would put it first.
  const char text[] = "b\0a";
  const CStringView b(text);
  const CStringView a(text + 2);
  CHECK(a < b);
  CHECK(b > a);
  CHECK(a <= b);
  CHECK(b >= a);
  CHECK(a <= CStringView("a"));
  CHECK(!(b < a));

  std::vector<CStringView> sorted{b, a};
  std::sort(sorted.begin(), sorted.end());
  CHECK(sorted[0] == "a");
  CHECK(sorted[1] == "b");
  const std::set<CStringView> unique{b, a, CStringView("b")};
  CHECK(unique.size() == 2);
}
