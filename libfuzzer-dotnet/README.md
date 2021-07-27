# libfuzzer-dotnet

libFuzzer driver for SharpFuzz. Read [Using libFuzzer with SharpFuzz] for usage instructions.

[Using libFuzzer with SharpFuzz]: https://github.com/Metalnem/sharpfuzz/blob/master/docs/libFuzzer.md

The required executable file (libfuzzer-dotnet.exe) is contained within the zipped folder `libfuzzer-dotnet-windows.zip`. It is a 64-bit binary, to the best of my knowledge. To build it from source, follow the following steps:

1. Install clang for Windows (official build found [here](https://llvm.org/builds/)).
2. Build libfuzzer-dotnet.cc into an executable with the following command: `clang++ -g -fsanitize="fuzzer,address" libfuzzer-dotnet.cc -o libfuzzer-dotnet.exe`
3. Optional: add libfuzzer-dotnet.exe to your PATH (Control Panel->System->Advanced system settings->'Advanced' tab->Environment Variables...)
4. Run libfuzzer-dotnet with the desired .NET executable (see the above link for instructions).

For more information on how to use libFuzzer and its various options during fuzzing, look at [the official libFuzzer documentation](https://llvm.org/docs/LibFuzzer.html).
