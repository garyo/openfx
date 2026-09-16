// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxCore.h>
#include <ofxProperty.h>

#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace testhost {

// Storage for one OFX property set, plus the host's OfxPropertySuiteV1.
//
// A set is normally created from the generated metadata (openfx::prop_sets or
// openfx::action_props), which pre-defines every property the spec lists with
// its type and dimension. Properties a plugin sets that were not pre-defined
// are created on the fly. A set may have a parent: reads of properties absent
// locally fall through to it, which is how an instance sees its descriptor.
class PropertySet {
 public:
  enum class Type { Int, Double, String, Pointer };

  struct Property {
    Type type;
    int dimension;  // 0 means variable
    std::variant<std::vector<int>, std::vector<double>, std::vector<std::string>, std::vector<void*>> values;
  };

  PropertySet() = default;
  // Pre-define the properties of the named set from openfx::prop_sets. With a
  // parent, properties present in both are seeded from the parent's values.
  explicit PropertySet(std::string_view setName, const PropertySet* parent = nullptr);
  // Pre-define the properties of an action's inArgs or outArgs set.
  static PropertySet forAction(std::string_view action, std::string_view which);

  PropertySet(const PropertySet&) = default;
  PropertySet& operator=(const PropertySet&) = default;

  OfxPropertySetHandle handle() { return reinterpret_cast<OfxPropertySetHandle>(this); }
  static PropertySet* from(OfxPropertySetHandle h) { return reinterpret_cast<PropertySet*>(h); }

  void define(std::string_view name, Type type, int dimension);
  bool has(std::string_view name) const;
  const Property* find(std::string_view name) const;  // searches parents too

  template <typename T>
  OfxStatus set(std::string_view name, int index, T value);
  template <typename T>
  OfxStatus get(std::string_view name, int index, T* out) const;
  OfxStatus dimension(std::string_view name, int* out) const;
  OfxStatus reset(std::string_view name);

  // Convenience for host code: value or default, never an error.
  int getInt(std::string_view name, int index = 0, int fallback = 0) const;
  double getDouble(std::string_view name, int index = 0, double fallback = 0) const;
  std::string getString(std::string_view name, int index = 0, std::string_view fallback = "") const;
  std::vector<std::string> getStrings(std::string_view name) const;

  // One line per property, for diagnostics.
  std::string dump(std::string_view indent = "") const;

  static const OfxPropertySuiteV1* suite();

 private:
  static Type typeFor(std::string_view propName, bool* single);
  Property& create(std::string_view name, Type type, int dimension);

  std::map<std::string, Property, std::less<>> props_;
  const PropertySet* parent_ = nullptr;
};

}  // namespace testhost
