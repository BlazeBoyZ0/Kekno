# Kekno Programming Language (v0.5.2)

Kekno is a lightweight, bytecode-compiled scripting language interpreter and REPL written in C++17.

## What's New in v0.5.2

Kekno v0.5.2 introduces a full multi-file program and module system, replacing the former single-file import mechanism.

### Key Module Features
* **Module Resolution**: Every `.kek` file is a module. Paths use dots as directory separators:
  ```kekno
  grab math~       // loads math.kek
  grab lib.num~   // loads lib/num.kek
  ```
* **Member Access**: Access exported symbols on loaded modules using dot syntax:
  ```kekno
  grab math~
  echo math.sqrt(9)~
  echo lib.num.five()~
  ```
* **Module Aliases**: Use `as` to alias imported module namespaces:
  ```kekno
  grab math as m~
  grab lib.num as n~

  echo m.sqrt(9)~
  echo n.five()~
  ```
  Module aliases are immutable bindings and cannot be reassigned.
* **Visibility Control (`pub` and `priv`)**:
  Declarations at module scope are private by default unless marked `pub`:
  ```kekno
  pub let pi = 3.14~
  pub const int MAX = 100~
  priv let secret = 42~

  pub task sqrt(x) {
      give x~
  }
  priv task helper() {
      // internal helper
  }
  ```
  `pub` and `priv` modifiers are restricted to module-level scope and produce compile-time errors if used on local variables, task parameters, or local tasks.
* **Top-Level Scope Enforcement**:
  `grab` statements are allowed only at module top-level scope and will produce compiler errors if placed inside tasks, blocks, loops, or conditionals.
* **Single Initialization and Caching**:
  Modules execute their top-level initialization code exactly once when first grabbed and are cached across all aliases and subsequent `grab` statements.
* **Relative Dependencies**:
  Modules resolve relative `grab` dependencies with respect to their own file path rather than the current working directory.
* **Circular Dependency Detection**:
  Circular import chains (e.g. `a -> b -> a`) are automatically detected and reported with detailed dependency trace errors:
  `[Module Error]: Circular module dependency detected: a -> b -> a`

## Building and Running

### Build Prerequisites
* CMake 3.12 or higher
* C++17 compliant compiler (GCC, Clang, or MSVC)

### Building
```bash
mkdir -p build
cd build
cmake ..
make
```

### Running Tests
```bash
cd build
ctest --output-on-failure
```

### Running Kekno Scripts
```bash
./build/kekno script.kek
```
Running `./build/kekno` without arguments launches the interactive REPL.
