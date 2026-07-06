---
name: format
description: Format airspy-fmradion sources with clang-format and cmake-format. Use after editing C++ sources or CMakeLists.txt, and before committing code changes.
---

# Code formatting

```sh
# Format C++ sources
clang-format -i main.cpp include/*.h sfmbase/*.cpp

# Format CMakeLists.txt
cmake-format -i CMakeLists.txt
```

The `.clang-format` file defines the style (LLVM base, C++20, 80-column
limit, 2-space indent, no tabs). The `.cmake-format.py` file defines CMake
style.
