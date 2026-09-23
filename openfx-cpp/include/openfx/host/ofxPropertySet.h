// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxCore.h>
#include <ofxProperty.h>

#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxLog.h"
#include "openfx/ofxPropsBySet.h"
#include "openfx/ofxPropsMetadata.h"

namespace openfx::host {

// Storage for one OFX property set, plus the host's OfxPropertySuiteV1.
//
// A set is normally created from the generated metadata (openfx::prop_sets or
// openfx::action_props), which pre-defines every property the spec lists with
// its type, dimension and, where the spec states one, its default value.
// Properties a plugin sets that were not pre-defined
// are created on the fly. A set may have a parent: reads of properties absent
// locally fall through to it, which is how an instance sees its descriptor.
class PropertySet {
 public:
  enum class Type { Int, Double, String, Pointer };

  struct Property {
    Type type;
    int dimension;  // 0 means variable
    std::variant<std::vector<int>, std::vector<double>, std::vector<std::string>,
                 std::vector<void*>>
        values;
  };

  PropertySet() = default;
  // Pre-define the properties of the named set from openfx::prop_sets. With a
  // parent, properties present in both are seeded from the parent's values.
  explicit PropertySet(std::string_view setName, const PropertySet* parent = nullptr)
      : parent_(parent) {
    auto it = prop_sets.find(setName);
    if (it == prop_sets.end())
      throw std::runtime_error("unknown property set " + std::string(setName));
    for (const auto& prop : it->second) {
      // Multi-typed properties (e.g. OfxParamPropDefault) take the type of the first
      // write.
      if (prop.def.supportedTypes.size() != 1)
        continue;
      auto& p =
          create(prop.name, storageType(prop.def.supportedTypes[0]), prop.def.dimension);
      seedDefault(p, prop.def.defaults);
      if (parent) {
        if (const auto* inherited = parent->find(prop.name);
            inherited && inherited->type == p.type)
          p.values = inherited->values;
      }
    }
  }
  // Pre-define the properties of an action's inArgs or outArgs set.
  static PropertySet forAction(std::string_view action, std::string_view which) {
    PropertySet set;
    auto it = action_props.find(std::array<std::string_view, 2>{action, which});
    if (it == action_props.end())
      return set;
    for (const char* name : it->second) {
      if (const auto* def = find_prop_def(name); def && def->supportedTypes.size() == 1)
        seedDefault(set.create(name, storageType(def->supportedTypes[0]), def->dimension),
                    def->defaults);
    }
    return set;
  }

  PropertySet(const PropertySet&) = default;
  PropertySet& operator=(const PropertySet&) = default;
  PropertySet(PropertySet&&) = default;
  PropertySet& operator=(PropertySet&&) = default;

  OfxPropertySetHandle handle() { return reinterpret_cast<OfxPropertySetHandle>(this); }
  static PropertySet* from(OfxPropertySetHandle h) {
    return reinterpret_cast<PropertySet*>(h);
  }

  void define(std::string_view name, Type type, int dimension) {
    create(name, type, dimension);
  }
  bool has(std::string_view name) const { return find(name) != nullptr; }
  const Property* find(std::string_view name) const {  // searches parents too
    if (auto it = props_.find(name); it != props_.end())
      return &it->second;
    return parent_ ? parent_->find(name) : nullptr;
  }

  template <typename T>
  OfxStatus set(std::string_view name, int index, T value) {
    if (index < 0)
      return kOfxStatErrBadIndex;
    auto it = props_.find(name);
    if (it == props_.end()) {
      // Not defined locally: shadow the parent's definition, or invent one.
      bool single = false;
      Type type = typeFor(name, &single);
      if (!single)
        type = typeOf<T>();
      const auto* def = find_prop_def(name);
      if (!def)
        Logger::debug("property set: creating undeclared property {}", name);
      create(name, type, def ? def->dimension : 0);
      it = props_.find(name);
    }
    Property& p = it->second;
    if (p.dimension > 0 && index >= p.dimension) {
      Logger::warn("property {}: index {} out of range (dimension {})", name, index,
                   p.dimension);
      return kOfxStatErrBadIndex;
    }
    auto store = [&](auto& vec, auto converted) {
      if (index >= static_cast<int>(vec.size()))
        vec.resize(index + 1);
      vec[index] = std::move(converted);
      return kOfxStatOK;
    };
    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
      if (p.type == Type::Int)
        return store(std::get<std::vector<int>>(p.values), static_cast<int>(value));
      if (p.type == Type::Double)
        return store(std::get<std::vector<double>>(p.values), static_cast<double>(value));
    } else if constexpr (std::is_same_v<T, const char*>) {
      if (p.type == Type::String)
        return store(std::get<std::vector<std::string>>(p.values),
                     std::string(value ? value : ""));
    } else {
      if (p.type == Type::Pointer)
        return store(std::get<std::vector<void*>>(p.values), value);
    }
    Logger::warn("property {}: set with {} but it is {}", name, typeName(typeOf<T>()),
                 typeName(p.type));
    return kOfxStatErrValue;
  }

  template <typename T>
  OfxStatus get(std::string_view name, int index, T* out) const {
    const Property* p = find(name);
    if (!p)
      return kOfxStatErrUnknown;
    if (index < 0)
      return kOfxStatErrBadIndex;
    auto load = [&](const auto& vec) -> OfxStatus {
      if (index >= static_cast<int>(vec.size()))
        return kOfxStatErrBadIndex;
      if constexpr (std::is_same_v<T, char*>)
        *out = const_cast<char*>(vec[index].c_str());
      else
        *out = static_cast<T>(vec[index]);
      return kOfxStatOK;
    };
    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
      if (p->type == Type::Int)
        return load(std::get<std::vector<int>>(p->values));
      if (p->type == Type::Double)
        return load(std::get<std::vector<double>>(p->values));
    } else if constexpr (std::is_same_v<T, char*>) {
      if (p->type == Type::String)
        return load(std::get<std::vector<std::string>>(p->values));
    } else {
      if (p->type == Type::Pointer)
        return load(std::get<std::vector<void*>>(p->values));
    }
    Logger::warn("property {}: read as {} but it is {}", name,
                 typeName(typeOf<std::remove_pointer_t<T>>()), typeName(p->type));
    return kOfxStatErrValue;
  }

  OfxStatus dimension(std::string_view name, int* out) const {
    const Property* p = find(name);
    if (!p)
      return kOfxStatErrUnknown;
    *out =
        static_cast<int>(std::visit([](const auto& v) { return v.size(); }, p->values));
    return kOfxStatOK;
  }

  // propReset restores the specification's default, which for most properties
  // is not zero.
  OfxStatus reset(std::string_view name) {
    auto it = props_.find(name);
    if (it == props_.end())
      return kOfxStatErrUnknown;
    Property& p = it->second;
    auto n = static_cast<size_t>(p.dimension);
    std::visit([n](auto& v) { v.assign(n, {}); }, p.values);
    if (const auto* def = find_prop_def(name))
      seedDefault(p, def->defaults);
    return kOfxStatOK;
  }

  // Convenience for host code: value or default, never an error.
  int getInt(std::string_view name, int index = 0, int fallback = 0) const {
    int v = fallback;
    return get(name, index, &v) == kOfxStatOK ? v : fallback;
  }

  double getDouble(std::string_view name, int index = 0, double fallback = 0) const {
    double v = fallback;
    return get(name, index, &v) == kOfxStatOK ? v : fallback;
  }

  std::string getString(std::string_view name, int index = 0,
                        std::string_view fallback = "") const {
    char* v = nullptr;
    return get(name, index, &v) == kOfxStatOK ? std::string(v) : std::string(fallback);
  }

  std::vector<std::string> getStrings(std::string_view name) const {
    std::vector<std::string> out;
    int n = 0;
    if (dimension(name, &n) != kOfxStatOK)
      return out;
    for (int i = 0; i < n; ++i) out.push_back(getString(name, i));
    return out;
  }

  // One line per property, for diagnostics.
  std::string dump(std::string_view indent = "") const {
    std::ostringstream os;
    for (const auto& [name, p] : props_) {
      os << indent << name << " = ";
      std::visit(
          [&](const auto& vec) {
            for (size_t i = 0; i < vec.size(); ++i) {
              if (i)
                os << ", ";
              if constexpr (std::is_same_v<std::decay_t<decltype(vec[i])>, std::string>)
                os << '"' << vec[i] << '"';
              else
                os << vec[i];
            }
          },
          p.values);
      os << "\n";
    }
    return os.str();
  }

  static const OfxPropertySuiteV1* suite() {
    // clang-format off
    static const OfxPropertySuiteV1 kSuite = {
        detail::propSetPointer, detail::propSetString, detail::propSetDouble, detail::propSetInt,
        detail::propSetPointerN, detail::propSetStringN, detail::propSetDoubleN, detail::propSetIntN,
        detail::propGetPointer, detail::propGetString, detail::propGetDouble, detail::propGetInt,
        detail::propGetPointerN, detail::propGetStringN, detail::propGetDoubleN, detail::propGetIntN,
        detail::propReset, detail::propGetDimension,
    };
    // clang-format on
    return &kSuite;
  }

 private:
  static Type storageType(openfx::PropType t) {
    switch (t) {
      case openfx::PropType::Int:
      case openfx::PropType::Bool:
        return Type::Int;
      case openfx::PropType::Double:
        return Type::Double;
      case openfx::PropType::Pointer:
        return Type::Pointer;
      case openfx::PropType::Enum:
      case openfx::PropType::String:
        break;
    }
    return Type::String;
  }

  template <typename T>
  static constexpr Type typeOf() {
    if constexpr (std::is_same_v<T, int>)
      return Type::Int;
    else if constexpr (std::is_same_v<T, double>)
      return Type::Double;
    else if constexpr (std::is_same_v<T, void*>)
      return Type::Pointer;
    else
      return Type::String;
  }

  static const char* typeName(Type t) {
    switch (t) {
      case Type::Int:
        return "int";
      case Type::Double:
        return "double";
      case Type::String:
        return "string";
      case Type::Pointer:
        return "pointer";
    }
    return "?";
  }

  static Type typeFor(std::string_view propName, bool* single) {
    const auto* def = find_prop_def(propName);
    *single = def && def->supportedTypes.size() == 1;
    return *single ? storageType(def->supportedTypes[0]) : Type::String;
  }

  Property& create(std::string_view name, Type type, int dimension) {
    Property p{type, dimension, {}};
    auto n = static_cast<size_t>(dimension);
    switch (type) {
      case Type::Int:
        p.values = std::vector<int>(n, 0);
        break;
      case Type::Double:
        p.values = std::vector<double>(n, 0.0);
        break;
      case Type::String:
        p.values = std::vector<std::string>(n);
        break;
      case Type::Pointer:
        p.values = std::vector<void*>(n, nullptr);
        break;
    }
    return props_.insert_or_assign(std::string(name), std::move(p)).first->second;
  }

  // Fill a freshly created property with the spec default from the metadata,
  // held there as text. One value fills every dimension, otherwise there is
  // one value per dimension. Pointer properties never have a default.
  static void seedDefault(Property& p, openfx::span<const char* const> defaults) {
    if (defaults.empty())
      return;
    auto fill = [&](auto& vec, auto convert) {
      size_t n =
          defaults.size() == 1 ? vec.size() : std::min(vec.size(), defaults.size());
      for (size_t i = 0; i < n; ++i)
        vec[i] = convert(defaults[defaults.size() == 1 ? 0 : i]);
    };
    switch (p.type) {
      case Type::Int:
        fill(std::get<std::vector<int>>(p.values),
             [](const char* s) { return std::stoi(s); });
        break;
      case Type::Double:
        fill(std::get<std::vector<double>>(p.values),
             [](const char* s) { return std::stod(s); });
        break;
      case Type::String:
        fill(std::get<std::vector<std::string>>(p.values),
             [](const char* s) { return std::string(s); });
        break;
      case Type::Pointer:
        break;
    }
  }

  // ---------------------------------------------------------------------
  // OfxPropertySuiteV1: C callbacks bridging the handle to a PropertySet.
  // ---------------------------------------------------------------------
  struct detail {
    // Every entry point: a null handle or property name is refused before
    // anything else, and an exception becomes a status rather than unwinding
    // into the plugin.
    template <class F>
    static OfxStatus guarded(OfxPropertySetHandle h, const char* name, F&& f) noexcept {
      return callAtCBoundary([&] {
        PropertySet* set = PropertySet::from(h);
        if (!set)
          return kOfxStatErrBadHandle;
        if (!name)
          return kOfxStatErrUnknown;
        return f(*set);
      });
    }

    template <typename T>
    static OfxStatus setN(OfxPropertySetHandle h, const char* name, int count,
                          const T* values) noexcept {
      return guarded(h, name, [&](PropertySet& set) {
        for (int i = 0; i < count; ++i)
          if (OfxStatus s = set.set(name, i, values[i]); s != kOfxStatOK)
            return s;
        return kOfxStatOK;
      });
    }

    template <typename T>
    static OfxStatus getN(OfxPropertySetHandle h, const char* name, int count,
                          T* values) noexcept {
      return guarded(h, name, [&](PropertySet& set) {
        for (int i = 0; i < count; ++i)
          if (OfxStatus s = set.get(name, i, &values[i]); s != kOfxStatOK)
            return s;
        return kOfxStatOK;
      });
    }

    static OfxStatus propSetPointer(OfxPropertySetHandle h, const char* n, int i,
                                    void* v) noexcept {
      return guarded(h, n, [&](PropertySet& set) { return set.set(n, i, v); });
    }
    static OfxStatus propSetString(OfxPropertySetHandle h, const char* n, int i,
                                   const char* v) noexcept {
      return guarded(h, n, [&](PropertySet& set) { return set.set(n, i, v); });
    }
    static OfxStatus propSetDouble(OfxPropertySetHandle h, const char* n, int i,
                                   double v) noexcept {
      return guarded(h, n, [&](PropertySet& set) { return set.set(n, i, v); });
    }
    static OfxStatus propSetInt(OfxPropertySetHandle h, const char* n, int i,
                                int v) noexcept {
      return guarded(h, n, [&](PropertySet& set) { return set.set(n, i, v); });
    }
    static OfxStatus propSetPointerN(OfxPropertySetHandle h, const char* n, int c,
                                     void* const* v) noexcept {
      return setN(h, n, c, v);
    }
    static OfxStatus propSetStringN(OfxPropertySetHandle h, const char* n, int c,
                                    const char* const* v) noexcept {
      return setN(h, n, c, v);
    }
    static OfxStatus propSetDoubleN(OfxPropertySetHandle h, const char* n, int c,
                                    const double* v) noexcept {
      return setN(h, n, c, v);
    }
    static OfxStatus propSetIntN(OfxPropertySetHandle h, const char* n, int c,
                                 const int* v) noexcept {
      return setN(h, n, c, v);
    }
    static OfxStatus propGetPointer(OfxPropertySetHandle h, const char* n, int i,
                                    void** v) noexcept {
      return guarded(h, n, [&](PropertySet& set) { return set.get(n, i, v); });
    }
    static OfxStatus propGetString(OfxPropertySetHandle h, const char* n, int i,
                                   char** v) noexcept {
      return guarded(h, n, [&](PropertySet& set) { return set.get(n, i, v); });
    }
    static OfxStatus propGetDouble(OfxPropertySetHandle h, const char* n, int i,
                                   double* v) noexcept {
      return guarded(h, n, [&](PropertySet& set) { return set.get(n, i, v); });
    }
    static OfxStatus propGetInt(OfxPropertySetHandle h, const char* n, int i,
                                int* v) noexcept {
      return guarded(h, n, [&](PropertySet& set) { return set.get(n, i, v); });
    }
    static OfxStatus propGetPointerN(OfxPropertySetHandle h, const char* n, int c,
                                     void** v) noexcept {
      return getN(h, n, c, v);
    }
    static OfxStatus propGetStringN(OfxPropertySetHandle h, const char* n, int c,
                                    char** v) noexcept {
      return getN(h, n, c, v);
    }
    static OfxStatus propGetDoubleN(OfxPropertySetHandle h, const char* n, int c,
                                    double* v) noexcept {
      return getN(h, n, c, v);
    }
    static OfxStatus propGetIntN(OfxPropertySetHandle h, const char* n, int c,
                                 int* v) noexcept {
      return getN(h, n, c, v);
    }
    static OfxStatus propReset(OfxPropertySetHandle h, const char* n) noexcept {
      return guarded(h, n, [&](PropertySet& set) { return set.reset(n); });
    }
    static OfxStatus propGetDimension(OfxPropertySetHandle h, const char* n,
                                      int* d) noexcept {
      return guarded(h, n, [&](PropertySet& set) { return set.dimension(n, d); });
    }
  };

  std::map<std::string, Property, std::less<>> props_;
  const PropertySet* parent_ = nullptr;
};

}  // namespace openfx::host
