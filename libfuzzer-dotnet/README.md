# libfuzzer-dotnet

libFuzzer driver for WinSharpFuzz. Read [Using libFuzzer with SharpFuzz] for usage instructions.

[Using libFuzzer with WinSharpFuzz]: https://github.com/nathaniel-bennett/winsharpfuzz/blob/master/docs/libFuzzer.md

The required executable file (libfuzzer-dotnet.exe) is contained within the zipped folder `libfuzzer-dotnet-windows.zip`. It is a 64-bit binary, to the best of my knowledge. To build it from source, follow the following steps:

1. Install clang for Windows (official build found [here](https://llvm.org/builds/)). Make sure to check the box that adds clang to the PATH variable during the installation process.
2. Install Visual Studio and its associated C++ build tools (note that VS can be swapped out for MinGW if desired; Clang only uses it to do its linking. See [here](https://www.reddit.com/r/cpp_questions/comments/715bcn/can_i_use_clang_on_windows_without_installing/).
3. Build libfuzzer-dotnet.cc into an executable with the following command: `clang++ -g -fsanitize="fuzzer,address" libfuzzer-dotnet.cc -o libfuzzer-dotnet.exe`. Make sure to install both Clang and Visual Studio before opening the PowerShell instance, or the command won't work as intended.
4. Optional: add libfuzzer-dotnet.exe to your PATH (Control Panel->System->Advanced system settings->'Advanced' tab->Environment Variables...)
5. Run libfuzzer-dotnet with the desired .NET executable (see the above link for instructions).

For more information on how to use libFuzzer and its various options during fuzzing, look at [the official libFuzzer documentation](https://llvm.org/docs/LibFuzzer.html).
