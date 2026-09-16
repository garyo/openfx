// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#include "PropertySet.h"

#include <openfx/ofxPropsBySet.h>
#include <openfx/ofxPropsMetadata.h>

#include <cstring>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#include "Log.h"

namespace testhost {

using Type = PropertySet::Type;

namespace {

Type storageType(openfx::PropType t) {
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
constexpr Type typeOf() {
  if constexpr (std::is_same_v<T, int>) return Type::Int;
  else if constexpr (std::is_same_v<T, double>) return Type::Double;
  else if constexpr (std::is_same_v<T, void*>) return Type::Pointer;
  else return Type::String;
}

const char* typeName(Type t) {
  switch (t) {
    case Type::Int: return "int";
    case Type::Double: return "double";
    case Type::String: return "string";
    case Type::Pointer: return "pointer";
  }
  return "?";
}

const openfx::PropDef* lookupDef(std::string_view name) {
  for (const auto& def : openfx::prop_defs.data)
    if (name == def.name) return &def;
  return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction from metadata
// ---------------------------------------------------------------------------

Type PropertySet::typeFor(std::string_view propName, bool* single) {
  const auto* def = lookupDef(propName);
  *single = def && def->supportedTypes.size() == 1;
  return *single ? storageType(def->supportedTypes[0]) : Type::String;
}

PropertySet::PropertySet(std::string_view setName, const PropertySet* parent) : parent_(parent) {
  // prop_sets is keyed by C-string pointer, so look up by content.
  const std::vector<openfx::Prop>* props = nullptr;
  for (const auto& [key, value] : openfx::prop_sets)
    if (setName == key) props = &value;
  if (!props) throw std::runtime_error("unknown property set " + std::string(setName));
  for (const auto& prop : *props) {
    // Multi-typed properties (e.g. OfxParamPropDefault) take the type of the first write.
    if (prop.def.supportedTypes.size() != 1) continue;
    auto& p = create(prop.name, storageType(prop.def.supportedTypes[0]), prop.def.dimension);
    if (parent) {
      if (const auto* inherited = parent->find(prop.name); inherited && inherited->type == p.type)
        p.values = inherited->values;
    }
  }
}

PropertySet PropertySet::forAction(std::string_view action, std::string_view which) {
  PropertySet set;
  auto it = openfx::action_props.find(std::array<std::string_view, 2>{action, which});
  if (it == openfx::action_props.end()) return set;
  for (const char* name : it->second) {
    if (const auto* def = lookupDef(name); def && def->supportedTypes.size() == 1)
      set.create(name, storageType(def->supportedTypes[0]), def->dimension);
  }
  return set;
}

PropertySet::Property& PropertySet::create(std::string_view name, Type type, int dimension) {
  Property p{type, dimension, {}};
  auto n = static_cast<size_t>(dimension);
  switch (type) {
    case Type::Int: p.values = std::vector<int>(n, 0); break;
    case Type::Double: p.values = std::vector<double>(n, 0.0); break;
    case Type::String: p.values = std::vector<std::string>(n); break;
    case Type::Pointer: p.values = std::vector<void*>(n, nullptr); break;
  }
  return props_.insert_or_assign(std::string(name), std::move(p)).first->second;
}

void PropertySet::define(std::string_view name, Type type, int dimension) { create(name, type, dimension); }

bool PropertySet::has(std::string_view name) const { return find(name) != nullptr; }

const PropertySet::Property* PropertySet::find(std::string_view name) const {
  if (auto it = props_.find(name); it != props_.end()) return &it->second;
  return parent_ ? parent_->find(name) : nullptr;
}

// ---------------------------------------------------------------------------
// Typed access
// ---------------------------------------------------------------------------

template <typename T>
OfxStatus PropertySet::set(std::string_view name, int index, T value) {
  if (index < 0) return kOfxStatErrBadIndex;
  auto it = props_.find(name);
  if (it == props_.end()) {
    // Not defined locally: shadow the parent's definition, or invent one.
    bool single = false;
    Type type = typeFor(name, &single);
    if (!single) type = typeOf<T>();
    const auto* def = lookupDef(name);
    if (!def) log::debug("property set: creating undeclared property {}", name);
    create(name, type, def ? def->dimension : 0);
    it = props_.find(name);
  }
  Property& p = it->second;
  if (p.dimension > 0 && index >= p.dimension) {
    log::warn("property {}: index {} out of range (dimension {})", name, index, p.dimension);
    return kOfxStatErrBadIndex;
  }
  auto store = [&](auto& vec, auto converted) {
    if (index >= static_cast<int>(vec.size())) vec.resize(index + 1);
    vec[index] = converted;
    return kOfxStatOK;
  };
  if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
    if (p.type == Type::Int) return store(std::get<std::vector<int>>(p.values), static_cast<int>(value));
    if (p.type == Type::Double) return store(std::get<std::vector<double>>(p.values), static_cast<double>(value));
  } else if constexpr (std::is_same_v<T, const char*>) {
    if (p.type == Type::String) return store(std::get<std::vector<std::string>>(p.values), std::string(value ? value : ""));
  } else {
    if (p.type == Type::Pointer) return store(std::get<std::vector<void*>>(p.values), value);
  }
  log::warn("property {}: set with {} but it is {}", name, typeName(typeOf<T>()), typeName(p.type));
  return kOfxStatErrValue;
}

template <typename T>
OfxStatus PropertySet::get(std::string_view name, int index, T* out) const {
  const Property* p = find(name);
  if (!p) return kOfxStatErrUnknown;
  if (index < 0) return kOfxStatErrBadIndex;
  auto load = [&](const auto& vec) -> OfxStatus {
    if (index >= static_cast<int>(vec.size())) return kOfxStatErrBadIndex;
    if constexpr (std::is_same_v<T, char*>) *out = const_cast<char*>(vec[index].c_str());
    else *out = static_cast<T>(vec[index]);
    return kOfxStatOK;
  };
  if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
    if (p->type == Type::Int) return load(std::get<std::vector<int>>(p->values));
    if (p->type == Type::Double) return load(std::get<std::vector<double>>(p->values));
  } else if constexpr (std::is_same_v<T, char*>) {
    if (p->type == Type::String) return load(std::get<std::vector<std::string>>(p->values));
  } else {
    if (p->type == Type::Pointer) return load(std::get<std::vector<void*>>(p->values));
  }
  log::warn("property {}: read as {} but it is {}", name, typeName(typeOf<std::remove_pointer_t<T>>()),
            typeName(p->type));
  return kOfxStatErrValue;
}

template OfxStatus PropertySet::set<int>(std::string_view, int, int);
template OfxStatus PropertySet::set<double>(std::string_view, int, double);
template OfxStatus PropertySet::set<const char*>(std::string_view, int, const char*);
template OfxStatus PropertySet::set<void*>(std::string_view, int, void*);
template OfxStatus PropertySet::get<int>(std::string_view, int, int*) const;
template OfxStatus PropertySet::get<double>(std::string_view, int, double*) const;
template OfxStatus PropertySet::get<char*>(std::string_view, int, char**) const;
template OfxStatus PropertySet::get<void*>(std::string_view, int, void**) const;

OfxStatus PropertySet::dimension(std::string_view name, int* out) const {
  const Property* p = find(name);
  if (!p) return kOfxStatErrUnknown;
  *out = static_cast<int>(std::visit([](const auto& v) { return v.size(); }, p->values));
  return kOfxStatOK;
}

OfxStatus PropertySet::reset(std::string_view name) {
  auto it = props_.find(name);
  if (it == props_.end()) return kOfxStatErrUnknown;
  Property& p = it->second;
  auto n = static_cast<size_t>(p.dimension);
  std::visit([n](auto& v) { v.assign(n, {}); }, p.values);
  return kOfxStatOK;
}

int PropertySet::getInt(std::string_view name, int index, int fallback) const {
  int v = fallback;
  return get(name, index, &v) == kOfxStatOK ? v : fallback;
}

double PropertySet::getDouble(std::string_view name, int index, double fallback) const {
  double v = fallback;
  return get(name, index, &v) == kOfxStatOK ? v : fallback;
}

std::string PropertySet::getString(std::string_view name, int index, std::string_view fallback) const {
  char* v = nullptr;
  return get(name, index, &v) == kOfxStatOK ? std::string(v) : std::string(fallback);
}

std::vector<std::string> PropertySet::getStrings(std::string_view name) const {
  std::vector<std::string> out;
  int n = 0;
  if (dimension(name, &n) != kOfxStatOK) return out;
  for (int i = 0; i < n; ++i) out.push_back(getString(name, i));
  return out;
}

std::string PropertySet::dump(std::string_view indent) const {
  std::ostringstream os;
  for (const auto& [name, p] : props_) {
    os << indent << name << " = ";
    std::visit(
        [&](const auto& vec) {
          for (size_t i = 0; i < vec.size(); ++i) {
            if (i) os << ", ";
            if constexpr (std::is_same_v<std::decay_t<decltype(vec[i])>, std::string>) os << '"' << vec[i] << '"';
            else os << vec[i];
          }
        },
        p.values);
    os << "\n";
  }
  return os.str();
}

// ---------------------------------------------------------------------------
// OfxPropertySuiteV1
// ---------------------------------------------------------------------------

namespace {

template <typename T>
OfxStatus setN(OfxPropertySetHandle h, const char* name, int count, const T* values) {
  auto* set = PropertySet::from(h);
  if (!set) return kOfxStatErrBadHandle;
  for (int i = 0; i < count; ++i)
    if (OfxStatus s = set->set(name, i, values[i]); s != kOfxStatOK) return s;
  return kOfxStatOK;
}

template <typename T>
OfxStatus getN(OfxPropertySetHandle h, const char* name, int count, T* values) {
  auto* set = PropertySet::from(h);
  if (!set) return kOfxStatErrBadHandle;
  for (int i = 0; i < count; ++i)
    if (OfxStatus s = set->get(name, i, &values[i]); s != kOfxStatOK) return s;
  return kOfxStatOK;
}

OfxStatus propSetPointer(OfxPropertySetHandle h, const char* n, int i, void* v) { return PropertySet::from(h) ? PropertySet::from(h)->set(n, i, v) : kOfxStatErrBadHandle; }
OfxStatus propSetString(OfxPropertySetHandle h, const char* n, int i, const char* v) { return PropertySet::from(h) ? PropertySet::from(h)->set(n, i, v) : kOfxStatErrBadHandle; }
OfxStatus propSetDouble(OfxPropertySetHandle h, const char* n, int i, double v) { return PropertySet::from(h) ? PropertySet::from(h)->set(n, i, v) : kOfxStatErrBadHandle; }
OfxStatus propSetInt(OfxPropertySetHandle h, const char* n, int i, int v) { return PropertySet::from(h) ? PropertySet::from(h)->set(n, i, v) : kOfxStatErrBadHandle; }
OfxStatus propSetPointerN(OfxPropertySetHandle h, const char* n, int c, void* const* v) { return setN(h, n, c, v); }
OfxStatus propSetStringN(OfxPropertySetHandle h, const char* n, int c, const char* const* v) { return setN(h, n, c, v); }
OfxStatus propSetDoubleN(OfxPropertySetHandle h, const char* n, int c, const double* v) { return setN(h, n, c, v); }
OfxStatus propSetIntN(OfxPropertySetHandle h, const char* n, int c, const int* v) { return setN(h, n, c, v); }
OfxStatus propGetPointer(OfxPropertySetHandle h, const char* n, int i, void** v) { return PropertySet::from(h) ? PropertySet::from(h)->get(n, i, v) : kOfxStatErrBadHandle; }
OfxStatus propGetString(OfxPropertySetHandle h, const char* n, int i, char** v) { return PropertySet::from(h) ? PropertySet::from(h)->get(n, i, v) : kOfxStatErrBadHandle; }
OfxStatus propGetDouble(OfxPropertySetHandle h, const char* n, int i, double* v) { return PropertySet::from(h) ? PropertySet::from(h)->get(n, i, v) : kOfxStatErrBadHandle; }
OfxStatus propGetInt(OfxPropertySetHandle h, const char* n, int i, int* v) { return PropertySet::from(h) ? PropertySet::from(h)->get(n, i, v) : kOfxStatErrBadHandle; }
OfxStatus propGetPointerN(OfxPropertySetHandle h, const char* n, int c, void** v) { return getN(h, n, c, v); }
OfxStatus propGetStringN(OfxPropertySetHandle h, const char* n, int c, char** v) { return getN(h, n, c, v); }
OfxStatus propGetDoubleN(OfxPropertySetHandle h, const char* n, int c, double* v) { return getN(h, n, c, v); }
OfxStatus propGetIntN(OfxPropertySetHandle h, const char* n, int c, int* v) { return getN(h, n, c, v); }
OfxStatus propReset(OfxPropertySetHandle h, const char* n) { return PropertySet::from(h) ? PropertySet::from(h)->reset(n) : kOfxStatErrBadHandle; }
OfxStatus propGetDimension(OfxPropertySetHandle h, const char* n, int* d) { return PropertySet::from(h) ? PropertySet::from(h)->dimension(n, d) : kOfxStatErrBadHandle; }

const OfxPropertySuiteV1 kSuite = {
    propSetPointer, propSetString, propSetDouble, propSetInt,
    propSetPointerN, propSetStringN, propSetDoubleN, propSetIntN,
    propGetPointer, propGetString, propGetDouble, propGetInt,
    propGetPointerN, propGetStringN, propGetDoubleN, propGetIntN,
    propReset, propGetDimension,
};

}  // namespace

const OfxPropertySuiteV1* PropertySet::suite() { return &kSuite; }

}  // namespace testhost
