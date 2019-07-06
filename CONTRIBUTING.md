# Contributing to dd-opentracing-cpp

Pull requests for bug fixes are welcome.
Before submitting new features or changes to current functionality, [open an issue](https://github.com/DataDog/dd-opentracing-cpp/issues/new) and discuss your ideas or propose the changes you wish to make.
After a resolution is reached, a PR can be submitted for review.

## Code Style

C++ code must be formatted using `clang-format`. Before submitting code changes, the following command should be run:

```shell 
find include src test -iname '*.h' -o -iname '*.cpp' | xargs clang-format-6.0 -i
```

Other source and documentation files should be consistently formatted, but this is not enforced in CI checks.
