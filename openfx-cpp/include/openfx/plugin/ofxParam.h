// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Plugin-side wrappers over OfxParameterSuiteV1: a parameter set, and one
// class per parameter type whose getValue/setValue pass exactly the C types
// the suite's varargs entry points expect.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxPropsAccess.h"
#include "openfx/ofxSuites.h"
#include "openfx/plugin/ofxPropSetAccessors.h"

namespace openfx::plugin {

namespace detail {

// The two handles every parameter wrapper needs, fetched together.
struct ParamHandles {
  OfxParamHandle param;
  OfxPropertySetHandle propSet;
};

inline const OfxParameterSuiteV1* requireParamSuite(const SuiteContainer& suites) {
  const auto* suite = suites.get<OfxParameterSuiteV1>();
  if (!suite)
    throw SuiteNotFoundException(kOfxStatErrMissingHostFeature, kOfxParameterSuite);
  return suite;
}

inline void checkParamStatus(OfxStatus status, const char* what) {
  if (status != kOfxStatOK)
    throw OfxException(status, what);
}

inline ParamHandles fetchParam(const OfxParameterSuiteV1* suite, OfxParamSetHandle set,
                               std::string_view name) {
  ParamHandles handles{};
  checkParamStatus(suite->paramGetHandle(set, std::string(name).c_str(), &handles.param,
                                         &handles.propSet),
                   "paramGetHandle");
  return handles;
}

inline ParamHandles paramPropSet(const OfxParameterSuiteV1* suite, OfxParamHandle param) {
  ParamHandles handles{param, nullptr};
  checkParamStatus(suite->paramGetPropertySet(param, &handles.propSet),
                   "paramGetPropertySet");
  return handles;
}

}  // namespace detail

// A parameter of any type: its handle, its property set and the keyframe and
// copy operations that do not depend on the value type. The handles belong to
// the effect, so nothing is released here.
class ParamBase {
 public:
  // Fetch a parameter instance by name from a parameter set.
  ParamBase(OfxParamSetHandle set, std::string_view name, const SuiteContainer& suites)
      : ParamBase(detail::fetchParam(detail::requireParamSuite(suites), set, name),
                  suites) {}

  // Wrap a parameter handle the caller already has.
  ParamBase(OfxParamHandle param, const SuiteContainer& suites)
      : ParamBase(detail::paramPropSet(detail::requireParamSuite(suites), param),
                  suites) {}

  OfxParamHandle handle() const { return param_; }
  OfxPropertySetHandle propSetHandle() const { return propSet_; }

  // The property accessor lives in this object; anything built on it, such as
  // the typed accessor from a derived class, must not outlive the parameter.
  PropertyAccessor& props() { return props_; }
  const PropertyAccessor& props() const { return props_; }

  const char* name() const { return props_.get<PropId::OfxPropName>(); }

  // The kOfxParamType* string this parameter was defined with.
  const char* type() const { return props_.get<PropId::OfxParamPropType>(); }

  unsigned numKeys() const {
    unsigned n = 0;
    detail::checkParamStatus(paramSuite_->paramGetNumKeys(param_, &n), "paramGetNumKeys");
    return n;
  }

  OfxTime keyTime(unsigned index) const {
    OfxTime time = 0;
    detail::checkParamStatus(paramSuite_->paramGetKeyTime(param_, index, &time),
                             "paramGetKeyTime");
    return time;
  }

  // Index of the key at (direction 0), after (> 0) or before (< 0) `time`,
  // or -1 if there is none.
  int keyIndex(OfxTime time, int direction) const {
    int index = -1;
    OfxStatus status = paramSuite_->paramGetKeyIndex(param_, time, direction, &index);
    if (status == kOfxStatFailed)
      return -1;
    detail::checkParamStatus(status, "paramGetKeyIndex");
    return index;
  }

  void deleteKey(OfxTime time) {
    detail::checkParamStatus(paramSuite_->paramDeleteKey(param_, time), "paramDeleteKey");
  }

  void deleteAllKeys() {
    detail::checkParamStatus(paramSuite_->paramDeleteAllKeys(param_),
                             "paramDeleteAllKeys");
  }

  // Copy value and animation from another parameter of the same type. `range`
  // limits which keys are copied; null copies all of them.
  void copyFrom(const ParamBase& from, OfxTime offset, const OfxRangeD* range = nullptr) {
    detail::checkParamStatus(paramSuite_->paramCopy(param_, from.param_, offset, range),
                             "paramCopy");
  }

 protected:
  // Value access. Each typed parameter calls these with exactly the C types
  // the suite's varargs expect: double*, int* or char** to read, and double,
  // int or const char* to write.
  template <typename... Args>
  void getValues(Args*... out) const {
    detail::checkParamStatus(paramSuite_->paramGetValue(param_, out...), "paramGetValue");
  }

  template <typename... Args>
  void getValuesAtTime(OfxTime time, Args*... out) const {
    detail::checkParamStatus(paramSuite_->paramGetValueAtTime(param_, time, out...),
                             "paramGetValueAtTime");
  }

  template <typename... Args>
  void getDerivatives(OfxTime time, Args*... out) const {
    detail::checkParamStatus(paramSuite_->paramGetDerivative(param_, time, out...),
                             "paramGetDerivative");
  }

  template <typename... Args>
  void getIntegrals(OfxTime time1, OfxTime time2, Args*... out) const {
    detail::checkParamStatus(paramSuite_->paramGetIntegral(param_, time1, time2, out...),
                             "paramGetIntegral");
  }

  template <typename... Args>
  void setValues(Args... values) {
    detail::checkParamStatus(paramSuite_->paramSetValue(param_, values...),
                             "paramSetValue");
  }

  template <typename... Args>
  void setValuesAtTime(OfxTime time, Args... values) {
    detail::checkParamStatus(paramSuite_->paramSetValueAtTime(param_, time, values...),
                             "paramSetValueAtTime");
  }

 private:
  ParamBase(detail::ParamHandles handles, const SuiteContainer& suites)
      : paramSuite_(detail::requireParamSuite(suites)), param_(handles.param),
        propSet_(handles.propSet), props_(handles.propSet, suites) {}

  const OfxParameterSuiteV1* paramSuite_;
  OfxParamHandle param_;
  OfxPropertySetHandle propSet_;
  PropertyAccessor props_;
};

// A parameter of a known type: adds the generated property accessor for the
// property set that type uses.
template <class AccessorT>
class TypedParam : public ParamBase {
 public:
  using Accessor = AccessorT;
  using ParamBase::ParamBase;

  // Typed view of this parameter's properties, valid while this object is.
  Accessor accessor() { return Accessor(props()); }
};

class DoubleParam : public TypedParam<propsets::ParamsDouble1D> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypeDouble;

  double getValue() const {
    double v = 0;
    getValues(&v);
    return v;
  }
  double getValueAtTime(OfxTime time) const {
    double v = 0;
    getValuesAtTime(time, &v);
    return v;
  }
  void setValue(double v) { setValues(v); }
  void setValueAtTime(OfxTime time, double v) { setValuesAtTime(time, v); }
  double getDerivative(OfxTime time) const {
    double v = 0;
    getDerivatives(time, &v);
    return v;
  }
  double getIntegral(OfxTime time1, OfxTime time2) const {
    double v = 0;
    getIntegrals(time1, time2, &v);
    return v;
  }
};

class Double2DParam : public TypedParam<propsets::ParamsDouble2D3D> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypeDouble2D;

  OfxPointD getValue() const {
    OfxPointD v{};
    getValues(&v.x, &v.y);
    return v;
  }
  OfxPointD getValueAtTime(OfxTime time) const {
    OfxPointD v{};
    getValuesAtTime(time, &v.x, &v.y);
    return v;
  }
  void setValue(OfxPointD v) { setValues(v.x, v.y); }
  void setValueAtTime(OfxTime time, OfxPointD v) { setValuesAtTime(time, v.x, v.y); }
  OfxPointD getDerivative(OfxTime time) const {
    OfxPointD v{};
    getDerivatives(time, &v.x, &v.y);
    return v;
  }
  OfxPointD getIntegral(OfxTime time1, OfxTime time2) const {
    OfxPointD v{};
    getIntegrals(time1, time2, &v.x, &v.y);
    return v;
  }
};

class Double3DParam : public TypedParam<propsets::ParamsDouble2D3D> {
 public:
  using TypedParam::TypedParam;
  using Value = std::array<double, 3>;
  static constexpr const char* kParamType = kOfxParamTypeDouble3D;

  Value getValue() const {
    Value v{};
    getValues(&v[0], &v[1], &v[2]);
    return v;
  }
  Value getValueAtTime(OfxTime time) const {
    Value v{};
    getValuesAtTime(time, &v[0], &v[1], &v[2]);
    return v;
  }
  void setValue(const Value& v) { setValues(v[0], v[1], v[2]); }
  void setValueAtTime(OfxTime time, const Value& v) {
    setValuesAtTime(time, v[0], v[1], v[2]);
  }
  Value getDerivative(OfxTime time) const {
    Value v{};
    getDerivatives(time, &v[0], &v[1], &v[2]);
    return v;
  }
  Value getIntegral(OfxTime time1, OfxTime time2) const {
    Value v{};
    getIntegrals(time1, time2, &v[0], &v[1], &v[2]);
    return v;
  }
};

class IntParam : public TypedParam<propsets::ParamsByte> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypeInteger;

  int getValue() const {
    int v = 0;
    getValues(&v);
    return v;
  }
  int getValueAtTime(OfxTime time) const {
    int v = 0;
    getValuesAtTime(time, &v);
    return v;
  }
  void setValue(int v) { setValues(v); }
  void setValueAtTime(OfxTime time, int v) { setValuesAtTime(time, v); }
};

class Int2DParam : public TypedParam<propsets::ParamsInt2D3D> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypeInteger2D;

  OfxPointI getValue() const {
    OfxPointI v{};
    getValues(&v.x, &v.y);
    return v;
  }
  OfxPointI getValueAtTime(OfxTime time) const {
    OfxPointI v{};
    getValuesAtTime(time, &v.x, &v.y);
    return v;
  }
  void setValue(OfxPointI v) { setValues(v.x, v.y); }
  void setValueAtTime(OfxTime time, OfxPointI v) { setValuesAtTime(time, v.x, v.y); }
};

class Int3DParam : public TypedParam<propsets::ParamsInt2D3D> {
 public:
  using TypedParam::TypedParam;
  using Value = std::array<int, 3>;
  static constexpr const char* kParamType = kOfxParamTypeInteger3D;

  Value getValue() const {
    Value v{};
    getValues(&v[0], &v[1], &v[2]);
    return v;
  }
  Value getValueAtTime(OfxTime time) const {
    Value v{};
    getValuesAtTime(time, &v[0], &v[1], &v[2]);
    return v;
  }
  void setValue(const Value& v) { setValues(v[0], v[1], v[2]); }
  void setValueAtTime(OfxTime time, const Value& v) {
    setValuesAtTime(time, v[0], v[1], v[2]);
  }
};

// Booleans travel through the suite as ints.
class BooleanParam : public TypedParam<propsets::ParamsByte> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypeBoolean;

  bool getValue() const {
    int v = 0;
    getValues(&v);
    return v != 0;
  }
  bool getValueAtTime(OfxTime time) const {
    int v = 0;
    getValuesAtTime(time, &v);
    return v != 0;
  }
  void setValue(bool v) { setValues(v ? 1 : 0); }
  void setValueAtTime(OfxTime time, bool v) { setValuesAtTime(time, v ? 1 : 0); }
};

class ChoiceParam : public TypedParam<propsets::ParamsChoice> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypeChoice;

  int getValue() const {
    int v = 0;
    getValues(&v);
    return v;
  }
  int getValueAtTime(OfxTime time) const {
    int v = 0;
    getValuesAtTime(time, &v);
    return v;
  }
  void setValue(int v) { setValues(v); }
  void setValueAtTime(OfxTime time, int v) { setValuesAtTime(time, v); }
};

class StrChoiceParam : public TypedParam<propsets::ParamsStrChoice> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypeStrChoice;

  std::string getValue() const {
    char* v = nullptr;
    getValues(&v);
    return v ? v : "";
  }
  std::string getValueAtTime(OfxTime time) const {
    char* v = nullptr;
    getValuesAtTime(time, &v);
    return v ? v : "";
  }
  void setValue(const std::string& v) { setValues(v.c_str()); }
  void setValueAtTime(OfxTime time, const std::string& v) {
    setValuesAtTime(time, v.c_str());
  }
};

class RGBParam : public TypedParam<propsets::ParamsRGB> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypeRGB;

  OfxRGBColourD getValue() const {
    OfxRGBColourD v{};
    getValues(&v.r, &v.g, &v.b);
    return v;
  }
  OfxRGBColourD getValueAtTime(OfxTime time) const {
    OfxRGBColourD v{};
    getValuesAtTime(time, &v.r, &v.g, &v.b);
    return v;
  }
  void setValue(const OfxRGBColourD& v) { setValues(v.r, v.g, v.b); }
  void setValueAtTime(OfxTime time, const OfxRGBColourD& v) {
    setValuesAtTime(time, v.r, v.g, v.b);
  }
  OfxRGBColourD getDerivative(OfxTime time) const {
    OfxRGBColourD v{};
    getDerivatives(time, &v.r, &v.g, &v.b);
    return v;
  }
  OfxRGBColourD getIntegral(OfxTime time1, OfxTime time2) const {
    OfxRGBColourD v{};
    getIntegrals(time1, time2, &v.r, &v.g, &v.b);
    return v;
  }
};

class RGBAParam : public TypedParam<propsets::ParamsRGBA> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypeRGBA;

  OfxRGBAColourD getValue() const {
    OfxRGBAColourD v{};
    getValues(&v.r, &v.g, &v.b, &v.a);
    return v;
  }
  OfxRGBAColourD getValueAtTime(OfxTime time) const {
    OfxRGBAColourD v{};
    getValuesAtTime(time, &v.r, &v.g, &v.b, &v.a);
    return v;
  }
  void setValue(const OfxRGBAColourD& v) { setValues(v.r, v.g, v.b, v.a); }
  void setValueAtTime(OfxTime time, const OfxRGBAColourD& v) {
    setValuesAtTime(time, v.r, v.g, v.b, v.a);
  }
  OfxRGBAColourD getDerivative(OfxTime time) const {
    OfxRGBAColourD v{};
    getDerivatives(time, &v.r, &v.g, &v.b, &v.a);
    return v;
  }
  OfxRGBAColourD getIntegral(OfxTime time1, OfxTime time2) const {
    OfxRGBAColourD v{};
    getIntegrals(time1, time2, &v.r, &v.g, &v.b, &v.a);
    return v;
  }
};

class StringParam : public TypedParam<propsets::ParamsString> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypeString;

  std::string getValue() const {
    char* v = nullptr;
    getValues(&v);
    return v ? v : "";
  }
  std::string getValueAtTime(OfxTime time) const {
    char* v = nullptr;
    getValuesAtTime(time, &v);
    return v ? v : "";
  }
  void setValue(const std::string& v) { setValues(v.c_str()); }
  void setValueAtTime(OfxTime time, const std::string& v) {
    setValuesAtTime(time, v.c_str());
  }
};

class CustomParam : public TypedParam<propsets::ParamsCustom> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypeCustom;

  std::string getValue() const {
    char* v = nullptr;
    getValues(&v);
    return v ? v : "";
  }
  std::string getValueAtTime(OfxTime time) const {
    char* v = nullptr;
    getValuesAtTime(time, &v);
    return v ? v : "";
  }
  void setValue(const std::string& v) { setValues(v.c_str()); }
  void setValueAtTime(OfxTime time, const std::string& v) {
    setValuesAtTime(time, v.c_str());
  }
};

// Parameters with no value of their own.
class PushButtonParam : public TypedParam<propsets::ParamsByte> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypePushButton;
};

class GroupParam : public TypedParam<propsets::ParamsGroup> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypeGroup;
};

class PageParam : public TypedParam<propsets::ParamsPage> {
 public:
  using TypedParam::TypedParam;
  static constexpr const char* kParamType = kOfxParamTypePage;
};

// An effect's parameter set: defines parameters in the describe actions and
// fetches typed parameter instances afterwards.
//
// The define* helpers return a property accessor over a PropertyAccessor this
// object owns, so keep the ParamSet alive while the accessor is in use.
class ParamSet {
 public:
  ParamSet(OfxImageEffectHandle effect, const SuiteContainer& suites)
      : ParamSet(fetchParamSet(suites, effect), suites) {}

  ParamSet(OfxParamSetHandle set, const SuiteContainer& suites)
      : suites_(&suites), paramSuite_(detail::requireParamSuite(suites)), set_(set),
        props_(fetchPropSet(paramSuite_, set), suites) {}

  OfxParamSetHandle handle() const { return set_; }
  const OfxParameterSuiteV1* suite() const { return paramSuite_; }

  // The parameter set's property set, which is the effect instance's.
  PropertyAccessor& props() { return props_; }

  // Fetch a parameter instance, e.g. params.get<DoubleParam>("scale").
  template <class P>
  P get(std::string_view name) const {
    return P(set_, name, *suites_);
  }

  // Define a new parameter and return the typed accessor for its descriptor.
  template <class P>
  typename P::Accessor define(std::string_view name) {
    OfxPropertySetHandle propSet = nullptr;
    detail::checkParamStatus(
        paramSuite_->paramDefine(set_, P::kParamType, std::string(name).c_str(),
                                 &propSet),
        "paramDefine");
    descriptors_.push_back(std::make_unique<PropertyAccessor>(propSet, *suites_));
    return typename P::Accessor(*descriptors_.back());
  }

  propsets::ParamsDouble1D defineDouble(std::string_view name) {
    return define<DoubleParam>(name);
  }
  propsets::ParamsDouble2D3D defineDouble2D(std::string_view name) {
    return define<Double2DParam>(name);
  }
  propsets::ParamsDouble2D3D defineDouble3D(std::string_view name) {
    return define<Double3DParam>(name);
  }
  propsets::ParamsByte defineInt(std::string_view name) { return define<IntParam>(name); }
  propsets::ParamsInt2D3D defineInt2D(std::string_view name) {
    return define<Int2DParam>(name);
  }
  propsets::ParamsInt2D3D defineInt3D(std::string_view name) {
    return define<Int3DParam>(name);
  }
  propsets::ParamsByte defineBoolean(std::string_view name) {
    return define<BooleanParam>(name);
  }
  propsets::ParamsChoice defineChoice(std::string_view name) {
    return define<ChoiceParam>(name);
  }
  propsets::ParamsStrChoice defineStrChoice(std::string_view name) {
    return define<StrChoiceParam>(name);
  }
  propsets::ParamsRGB defineRGB(std::string_view name) { return define<RGBParam>(name); }
  propsets::ParamsRGBA defineRGBA(std::string_view name) {
    return define<RGBAParam>(name);
  }
  propsets::ParamsString defineString(std::string_view name) {
    return define<StringParam>(name);
  }
  propsets::ParamsCustom defineCustom(std::string_view name) {
    return define<CustomParam>(name);
  }
  propsets::ParamsByte definePushButton(std::string_view name) {
    return define<PushButtonParam>(name);
  }
  propsets::ParamsGroup defineGroup(std::string_view name) {
    return define<GroupParam>(name);
  }
  propsets::ParamsPage definePage(std::string_view name) {
    return define<PageParam>(name);
  }

  // Group parameter changes into one undo/redo block.
  void editBegin(std::string_view label) {
    detail::checkParamStatus(
        paramSuite_->paramEditBegin(set_, std::string(label).c_str()), "paramEditBegin");
  }
  void editEnd() {
    detail::checkParamStatus(paramSuite_->paramEditEnd(set_), "paramEditEnd");
  }

  // RAII form of editBegin/editEnd.
  class EditScope {
   public:
    EditScope(ParamSet& set, std::string_view label) : set_(&set) {
      set_->editBegin(label);
    }
    // A destructor cannot report a failure, so the status is dropped here;
    // call editEnd() directly when it matters.
    ~EditScope() {
      if (set_)
        static_cast<void>(set_->suite()->paramEditEnd(set_->handle()));
    }
    EditScope(const EditScope&) = delete;
    EditScope& operator=(const EditScope&) = delete;
    EditScope(EditScope&& other) noexcept : set_(other.set_) { other.set_ = nullptr; }
    EditScope& operator=(EditScope&&) = delete;

   private:
    ParamSet* set_;
  };

  EditScope editScope(std::string_view label) { return EditScope(*this, label); }

 private:
  static OfxParamSetHandle fetchParamSet(const SuiteContainer& suites,
                                         OfxImageEffectHandle effect) {
    const auto* effectSuite = suites.get<OfxImageEffectSuiteV1>();
    if (!effectSuite)
      throw SuiteNotFoundException(kOfxStatErrMissingHostFeature, kOfxImageEffectSuite);
    OfxParamSetHandle set = nullptr;
    detail::checkParamStatus(effectSuite->getParamSet(effect, &set), "getParamSet");
    return set;
  }

  static OfxPropertySetHandle fetchPropSet(const OfxParameterSuiteV1* suite,
                                           OfxParamSetHandle set) {
    OfxPropertySetHandle propSet = nullptr;
    detail::checkParamStatus(suite->paramSetGetPropertySet(set, &propSet),
                             "paramSetGetPropertySet");
    return propSet;
  }

  const SuiteContainer* suites_;
  const OfxParameterSuiteV1* paramSuite_;
  OfxParamSetHandle set_;
  PropertyAccessor props_;
  // Accessors for the parameters defined through this object. unique_ptr keeps
  // their addresses stable as the vector grows.
  std::vector<std::unique_ptr<PropertyAccessor>> descriptors_;
};

}  // namespace openfx::plugin
