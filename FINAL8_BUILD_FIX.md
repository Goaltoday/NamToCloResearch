# FINAL8 build fix

Fix for MSVC error C2027/C2676 in `src/common.cpp`.

Cause: `std::array<std::uint8_t,256>` is used by the official GP-200 CRC lookup tables, but `<array>` was not included explicitly. GCC/Clang syntax checks passed because another include provided it transitively in that environment; MSVC correctly treated `std::array` as incomplete.

Change:

```cpp
#include <algorithm>
#include <array>
#include <fstream>
```

No DSP, conversion, CRC table, trainer, NAMCore, r8brain, or CLO serialization logic has been changed.
