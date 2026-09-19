# Contributing to Pulse

Contributions should keep Pulse focused on local Linux system monitoring and keep the collector, model, state, and UI layers separate.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run `cmake --build build --target format` before opening a pull request when `clang-format` is installed. New parsing behavior should include a small fixture and a test that does not depend on the current machine's `/proc` contents.

Pull requests should explain the user-visible behavior, the Linux interface involved, and how the change was validated. Keep commits focused and do not include build output, credentials, editor state, or machine-specific data.
