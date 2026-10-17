<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- Copyright OpenFX and contributors to the OpenFX project. -->
# OpenFX string lifetime test kit

[OpenFX issue #273](https://github.com/AcademySoftwareFoundation/openfx/issues/273)
proposes tightening how long a string passed between a host and a plugin
stays valid. The rules are still under discussion and not part of the
standard. This kit lets host and plugin developers try the proposals on their
own software and report what breaks, or what would be hard to support, on the
issue.

It is a preview build of two test tools from the OpenFX repository:
`ofxtesthost`, a command-line host, and `TestProps`, a plugin that reads
every property and parameter a host offers. `BUILD-INFO.txt` gives the commit
they were built from.

## The proposed rules

A *string* here is any `char*` the host returns from the property or
parameter suites, and the data of a bytes parameter's value. An *entry call*
is a call from the host into the plugin: an action, `setHost`, an interact's
action, or a multithread thread function. Each rule says how long a string
the host returns stays valid:

| Rule | Until |
|---|---|
| `entry-call` | the entry call it was returned in returns, or the plugin changes that value |
| `same-value` | as `entry-call`, or the plugin reads that same value again |
| `next-call` | the plugin's next suite call on that thread that returns a string, or the entry call returns |

On a thread with no entry call in progress, such as one the plugin started
itself, a string lasts until the next suite call on that thread that returns
a string. Under every rule, a host copies any string the plugin passes it
before the call returns, rather than keeping the plugin's pointer.

## What's in the kit

| Path | What it is |
|---|---|
| `bin/ofxtesthost` | The test host, for testing a plugin |
| `plugins/example-TestProps.ofx.bundle` | The test plugin, for testing a host |
| `plugins-asan/example-TestProps-asan.ofx.bundle` | The same plugin built with AddressSanitizer |
| `asan/` | The AddressSanitizer runtime that build needs |
| `config/ofxtest.toml` | A sample configuration file |

On macOS, files downloaded through a browser are quarantined and will not
run. Clear that once after unpacking:

```sh
xattr -dr com.apple.quarantine ofx-string-test-macos-universal
```

On Windows the programs need the Microsoft Visual C++ 2015-2022
redistributable, which most machines already have.

## Testing a plugin

Run your plugin in `ofxtesthost` under one of the rules:

```sh
bin/ofxtesthost --string-lifetime next-call /path/to/YourPlugin.ofx.bundle
```

The host describes the plugin, creates an instance, renders a 64x64 ramp
through it and unloads it. Under a rule, every string it returns is a copy in
memory of its own, and when the rule says the string has expired that memory
is made inaccessible. A plugin that reads a string after that crashes at the
read, and the host prints the plugin, the action and a backtrace. A plugin
that passes an expired string back to the host, for instance by setting a
property to it, has the call refused, with a warning. A run that completes
with no such output means the plugin respects that rule, at least on the
paths this run took.

Try each rule, strictest first: `next-call`, `same-value`, `entry-call`. To
cover more of the plugin, use `--plugin ID` to pick one from a bundle of
several, `--param NAME=VALUE` to set parameters (which also sends
InstanceChanged), `--frames 0-9` for a sequence, `--interact` with `--pen` and
`--draw` to drive an overlay, `--verbose` to trace every action, and
`--help` for the rest. Inputs can be PPM or PFM files (`--in`), and
`--out` writes the result.

## Testing a host

TestProps checks the host instead. Install
`plugins/example-TestProps.ofx.bundle` where your host finds plugins, or add
the `plugins` directory to `OFX_PLUGIN_PATH`. Edit `config/ofxtest.toml` to
choose the rule to check, then set `OPENFX_TEST_CONFIG` to the file's full
path in the environment your host starts in:

```sh
export OPENFX_TEST_CONFIG=/path/to/kit/config/ofxtest.toml
/path/to/YourHost
```

In the host, apply "Property Tester V2" (in the "OFX Examples" group) to a
clip, change its parameters and render. TestProps logs every property it
reads; to see only the audit, set `log = "warning"`. Use `log-file` if your
host's standard output goes nowhere.

For each string the host returns, TestProps keeps a copy and, when the rule
lets the string expire, checks that the host's string still reads the same.
Each one that doesn't is logged as an error naming the property or
parameter, the action, and what the memory holds instead, or that it could
not be read at all. When the plugin is unloaded it logs a summary:

```
string audit (entry-call): 202 strings checked, 0 expired early, 0 kept by pointer
```

TestProps also passes each of its own string values to the host as a
temporary copy, which is made inaccessible as soon as the call returns. A host
that kept the pointer instead of copying the string crashes when it reads it
later, inside the host's own code, and one that hands that pointer back to
the plugin is reported.

### With AddressSanitizer

The audit compares the string's bytes, so a host that frees a string too
early but leaves the memory unchanged can still pass. The AddressSanitizer
build of TestProps catches that as well: reading the freed string is
reported as a heap use after free, with the stack where the host freed it.

The host itself need not be built with AddressSanitizer, but on macOS and
Linux it must be started with the sanitizer runtime already loaded; loaded
into a host started without it, the plugin doesn't just fail to load, it
stops the host. On Windows the runtime need only be where the host can find
it, and without it the plugin fails to load. Install
`plugins-asan/example-TestProps-asan.ofx.bundle` instead of the plain one
(they have the same plugin identifier, so not both), and start the host with
the runtime from the kit's `asan` directory:

- **macOS:** `DYLD_INSERT_LIBRARIES=/path/to/kit/asan/libclang_rt.asan_osx_dynamic.dylib /path/to/YourHost.app/Contents/MacOS/YourHost`.
  macOS ignores `DYLD_INSERT_LIBRARIES` for an app built with Apple's
  hardened runtime, as most shipping apps are, unless it has the
  `com.apple.security.cs.allow-dyld-environment-variables` entitlement.
- **Linux:** `LD_PRELOAD=/path/to/kit/asan/libasan.so.6 /path/to/YourHost`. Use the
  copy in the kit: it must come from the same compiler version that built
  the plugin.
- **Windows:** put `clang_rt.asan_dynamic-x86_64.dll` from the kit's `asan`
  directory next to the host's executable, or add that directory to `PATH`.

AddressSanitizer writes its reports to standard error;
`ASAN_OPTIONS=log_path=/tmp/asan` sends them to files instead.

## Configuration file

`ofxtesthost --config FILE` reads the `[host]` section of FILE, and sets
`OPENFX_TEST_CONFIG` to it for the plugins it loads; TestProps reads the
`[plugin]` section of the file `OPENFX_TEST_CONFIG` gives. Command-line
options override the file, and a key that is not known draws a warning.

| Section | Key | Value |
|---|---|---|
| `[host]` | `string-lifetime` | `storage` (the default: no limit), `entry-call`, `same-value` or `next-call` |
| `[host]` | `log` | `debug`, `info`, `warning`, `error` or `off` |
| `[plugin]` | `string-audit` | `entry-call`, `same-value` or `next-call` |
| `[plugin]` | `string-audit-fail` | `true` to fail each action in which a string expired early, as well as logging it |
| `[plugin]` | `log` | `debug`, `info`, `warning`, `error` or `off` |
| `[plugin]` | `log-file` | a file to append the log to instead of standard output |

## Reporting

Please post what you find on
[issue #273](https://github.com/AcademySoftwareFoundation/openfx/issues/273):
your host or plugin, the platform, the rule, and the log lines or backtrace.
A rule your software can't meet without a lot of work is as useful to hear
about as a crash.
