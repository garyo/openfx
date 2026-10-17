// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxCore.h>
#include <ofxProperty.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "openfx/host/ofxStringLifetime.h"
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
// Through the suite a plugin may also write a property the set does not
// pre-define but the metadata knows, which is created with the metadata's
// type; a name that is neither is kOfxStatErrUnknown, as the specification
// has it. Host code is not held to that: define() and set() create whatever
// they are given, so a property of the host's own that a plugin is to write
// is define()d first. A set may have a parent: reads of properties absent
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

  // A C handle carries no const, so a const set gives the same handle, and C
  // code may be handed one as it is.
  OfxPropertySetHandle handle() const {
    return reinterpret_cast<OfxPropertySetHandle>(const_cast<PropertySet*>(this));
  }
  static PropertySet* from(OfxPropertySetHandle h) {
    return reinterpret_cast<PropertySet*>(h);
  }

  // How many values set() and reset() have written to this set, however they
  // were called: compared across an action, whether the plugin wrote any.
  std::size_t writes() const { return writes_; }

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
      ++writes_;
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
    ++writes_;
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

  // The suite a host gives its plugins, which keeps the strings it returns
  // for as long as Strings::lifetime() says.
  static const OfxPropertySuiteV1* suite() { return table<true>(); }
  // The same suite for the host's own reads and writes, such as through the
  // propsets accessors, which no string lifetime applies to.
  static const OfxPropertySuiteV1* hostSuite() { return table<false>(); }

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
  // OfxPropertySuiteV1: C callbacks bridging the handle to a PropertySet,
  // either the plugin's or the host's.
  // ---------------------------------------------------------------------
  template <bool kForPlugin>
  struct detail {
    // Every entry point: a null handle or property name is refused before
    // anything else, and an exception becomes a status rather than unwinding
    // into the plugin. A null pointer to write a value through is
    // kOfxStatErrBadHandle, the one status the getters have for a bad
    // argument; a null array of values to set is kOfxStatErrValue.
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

    // Whether the suite may write the property: one the set defines, here or
    // in a parent, or one the metadata knows.
    static OfxStatus writable(const PropertySet& set, const char* name) {
      if (set.find(name) || find_prop_def(name))
        return kOfxStatOK;
      Logger::warn(
          "property {}: neither defined in this set nor known to the specification; "
          "refused",
          name);
      return kOfxStatErrUnknown;
    }

    // A string the plugin writes ends the lifetime of those it read from that
    // value; one that the host had already expired cannot be read, so it is
    // refused. Under a strict StringLifetime a string the plugin reads is a
    // copy that expires as the lifetime has it, and every string read starts
    // a new string-returning suite call.
    template <typename T>
    static OfxStatus beforeWrite(OfxPropertySetHandle h, const char* name, int index,
                                 T value) {
      if constexpr (kForPlugin && std::is_same_v<T, const char*>) {
        if (Strings::expired(value)) {
          Logger::warn("property {}: refused a string the host had already expired",
                       name);
          return kOfxStatErrValue;
        }
        Strings::changed(h, name, index);
      }
      return kOfxStatOK;
    }
    template <typename T>
    static void beforeRead() {
      if constexpr (kForPlugin && std::is_same_v<T, char*>)
        Strings::beginStringCall();
    }
    template <typename T>
    static void afterRead(OfxPropertySetHandle h, const char* name, int index, T* value) {
      if constexpr (kForPlugin && std::is_same_v<T, char*>)
        if (Strings::strict())
          *value = const_cast<char*>(Strings::handOut(*value, {h, name, index, 0}));
    }

    template <typename T>
    static OfxStatus setOne(OfxPropertySetHandle h, const char* name, int index,
                            T value) noexcept {
      return guarded(h, name, [&](PropertySet& set) {
        OfxStatus s = writable(set, name);
        if (s == kOfxStatOK)
          s = beforeWrite(h, name, index, value);
        return s == kOfxStatOK ? set.set(name, index, value) : s;
      });
    }

    template <typename T>
    static OfxStatus getOne(OfxPropertySetHandle h, const char* name, int index,
                            T* value) noexcept {
      return guarded(h, name, [&](PropertySet& set) {
        if (!value)
          return kOfxStatErrBadHandle;
        beforeRead<T>();
        const OfxStatus s = set.get(name, index, value);
        if (s == kOfxStatOK)
          afterRead(h, name, index, value);
        return s;
      });
    }

    template <typename T>
    static OfxStatus setN(OfxPropertySetHandle h, const char* name, int count,
                          const T* values) noexcept {
      return guarded(h, name, [&](PropertySet& set) {
        if (const OfxStatus s = writable(set, name); s != kOfxStatOK)
          return s;
        if (!values && count > 0)
          return kOfxStatErrValue;
        for (int i = 0; i < count; ++i) {
          OfxStatus s = beforeWrite(h, name, i, values[i]);
          if (s == kOfxStatOK)
            s = set.set(name, i, values[i]);
          if (s != kOfxStatOK)
            return s;
        }
        return kOfxStatOK;
      });
    }

    template <typename T>
    static OfxStatus getN(OfxPropertySetHandle h, const char* name, int count,
                          T* values) noexcept {
      return guarded(h, name, [&](PropertySet& set) {
        if (!values && count > 0)
          return kOfxStatErrBadHandle;
        beforeRead<T>();
        for (int i = 0; i < count; ++i) {
          if (OfxStatus s = set.get(name, i, &values[i]); s != kOfxStatOK)
            return s;
          afterRead(h, name, i, &values[i]);
        }
        return kOfxStatOK;
      });
    }

    static OfxStatus propSetPointer(OfxPropertySetHandle h, const char* n, int i,
                                    void* v) noexcept {
      return setOne(h, n, i, v);
    }
    static OfxStatus propSetString(OfxPropertySetHandle h, const char* n, int i,
                                   const char* v) noexcept {
      return setOne(h, n, i, v);
    }
    static OfxStatus propSetDouble(OfxPropertySetHandle h, const char* n, int i,
                                   double v) noexcept {
      return setOne(h, n, i, v);
    }
    static OfxStatus propSetInt(OfxPropertySetHandle h, const char* n, int i,
                                int v) noexcept {
      return setOne(h, n, i, v);
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
      return getOne(h, n, i, v);
    }
    static OfxStatus propGetString(OfxPropertySetHandle h, const char* n, int i,
                                   char** v) noexcept {
      return getOne(h, n, i, v);
    }
    static OfxStatus propGetDouble(OfxPropertySetHandle h, const char* n, int i,
                                   double* v) noexcept {
      return getOne(h, n, i, v);
    }
    static OfxStatus propGetInt(OfxPropertySetHandle h, const char* n, int i,
                                int* v) noexcept {
      return getOne(h, n, i, v);
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
      return guarded(h, n, [&](PropertySet& set) {
        if constexpr (kForPlugin)
          Strings::changed(h, n);
        return set.reset(n);
      });
    }
    static OfxStatus propGetDimension(OfxPropertySetHandle h, const char* n,
                                      int* d) noexcept {
      return guarded(h, n, [&](PropertySet& set) {
        return d ? set.dimension(n, d) : kOfxStatErrBadHandle;
      });
    }
  };

  template <bool kForPlugin>
  static const OfxPropertySuiteV1* table() {
    using D = detail<kForPlugin>;
    // clang-format off
    static const OfxPropertySuiteV1 kSuite = {
        D::propSetPointer, D::propSetString, D::propSetDouble, D::propSetInt,
        D::propSetPointerN, D::propSetStringN, D::propSetDoubleN, D::propSetIntN,
        D::propGetPointer, D::propGetString, D::propGetDouble, D::propGetInt,
        D::propGetPointerN, D::propGetStringN, D::propGetDoubleN, D::propGetIntN,
        D::propReset, D::propGetDimension,
    };
    // clang-format on
    return &kSuite;
  }

  std::map<std::string, Property, std::less<>> props_;
  const PropertySet* parent_ = nullptr;
  std::size_t writes_ = 0;
};

}  // namespace openfx::host
