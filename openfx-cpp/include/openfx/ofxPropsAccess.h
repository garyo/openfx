// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <ofxCore.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "ofxExceptions.h"
#include "ofxLog.h"
#include "ofxPropsMetadata.h"
#include "ofxSuites.h"

/**
 * PropertyAccessor: type-safe access to one property set through the C
 * property suite. For example:
 *
 *   using openfx::PropertyAccessor;
 *   using openfx::PropId;
 *
 *   void example(OfxPropertySetHandle handle, const OfxPropertySuiteV1* propSuite) {
 *     PropertyAccessor props(handle, propSuite);
 *
 *     // The PropId gives the property's name, type and dimension.
 *     props.set<PropId::OfxPropLabel>("Gain").set<PropId::OfxPropShortLabel>("G");
 *     const char* label = props.get<PropId::OfxPropLabel>();
 *     int connected = props.get<PropId::OfxImageClipPropConnected>();  // a bool
 *     OfxRectD rod = props.getRectD<PropId::OfxImageEffectPropRegionOfDefinition>();
 *     props.setAll<PropId::OfxPropVersion>({1, 0, 0});
 *     std::vector<int> version = props.getAll<PropId::OfxPropVersion>();
 *
 *     // A property of more than one type takes the one to use.
 *     double displayMax = props.get<PropId::OfxParamPropDisplayMax, double>();
 *     props.set<PropId::OfxParamPropDisplayMax, double>(10.5);
 *
 *     // An enum property's values come from the metadata.
 *     using Extraction = openfx::EnumValue<PropId::OfxImageClipPropFieldExtraction>;
 *     if (Extraction::isValid(kOfxImageFieldSingle))
 *       props.set<PropId::OfxImageClipPropFieldExtraction>(kOfxImageFieldSingle);
 *
 *     // A property the set may not have: read it softly, or ask first.
 *     const char* colourspace = props.get<PropId::OfxImageClipPropColourspace>(0, false);
 *     bool hasColourspace = props.exists<PropId::OfxImageClipPropColourspace>();
 *
 *     // Any property by name, and the suite itself.
 *     double gain = props.getRaw<double>("com.example.Gain");
 *     props.setRaw("com.example.Pass", 2);
 *     double rgb[3] = {};
 *     props.getRawN("com.example.Colour", 3, rgb);
 *     props.reset("com.example.Colour");
 *     int n = props.getDimensionRaw("com.example.Colour");
 *     props.suite()->propGetDimension(props.handle(), "com.example.Colour", &n);
 *   }
 */

namespace openfx {

// ============================================================================
// Host-Extensible Property System Support
// ============================================================================
//
// This section enables hosts to define their own custom properties in their
// own namespaces while maintaining the same type safety as standard OpenFX
// properties. This is achieved through C++17 auto template parameters and
// argument-dependent lookup (ADL).
//
// How it works:
//   1. Hosts define their own PropId enum in their namespace (e.g., myhost::PropId)
//   2. Hosts define PropTraits specializations in their namespace
//   3. Hosts define a prop_traits_helper function for ADL lookup
//   4. PropertyAccessor uses template<auto id> to accept any enum type
//   5. PropTraits_t<id> uses ADL to find the correct PropTraits in the right namespace
//
// Example host usage (in host's header file):
//   namespace myhost {
//     enum class PropId { CustomProperty, ... };
//     namespace properties {
//       template<PropId id> struct PropTraits;
//       template<> struct PropTraits<PropId::CustomProperty> { ... };
//     }
//     // Enable ADL lookup
//     template<PropId id>
//     properties::PropTraits<id> prop_traits_helper(std::integral_constant<PropId, id>);
//   }
//
// Plugin usage:
//   props.get<openfx::PropId::OfxPropLabel>();  // Standard property
//   props.get<myhost::PropId::CustomProperty>();  // Host property
//
// ============================================================================

// Forward declare the ADL helper for standard OpenFX properties.
// This function is never actually called - it's only used for type deduction via
// decltype. It must be in the same namespace as PropId (openfx) for ADL to work.
template <PropId id>
properties::PropTraits<id> prop_traits_helper(std::integral_constant<PropId, id>);

// PropTraits_t: Type alias that uses ADL to find the correct PropTraits
// for any PropId enum (openfx::PropId or host-defined).
// The decltype+ADL pattern allows each namespace to provide its own prop_traits_helper.
template <auto id>
using PropTraits_t =
    decltype(prop_traits_helper(std::integral_constant<decltype(id), id>{}));

// Type-mapping helper to infer C++ type from PropType
template <PropType propType>
struct PropTypeToNative {
  using type = void;  // Default case, should never be used directly
};

// Specializations for each property type
template <>
struct PropTypeToNative<PropType::Int> {
  using type = int;
};
template <>
struct PropTypeToNative<PropType::Double> {
  using type = double;
};
template <>
struct PropTypeToNative<PropType::Enum> {
  using type = const char*;
};
template <>
struct PropTypeToNative<PropType::Bool> {
  using type = int;
};
template <>
struct PropTypeToNative<PropType::String> {
  using type = const char*;
};
template <>
struct PropTypeToNative<PropType::Pointer> {
  using type = void*;
};

// Helper to access enum property values with strong typing.
// Works with any PropId enum (standard OpenFX or host-defined).
template <auto id>
struct EnumValue {
  using Traits = PropTraits_t<id>;

  // A constant index is checked where it is used in a constant expression;
  // nothing can check a runtime one at compile time.
  static constexpr const char* get(size_t index) { return Traits::def.enumValues[index]; }

  static constexpr size_t size() { return Traits::def.enumValues.size(); }

  static constexpr bool isValid(std::string_view value) {
    for (auto val : Traits::def.enumValues) {
      if (value == val)
        return true;
    }
    return false;
  }
};

// Type-safe property accessor for any props of a given prop set.
//
// A failed suite call throws: PropertyNotFoundException when the set has no
// such property (kOfxStatErrUnknown), and OfxException with the suite's
// status for any other failure. The message gives the property and the
// status. Nothing is logged: the exception is the one report of a failure.
//
// The calls that read, write or reset a property, or ask its dimension, take
// error_if_missing, true by default. A call with it false is soft about one
// failure alone, a property the set does not have: a soft write or reset of
// one does nothing, and a soft read of one returns the fallback for its type,
// the same from every getter:
//
//   int, bool     0, false
//   double        0.0
//   const char*   "", a static empty string, never null
//   void*         nullptr
//   dimension     0, so a soft getAll() of a variable-dimension property
//                 gives no values
//
// Any other failure throws from a soft call as from any other: a bad handle,
// an index past the end, a value of the wrong type. exists() tells a property
// the set does not have from one that holds the fallback's value.
class PropertyAccessor {
 public:
  // Basic constructor
  explicit PropertyAccessor(OfxPropertySetHandle propset,
                            const OfxPropertySuiteV1* prop_suite)
      : propset_(propset), propSuite_(prop_suite) {
    if (!propSuite_) {
      throw SuiteNotFoundException(kOfxStatErrMissingHostFeature,
                                   "PropertyAccessor: missing property suite");
    }
  }

  // Constructor taking a prop set and a suites container, for simplicity
  explicit PropertyAccessor(OfxPropertySetHandle propset, const SuiteContainer& suites)
      : propset_(propset) {
    propSuite_ = suites.get<OfxPropertySuiteV1>();
    if (!propSuite_) {
      throw SuiteNotFoundException(kOfxStatErrMissingHostFeature,
                                   "PropertyAccessor: missing property suite");
    }
  }

  // Convenience constructors for ImageEffect -- get effect property set & construct
  // accessor. If the host gives no property set, this throws OfxException with
  // its status, or kOfxStatErrBadHandle if it answered kOfxStatOK without one.
  explicit PropertyAccessor(OfxImageEffectHandle effect,
                            const OfxImageEffectSuiteV1* effects_suite,
                            const OfxPropertySuiteV1* prop_suite)
      : propset_(nullptr), propSuite_(prop_suite) {
    if (!propSuite_) {
      throw SuiteNotFoundException(kOfxStatErrMissingHostFeature,
                                   "PropertyAccessor: missing property suite");
    }
    if (!effects_suite) {
      throw SuiteNotFoundException(kOfxStatErrMissingHostFeature,
                                   "PropertyAccessor: missing effects suite");
    }
    requirePropSet(effects_suite->getPropertySet(effect, &propset_), "getPropertySet");
  }

  explicit PropertyAccessor(OfxImageEffectHandle effect, const SuiteContainer& suites)
      : PropertyAccessor(effect, suites.get<OfxImageEffectSuiteV1>(),
                         suites.get<OfxPropertySuiteV1>()) {}

  // Convenience constructors for Interact -- get the interact's property set &
  // construct accessor, throwing as the ImageEffect ones do.
  explicit PropertyAccessor(OfxInteractHandle interact,
                            const OfxInteractSuiteV1* interact_suite,
                            const OfxPropertySuiteV1* prop_suite)
      : propset_(nullptr), propSuite_(prop_suite) {
    if (!propSuite_) {
      throw SuiteNotFoundException(kOfxStatErrMissingHostFeature,
                                   "PropertyAccessor: missing property suite");
    }
    if (!interact_suite) {
      throw SuiteNotFoundException(kOfxStatErrMissingHostFeature,
                                   "PropertyAccessor: missing interact suite");
    }
    requirePropSet(interact_suite->interactGetPropertySet(interact, &propset_),
                   "interactGetPropertySet");
  }
  explicit PropertyAccessor(OfxInteractHandle interact, const SuiteContainer& suites)
      : PropertyAccessor(interact, suites.get<OfxInteractSuiteV1>(),
                         suites.get<OfxPropertySuiteV1>()) {}

  // Get property value using PropId (compile-time type checking).
  // Works with any PropId enum (openfx::PropId or host-defined).
  // The guard is a non-type parameter, so that get<id, T>() below cannot
  // satisfy it by naming a type and make the two overloads ambiguous.
  template <auto id, std::enable_if_t<!PropTraits_t<id>::is_multitype, int> = 0>
  typename PropTraits_t<id>::type get(int index = 0, bool error_if_missing = true) const {
    using Traits = PropTraits_t<id>;
    return read<typename Traits::type>(Traits::def.name, index, error_if_missing);
  }

  // Get multi-type property value (requires explicit type).
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id, typename T,
            std::enable_if_t<PropTraits_t<id>::is_multitype, int> = 0>
  T get(int index = 0, bool error_if_missing = true) const {
    using Traits = PropTraits_t<id>;

    // Check if T is compatible with any of the supported PropTypes
    constexpr bool isValidType = [&]() {
      for (const auto& type : Traits::def.supportedTypes) {
        if constexpr (std::is_same_v<T, int> || std::is_same_v<T, bool>) {
          if (type == PropType::Int || type == PropType::Bool || type == PropType::Enum)
            return true;
        } else if constexpr (std::is_same_v<T, double>) {
          if (type == PropType::Double)
            return true;
        } else if constexpr (std::is_same_v<T, const char*>) {
          if (type == PropType::String || type == PropType::Enum)
            return true;
        } else if constexpr (std::is_same_v<T, void*>) {
          if (type == PropType::Pointer)
            return true;
        }
      }
      return false;
    }();

    static_assert(isValidType, "Requested type is not compatible with this property");
    return read<T>(Traits::def.name, index, error_if_missing);
  }

  // Set property value using PropId (compile-time type checking).
  // Works with any PropId enum (openfx::PropId or host-defined).
  // An enum property takes any string, as the C API does: hosts and later
  // versions of the specification use values the metadata does not list.
  // EnumValue<id>::isValid() checks a value against the list.
  template <auto id>
  PropertyAccessor& set(typename PropTraits_t<id>::type value, int index = 0,
                        bool error_if_missing = true) {
    using Traits = PropTraits_t<id>;
    static_assert(!Traits::is_multitype,
                  "This property supports multiple types. Use set<PropId, T>() instead.");
    write(Traits::def.name, value, index, error_if_missing);
    return *this;
  }

  // Set multi-type property value (requires explicit type).
  // Should only be used for multitype props (SFINAE).
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id, typename T,
            typename = std::enable_if_t<PropTraits_t<id>::is_multitype>>
  PropertyAccessor& set(T value, int index = 0, bool error_if_missing = true) {
    using Traits = PropTraits_t<id>;

    // Check if T is compatible with any of the supported PropTypes
    constexpr bool isValidType = [&]() {
      for (const auto& type : Traits::def.supportedTypes) {
        if constexpr (std::is_same_v<T, int> || std::is_same_v<T, bool>) {
          if (type == PropType::Int || type == PropType::Bool)
            return true;
        } else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
          if (type == PropType::Double)
            return true;
        } else if constexpr (std::is_same_v<T, const char*>) {
          if (type == PropType::String)  // no Enums here -- there shouldn't be
                                         // any multi-type enums
            return true;
        } else if constexpr (std::is_same_v<T, void*>) {
          if (type == PropType::Pointer)
            return true;
        }
      }
      return false;
    }();

    static_assert(isValidType, "Requested type is not compatible with this property");
    write(Traits::def.name, value, index, error_if_missing);
    return *this;
  }

  // Get all values of a property (for single-type properties).
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id>
  auto getAll(bool error_if_missing = true) const {
    static_assert(!PropTraits_t<id>::is_multitype,
                  "This property supports multiple types. Use getAllTyped<PropId, "
                  "ElementType>() instead.");
    assert(propset_ != nullptr);

    using ValueType = typename PropTraits_t<id>::type;

    // If dimension is known at compile time, use std::array for stack allocation
    if constexpr (PropTraits_t<id>::def.dimension > 0) {
      constexpr int dim = PropTraits_t<id>::def.dimension;
      std::array<ValueType, dim> values;

      for (int i = 0; i < dim; ++i) {
        values[i] = get<id>(i, error_if_missing);
      }

      return values;
    } else {
      // Otherwise use std::vector for dynamic sizing. A soft read of a
      // property the set does not have asks for no values at all.
      int dimension = getDimension<id>(error_if_missing);
      std::vector<ValueType> values;
      values.reserve(dimension);

      for (int i = 0; i < dimension; ++i) {
        values.push_back(get<id>(i, error_if_missing));
      }

      return values;
    }
  }

  // Get all values of a multi-type property - require explicit ElementType.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id, typename ElementType>
  auto getAllTyped(bool error_if_missing = true) const {
    static_assert(
        PropTraits_t<id>::is_multitype,
        "This property does not support multiple types. Use getAll<PropId>() instead.");
    assert(propset_ != nullptr);

    // If dimension is known at compile time, use std::array for stack allocation
    if constexpr (PropTraits_t<id>::def.dimension > 0) {
      constexpr int dim = PropTraits_t<id>::def.dimension;
      std::array<ElementType, dim> values;

      for (int i = 0; i < dim; ++i) {
        values[i] = get<id, ElementType>(i, error_if_missing);
      }

      return values;
    } else {
      // Otherwise use std::vector for dynamic sizing. A soft read of a
      // property the set does not have asks for no values at all.
      int dimension = getDimension<id>(error_if_missing);
      std::vector<ElementType> values;
      values.reserve(dimension);

      for (int i = 0; i < dimension; ++i) {
        values.push_back(get<id, ElementType>(i, error_if_missing));
      }

      return values;
    }
  }

  // Set all values of a prop

  // For single-type properties with any container.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id,
            typename Container>  // Container must have size() and operator[]
  PropertyAccessor& setAll(const Container& values, bool error_if_missing = true) {
    static_assert(!PropTraits_t<id>::is_multitype,
                  "This property supports multiple types. Use setAll<PropId, "
                  "ElementType>(container) instead.");
    assert(propset_ != nullptr);

    for (size_t i = 0; i < values.size(); ++i) {
      this->template set<id>(values[i], static_cast<int>(i), error_if_missing);
    }

    return *this;
  }

  // For single-type properties with initializer lists.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id>
  PropertyAccessor& setAll(std::initializer_list<typename PropTraits_t<id>::type> values,
                           bool error_if_missing = true) {
    static_assert(!PropTraits_t<id>::is_multitype,
                  "This property supports multiple types. Use "
                  "setAllTyped<PropId, ElementType>() instead.");
    assert(propset_ != nullptr);

    int index = 0;
    for (const auto& value : values) {
      this->template set<id>(value, index++, error_if_missing);
    }

    return *this;
  }

  // For 2-d (PointD) single-type properties.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id,
            std::enable_if_t<PropTraits_t<id>::def.dimension == 2 &&
                                 !PropTraits_t<id>::is_multitype &&
                                 std::is_same_v<typename PropTraits_t<id>::type, double>,
                             int> = 0>
  PropertyAccessor& set(OfxPointD values, bool error_if_missing = true) {
    assert(propset_ != nullptr);
    this->template set<id>(values.x, 0, error_if_missing);
    this->template set<id>(values.y, 1, error_if_missing);
    return *this;
  }

  // For 2-d (PointD) single-type properties.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id,
            std::enable_if_t<PropTraits_t<id>::def.dimension == 2 &&
                                 !PropTraits_t<id>::is_multitype &&
                                 std::is_same_v<typename PropTraits_t<id>::type, double>,
                             int> = 0>
  OfxPointD getPointD(bool error_if_missing = true) const {
    assert(propset_ != nullptr);
    return OfxPointD{get<id>(0, error_if_missing), get<id>(1, error_if_missing)};
  }

  // For 2-d (PointI) single-type properties.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id,
            std::enable_if_t<PropTraits_t<id>::def.dimension == 2 &&
                                 !PropTraits_t<id>::is_multitype &&
                                 std::is_same_v<typename PropTraits_t<id>::type, int>,
                             int> = 0>
  PropertyAccessor& set(OfxPointI values, bool error_if_missing = true) {
    assert(propset_ != nullptr);
    this->template set<id>(values.x, 0, error_if_missing);
    this->template set<id>(values.y, 1, error_if_missing);
    return *this;
  }

  // For 2-d (PointI) single-type properties.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id,
            std::enable_if_t<PropTraits_t<id>::def.dimension == 2 &&
                                 !PropTraits_t<id>::is_multitype &&
                                 std::is_same_v<typename PropTraits_t<id>::type, int>,
                             int> = 0>
  OfxPointI getPointI(bool error_if_missing = true) const {
    assert(propset_ != nullptr);
    return OfxPointI{get<id>(0, error_if_missing), get<id>(1, error_if_missing)};
  }

  // For 4-d (RectD) single-type properties.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id,
            std::enable_if_t<PropTraits_t<id>::def.dimension == 4 &&
                                 !PropTraits_t<id>::is_multitype &&
                                 std::is_same_v<typename PropTraits_t<id>::type, double>,
                             int> = 0>
  PropertyAccessor& set(OfxRectD values, bool error_if_missing = true) {
    assert(propset_ != nullptr);
    this->template set<id>(values.x1, 0, error_if_missing);
    this->template set<id>(values.y1, 1, error_if_missing);
    this->template set<id>(values.x2, 2, error_if_missing);
    this->template set<id>(values.y2, 3, error_if_missing);
    return *this;
  }

  // For 4-d (RectD) single-type properties.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id,
            std::enable_if_t<PropTraits_t<id>::def.dimension == 4 &&
                                 !PropTraits_t<id>::is_multitype &&
                                 std::is_same_v<typename PropTraits_t<id>::type, double>,
                             int> = 0>
  OfxRectD getRectD(bool error_if_missing = true) const {
    assert(propset_ != nullptr);
    return OfxRectD{get<id>(0, error_if_missing), get<id>(1, error_if_missing),
                    get<id>(2, error_if_missing), get<id>(3, error_if_missing)};
  }

  // For 4-d (RectI) single-type properties.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id,
            std::enable_if_t<PropTraits_t<id>::def.dimension == 4 &&
                                 !PropTraits_t<id>::is_multitype &&
                                 std::is_same_v<typename PropTraits_t<id>::type, int>,
                             int> = 0>
  PropertyAccessor& set(OfxRectI values, bool error_if_missing = true) {
    assert(propset_ != nullptr);
    this->template set<id>(values.x1, 0, error_if_missing);
    this->template set<id>(values.y1, 1, error_if_missing);
    this->template set<id>(values.x2, 2, error_if_missing);
    this->template set<id>(values.y2, 3, error_if_missing);
    return *this;
  }

  // For 4-d (RectI) single-type properties.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id,
            std::enable_if_t<PropTraits_t<id>::def.dimension == 4 &&
                                 !PropTraits_t<id>::is_multitype &&
                                 std::is_same_v<typename PropTraits_t<id>::type, int>,
                             int> = 0>
  OfxRectI getRectI(bool error_if_missing = true) const {
    assert(propset_ != nullptr);
    return OfxRectI{get<id>(0, error_if_missing), get<id>(1, error_if_missing),
                    get<id>(2, error_if_missing), get<id>(3, error_if_missing)};
  }

  // For multi-type properties - require explicit ElementType.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id, typename ElementType>
  PropertyAccessor& setAllTyped(const std::initializer_list<ElementType>& values,
                                bool error_if_missing = true) {
    static_assert(PropTraits_t<id>::is_multitype,
                  "This property does not support multiple types. Use "
                  "setAll<PropId>() instead.");
    assert(propset_ != nullptr);

    int index = 0;
    for (const auto& value : values) {
      this->template set<id, ElementType>(value, index++, error_if_missing);
    }

    return *this;
  }

  // Overload for any container with multi-type properties.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id, typename ElementType, typename Container>
  PropertyAccessor& setAllTyped(const Container& values, bool error_if_missing = true) {
    static_assert(PropTraits_t<id>::is_multitype,
                  "This property does not support multiple types. Use "
                  "setAll<PropId>() instead.");
    assert(propset_ != nullptr);

    for (size_t i = 0; i < values.size(); ++i) {
      this->template set<id, ElementType>(values[i], static_cast<int>(i),
                                          error_if_missing);
    }

    return *this;
  }

  // Get dimension of a property.
  // Works with any PropId enum (openfx::PropId or host-defined).
  template <auto id>
  int getDimension(bool error_if_missing = true) const {
    using Traits = PropTraits_t<id>;
    // If dimension is known at compile time, we can just return it
    if constexpr (Traits::def.dimension > 0)
      return Traits::def.dimension;
    else
      return getDimensionRaw(Traits::def.name, error_if_missing);
  }

  // --- Raw access: any property by name, and the suite itself -------------

  // "Escape hatch" for unchecked property access - get any property by name
  // as int, bool, double, const char* or void*
  template <typename T>
  T getRaw(const char* name, int index = 0, bool error_if_missing = true) const {
    return read<T>(name, index, error_if_missing);
  }

  // "Escape hatch" for unchecked property access - set any property by name.
  // An integral value, bool included, goes as an int, a floating-point one as
  // a double, a C string or std::string as a string, and any other pointer as
  // a pointer.
  template <typename T>
  PropertyAccessor& setRaw(const char* name, const T& value, int index = 0,
                           bool error_if_missing = true) {
    write(name, value, index, error_if_missing);
    return *this;
  }

  // The propGet*N calls: the first count values at once, into values, which
  // has room for them; T is int, double, const char* or void*. A soft read of
  // a missing property fills values with the fallback.
  template <typename T>
  void getRawN(const char* name, int count, T* values,
               bool error_if_missing = true) const {
    assert(propset_ != nullptr);
    OfxStatus status = kOfxStatOK;
    if constexpr (std::is_same_v<T, int>)
      status = propSuite_->propGetIntN(propset_, name, count, values);
    else if constexpr (std::is_same_v<T, double>)
      status = propSuite_->propGetDoubleN(propset_, name, count, values);
    else if constexpr (std::is_same_v<T, const char*>)
      status =
          propSuite_->propGetStringN(propset_, name, count, const_cast<char**>(values));
    else if constexpr (std::is_same_v<T, void*>)
      status = propSuite_->propGetPointerN(propset_, name, count, values);
    else
      static_assert(always_false<T>::value, "Unsupported property value type");
    if (!check(status, name, error_if_missing))
      std::fill_n(values, count, fallback<T>());
  }

  // The propSet*N calls: count values at once, from index 0; T is int, double,
  // const char* or void*.
  template <typename T>
  PropertyAccessor& setRawN(const char* name, int count, const T* values,
                            bool error_if_missing = true) {
    assert(propset_ != nullptr);
    OfxStatus status = kOfxStatOK;
    if constexpr (std::is_same_v<T, int>)
      status = propSuite_->propSetIntN(propset_, name, count, values);
    else if constexpr (std::is_same_v<T, double>)
      status = propSuite_->propSetDoubleN(propset_, name, count, values);
    else if constexpr (std::is_same_v<T, const char*>)
      status = propSuite_->propSetStringN(propset_, name, count, values);
    else if constexpr (std::is_same_v<T, void*>)
      status = propSuite_->propSetPointerN(propset_, name, count, values);
    else
      static_assert(always_false<T>::value, "Unsupported property value type");
    check(status, name, error_if_missing);
    return *this;
  }

  // Get raw dimension of a property
  int getDimensionRaw(const char* name, bool error_if_missing = true) const {
    assert(propset_ != nullptr);
    int dimension = 0;
    return check(propSuite_->propGetDimension(propset_, name, &dimension), name,
                 error_if_missing)
               ? dimension
               : fallback<int>();
  }

  // The propReset call: the property back to its default.
  PropertyAccessor& reset(const char* name, bool error_if_missing = true) {
    assert(propset_ != nullptr);
    check(propSuite_->propReset(propset_, name), name, error_if_missing);
    return *this;
  }

  template <auto id>
  PropertyAccessor& reset(bool error_if_missing = true) {
    return reset(PropTraits_t<id>::def.name, error_if_missing);
  }

  // Whether this property set has the property, asked of the suite at run
  // time. Any failure but its absence throws, as from a soft read. Not to be
  // confused with prop::exists<id>() below, which is a compile-time constant.
  bool exists(const char* name) const {
    assert(propset_ != nullptr);
    int dimension = 0;
    return check(propSuite_->propGetDimension(propset_, name, &dimension), name, false);
  }

  template <auto id>
  bool exists() const {
    return exists(PropTraits_t<id>::def.name);
  }

  // The property set this accessor reads and writes.
  OfxPropertySetHandle handle() const { return propset_; }

  // The property suite this accessor calls, for a call it does not wrap.
  const OfxPropertySuiteV1* suite() const { return propSuite_; }

 private:
  // For the constructors that ask the host for the property set: a failure
  // throws its status, and kOfxStatOK without a property set is a bad handle.
  void requirePropSet(OfxStatus status, const char* call) const {
    if (status == kOfxStatOK && !propset_)
      status = kOfxStatErrBadHandle;
    if (status != kOfxStatOK)
      throw OfxException(status, call);
  }

  // What a soft read of a property the set does not have returns; the table
  // above the class lists it.
  template <typename T>
  static T fallback() {
    if constexpr (std::is_same_v<T, const char*>)
      return "";
    else
      return T{};
  }

  // True if a suite call succeeded, false if it was soft and the set has no
  // such property. Any other failure throws, PropertyNotFoundException for a
  // property the set does not have, with the property and the status in the
  // message, and a string write's value too.
  static bool check(OfxStatus status, const char* name, bool error_if_missing,
                    const char* value = nullptr) {
    if (status == kOfxStatOK)
      return true;
    if (status == kOfxStatErrUnknown && !error_if_missing)
      return false;
    std::string what = name ? name : "(null)";
    if (value)
      what.append("=").append(value);
    if (status == kOfxStatErrUnknown)
      throw PropertyNotFoundException(status, what);
    throw OfxException(status, what);
  }

  // One value, through the propGet call for its type; a bool is read as an int.
  template <typename T>
  T read(const char* name, int index, bool error_if_missing) const {
    assert(propset_ != nullptr);
    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, bool>) {
      int value = 0;
      if (check(propSuite_->propGetInt(propset_, name, index, &value), name,
                error_if_missing))
        return static_cast<T>(value);
    } else if constexpr (std::is_same_v<T, double>) {
      double value = 0;
      if (check(propSuite_->propGetDouble(propset_, name, index, &value), name,
                error_if_missing))
        return value;
    } else if constexpr (std::is_same_v<T, const char*>) {
      char* value = nullptr;
      if (check(propSuite_->propGetString(propset_, name, index, &value), name,
                error_if_missing))
        return value;
    } else if constexpr (std::is_same_v<T, void*>) {
      void* value = nullptr;
      if (check(propSuite_->propGetPointer(propset_, name, index, &value), name,
                error_if_missing))
        return value;
    } else {
      static_assert(always_false<T>::value, "Unsupported property value type");
    }
    return fallback<T>();
  }

  // A value as the C API takes it: an integral type, bool included, as int, a
  // floating-point one as double, a string as const char* and any other
  // pointer as void*.
  template <typename T>
  static auto cValue(const T& value) {
    using V = std::decay_t<T>;
    if constexpr (std::is_same_v<V, std::string>)
      return value.c_str();
    else if constexpr (std::is_same_v<V, const char*> || std::is_same_v<V, char*>)
      return static_cast<const char*>(value);
    else if constexpr (std::is_integral_v<V>)
      return static_cast<int>(value);
    else if constexpr (std::is_floating_point_v<V>)
      return static_cast<double>(value);
    else if constexpr (std::is_convertible_v<V, void*>)
      return static_cast<void*>(value);
    else
      static_assert(always_false<T>::value,
                    "A property value is a number, a string or a pointer");
  }

  // One value, through the propSet call for its C type.
  template <typename T>
  void write(const char* name, const T& value, int index, bool error_if_missing) {
    assert(propset_ != nullptr);
    const auto c = cValue(value);
    using C = std::remove_const_t<decltype(c)>;
    if constexpr (std::is_same_v<C, int>)
      check(propSuite_->propSetInt(propset_, name, index, c), name, error_if_missing);
    else if constexpr (std::is_same_v<C, double>)
      check(propSuite_->propSetDouble(propset_, name, index, c), name, error_if_missing);
    else if constexpr (std::is_same_v<C, const char*>)
      check(propSuite_->propSetString(propset_, name, index, c), name, error_if_missing,
            c);
    else
      check(propSuite_->propSetPointer(propset_, name, index, c), name, error_if_missing);
  }

  OfxPropertySetHandle propset_;
  const OfxPropertySuiteV1* propSuite_;

  // Helper for static_assert to fail compilation for unsupported types
  template <typename>
  struct always_false : std::false_type {};
};

// Compile-time questions about a PropId.
namespace prop {

// Always true: it compiles only for a PropId (openfx::PropId or
// host-defined), and every PropId is a property the metadata declares. It
// says nothing about whether a property set has the property;
// PropertyAccessor::exists<id>() asks the suite that at run time.
template <auto id>
constexpr bool exists() {
  return true;  // All PropId values are valid by definition
}

// Helper to check if a property supports a specific C++ type.
// Works with any PropId enum (openfx::PropId or host-defined).
template <auto id, typename T>
constexpr bool supportsType() {
  constexpr auto supportedTypes = PropTraits_t<id>::def.supportedTypes;

  for (const auto& type : supportedTypes) {
    if constexpr (std::is_same_v<T, int>) {
      if (type == PropType::Int || type == PropType::Bool || type == PropType::Enum)
        return true;
    } else if constexpr (std::is_same_v<T, double>) {
      if (type == PropType::Double)
        return true;
    } else if constexpr (std::is_same_v<T, const char*>) {
      if (type == PropType::String || type == PropType::Enum)
        return true;
    } else if constexpr (std::is_same_v<T, void*>) {
      if (type == PropType::Pointer)
        return true;
    }
  }
  return false;
}
}  // namespace prop

}  // namespace openfx
