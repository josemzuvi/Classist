# Classist

**Classist** is a lightweight, compile-time object-oriented metaprogramming framework and header generator for modern C. It enables clean class definitions and ergonomic method dispatching without external parsers or runtime overhead, leveraging standard GCC/Clang ELF sections (`__attribute__((section(...)))`) to introspect types and generate standard C headers automatically.

---

## Architecture & Project Structure

A typical project using Classist is organized as follows:

```text
my-project/
├── headers/            # Public / global headers (includes classist.h)
│   └── classist.h
├── classist/           # Classist generator engine
│   └── classist.c
├── src/                # Your source code (.cls.h and implemented .c files)
│   ├── main.cls.h
│   ├── main.c
│   └── animal/
│       ├── Animal.cls.h
│       └── Animal.c
├── generated/          # Auto-generated standard C headers (git-ignored)
│   ├── main.h
│   └── animal/
│       └── Animal.h
├── build/              # Compilation artifacts and binaries (git-ignored)
│   └── app
├── compile_flags.txt   # Root compile flags for clangd / language servers
├── Makefile            # Build system orchestrator
└── .gitignore
```

---

## Writing Definitions (`.cls.h`)

Classist definition files use the `.cls.h` extension and are placed directly inside `src/` where the implementation `.c` files reside.

### 1. Defining a Class

Use `package()`, `class()`, `var()`, `func()`, and `static()` to declare members:

```c
// src/animal/Animal.cls.h
#include "classist.h"

package(animal);

include(<stdio.h>);
include(<stdbool.h>);

class(Animal)
    var(char*, action);
    func(void, act, ());
    static(Animal*, new, (char* action));
endClass
```

- **`package(pkg)`**: Namespaces the class to a subfolder/prefix (e.g. `animal.Animal` produces `animal_Animal` and paths `animal/Animal.h`).
- **`include(lib)`**: Injects standard system headers (e.g. `<stdio.h>`) or user headers (`"my_header.h"`) into the generated header.
- **`class(Name)`**: Begins a class definition.
- **`var(type, name)`**: Declares a field in the class struct.
- **`func(ret_type, name, (params...))`**: Declares an instance method. Classist automatically injects `(ClassName* this)` as the first parameter.
- **`static(ret_type, name, (params...))`**: Declares a static/factory method without an implicit `this` parameter.
- **`endClass`**: Terminates the class definition.

### 2. Defining a Plain Module (e.g. `main.cls.h`)

For entry points or files without a class/struct:

```c
// src/main.cls.h
#include "classist.h"

include(<stdio.h>);
import(animal.Animal);
```

- **`import(pkg.Class)`**: Imports another Classist class and generates the necessary `#include` and macro definitions inside `#ifdef <module>_C`.

---

## Implementing and Using Classes in C

When you run `make cls`, Classist generates:
1. Standard C headers inside `generated/`.
2. Initial source skeletons in `src/` (if the `.c` file does not exist yet). **Existing files are never overwritten**.

### Class Implementation (`src/animal/Animal.c`)

Methods are implemented as `static` functions so they don't pollute the global symbol table. The `endClass` macro binds them to the class function table:

```c
#define animal_Animal_C
#include "animal/Animal.h"

#include <stdlib.h>

static animal_Animal* new(char* action) {
    animal_Animal* self = malloc(sizeof(animal_Animal));
    self->action = action;
    return self;
}

static void act(animal_Animal* this) {
    printf("Animal action: %s\n", this->action);
}

endClass
```

### Consuming Classes (`src/main.c`)

Classist provides an ergonomic, overloaded macro syntax `ClassName(...)`:

```c
#define main_C
#include "main.h"

int main(int argc, char** argv) {
    // 1. Declare a type: Animal() -> animal_Animal
    Animal()* dog = Animal(new)("Bark");

    // 2. Call an instance method: Animal(member)(instance)
    Animal(act)(dog);

    return 0;
}
```

- **`Animal()`** (zero arguments) $\to$ resolves to the struct type (`animal_Animal`).
- **`Animal(method)`** (one argument) $\to$ resolves to the class function pointer (`__class__animal_Animal.method`).

---

## Makefile Commands

The included `Makefile` automates both the Classist code generation and the application build:

| Command | Description |
| :--- | :--- |
| **`make cls`** | Runs Classist: scans all `src/**/*.cls.h`, generates headers in `generated/`, and creates skeleton `.c` files in `src/` if missing. |
| **`make build`** (or **`make`**) | Ensures `make cls` has run and compiles all `.c` files in `src/` into the final executable `build/app`. |
| **`make run`** | Compiles the project and runs the resulting binary (`./build/app`). |
| **`make clean`** | Removes the `build/` directory and temporary Classist files. |
| **`make distclean`** | Full reset: removes `build/`, temporary Classist files, and the entire `generated/` directory. *(Source code in `src/` is preserved).* |
| **`make classist-clean`** | Cleans only the temporary Classist generator binaries without touching project objects. |
| **`make help`** | Displays the list of available commands and descriptions in the terminal. |

---

## Language Server (LSP) Support

A [compile_flags.txt](compile_flags.txt) file is located in the project root containing:
```text
-xc
-Iheaders
-Isrc
-Igenerated
```
This allows `clangd` and LSP-enabled editors (such as Zed, VS Code, Neovim) to resolve all paths, headers, and macros from the project root with zero configuration.
