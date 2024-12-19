# Validation
More thorough validation of the library beyond running the unit tests is to run the `validate` script. This can take some time but should increase confidence and shake out more potential race conditions and memory problems.

## Prerequisites
- `python3`
- `cmake`
- `c++20` compiler toolchain
- `valgrind` (specifically `memcheck`)
- `roff` 
- `tex`

## Build and Run 
NOTE: INCOMPLETE FEATURE
Execute all tests with `script/validate` from this repository's root directory (example with `gcc`):
```
./script/validate -cc=/path/to/gcc -cxx=/path/to/g++
```

Print test help with:
```
./script/validate --help
```

The `validate` command is a `python3` executable script. Its design goal is to provide a consistent and thorough unit test validation algorithm. It builds and runs the library in many different configurations:
- with the specified `c++` compiler toolchain
- `hce` library compiled with various `HCELOGLEVEL`s
- `hce_ut` unit tests compiled with various `HCELOGLIMIT`s
- produce `valgrind` memory leak reports 

The total times the the `hce_ut` unit tests are built and run is the combinatorial total of all enabled options. IE:
- `-1` up to the maximum `HCELOGLEVEL` value of `9` (x10 test runs )
- `-1` up to the maximum `HCELOGLIMIT` value of `9` (x10 test runs )
- valgrind `memcheck` (+1 test run)

Thus the unit tests will be run 101 times.

The goal of this is to introduce timing differences to increase confidence that the feature logic of the project is correct. A successful default `validate` run should produce high confidence of this software's correctness for the runtime hardware with the specified compiler.

## Compiler options
Compilers are set with the `-cc`/`--c-compiler` and `-cxx`/`-cxx-compiler`. They can be any compiler, including cross compilers.

The argument specified `cc`/`cxx` executable paths are provided to `script/validate` by searching the `PATH` (similar to how `linux` `which my-program` will return the path to the first found `my-program`). 

Additionally, if cross compiling, the user will need to specify `-otc`/`--override-test-command` and `-omc`/`--override-memcheck-command` so that the compiled unit tests can run in the necessary virtual environment or on connected target hardware. It is up to the user to implement said scripts/commands in such a way that compiled unit tests get executed in the right environment.
