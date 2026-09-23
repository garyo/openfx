# Host-Extensible Property System

This directory contains examples showing how OpenFX hosts can define custom properties that plugins can access with full type safety, without modifying OpenFX core code.

## Overview

The OpenFX C++ property system supports **host-extensible properties** through C++17 `auto` template parameters and argument-dependent lookup (ADL). This allows hosts to:

- Define custom properties in their own namespace
- Provide the same type safety as standard OpenFX properties
- Maintain zero coupling with OpenFX core code
- Give plugins optional access to host-specific features

Hosts just need to ship a `HOST-props.yml` and the corresponding generated header with the plugins in a unique namespace. Plugins include the generated header and get the same type safety provided by the built-in property accessors. The  `gen-props.py` script takes a `host-metadata` command to generate the props metadata header from a HOST-props.yml.

### Key Mechanism: ADL (Argument-Dependent Lookup)

The `PropertyAccessor` uses `template<auto id>` to accept any enum type:

```cpp
template<auto id>
auto get() {
  using Traits = PropTraits_t<id>;  // Uses ADL to find correct namespace
  // ...
}
```

The `PropTraits_t` type alias uses decltype + ADL:

```cpp
template<auto id>
using PropTraits_t = decltype(
  prop_traits_helper(std::integral_constant<decltype(id), id>{})
);
```

Each namespace provides a `prop_traits_helper` function for its `PropId` enum:

```cpp
// In openfx namespace:
template<PropId id>
properties::PropTraits<id> prop_traits_helper(std::integral_constant<PropId, id>);

// In myhost namespace:
template<PropId id>
properties::PropTraits<id> prop_traits_helper(std::integral_constant<PropId, id>);
```

When the compiler sees `prop_traits_helper(std::integral_constant<myhost::PropId, ...>{})`, ADL finds the `myhost::prop_traits_helper` function because the argument type (`std::integral_constant<myhost::PropId, ...>`) is associated with the `myhost` namespace.

## For Hosts: Defining Custom Properties

### Step 1: Define Properties in YAML

Create a YAML file following the OpenFX property schema:

```yaml
# myhost-props.yml
properties:
  MyHostViewerProcess:
    name: "com.example.myhost.ViewerProcess"
    type: string
    dimension: 1
    description: "MyHost viewer process name (color management display transform)"
    valid_for:
      - "Effect Descriptor"
      - "Effect Instance"
```

An `enum` property also lists its `values`, which plugins can read through
`openfx::EnumValue<id>` and check with `EnumValue<id>::isValid()`:

```yaml
  MyHostRenderQuality:
    name: "com.example.myhost.RenderQuality"
    type: enum
    dimension: 1
    values:
      - "com.example.myhost.RenderQualityDraft"
      - "com.example.myhost.RenderQualityFinal"
```

**Naming Convention**: Use reverse-DNS notation:
- `com.company.product.PropertyName`
- Examples: `com.foundry.myhost.ViewerProcess`, `com.blackmagic.resolve.Timeline`

### Step 2: Generate Metadata Header

Use the OpenFX `gen-props.py` script:

```bash
python scripts/gen-props.py host-metadata \
  myhost-props.yml \
  -o myhost/myhostPropsMetadata.h \
  -n myhost
```

Besides the C++ metadata, the header defines a C name for each property, as
the OFX headers do for theirs: `#define kMyHostViewerProcess
"com.example.myhost.ViewerProcess"`. A property's `cname:` key overrides
the name, which is otherwise `k` and its key. Each definition is guarded by
`#ifndef`, so a host C header that defines the same names can come first;
a `static_assert` checks that the two agree.

### Step 3: Distribute Header to Plugin Developers

Provide the generated header file to plugin developers in your SDK:

```
MyHostSDK/
  include/
    myhost/
      myhostPropsMetadata.h
```

## For Plugin Developers: Using Host Properties

### Step 1: Include Host Header

```cpp
#include <openfx/ofxPropsAccess.h>     // OpenFX core
#include <myhost/myhostPropsMetadata.h> // Host-specific
```

### Step 2: Use Properties with Type Safety

```cpp
using namespace openfx;

void describe(OfxImageEffectHandle effect, const SuiteContainer& suites) {
  PropertyAccessor props(effect, suites);

  // Standard OpenFX property
  props.set<PropId::OfxPropLabel>("My Effect");

  // Host-specific property (fully qualified). A string comes back as a
  // CStringView, which == compares by content.
  try {
    auto value = props.get<myhost::PropId::MyHostViewerProcess>();
    Logger::info("Host property value: {}", value);
  } catch (const PropertyNotFoundException&) {
    // Not running in MyHost, or property not supported
  }
}
```

### Step 3: Handle Missing Properties Gracefully

Host properties do not exist when running in other hosts, and every call is
strict: reading or writing one another host lacks throws
`PropertyNotFoundException`. To ask for one that may be missing:

```cpp
// Method 1: soft(), a copy of the accessor that gives a property the set does
// not have as 0, 0.0, "" or nullptr for its type, and ignores a write of one
CStringView path = props.soft().get<myhost::PropId::MyHostProjectPath>();
if (!path.empty()) {
  // Use the value
}

// Method 2: find(), which is std::nullopt for a missing property, to tell it
// from an empty one
if (auto name = props.find<myhost::PropId::MyHostNodeName>()) {
  // Use *name
}

// Method 3: ask first
if (props.exists<myhost::PropId::MyHostNodeColor>()) {
  auto colour = props.getAll<myhost::PropId::MyHostNodeColor>();
  // Use the value
}
```

`soft()` and `find()` forgive a missing property only: any other failure,
such as a bad handle or a value of the wrong type, throws either way.

## Examples

The `myhost/` directory provides a template for hosts to follow, and
`example-usage.cpp` shows a plugin using its properties.
