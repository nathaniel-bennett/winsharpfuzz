# WinSharpFuzz: Coverage-based Fuzzing for Windows .NET

- Fully ported to Windows; can handle .NET/Core/Framework libraries
- Supports both IL and mixed-mode assemblies
- Uses libFuzzer heuristics to generate inputs--most libFuzzer options are supported
- Reports any erroneous exceptions as crashes
- Well-suited for discovering memory corruption issues in `unsafe` or `Marshal` method calls

## Table of Contents

1. [Acknowledgements](#acknowledgements)
2. [Requirements](#requirements)
3. [Installation](#installation)
4. [Usage](#usage)
    * [Test Harness](#writing-a-test-harness)
    * [Instrumenting](#instrumenting-the-target-library)
    * [Building](#building-the-test-executable)
    * [Running with LibFuzzer](#running-winsharpfuzz-with-libfuzzer)

## Acknowledgements

This project is an unofficial fork of [SharpFuzz](https://github.com/metalnem/sharpfuzz) for the Windows platform; as such, portions of this codebase were written by Nemanja Mijailovic (see NOTICE.txt for license information). This project wouldn't be possible without the groundwork he laid by developing SharpFuzz.
 
Special thanks also goes to ManTech Corporation (www.mantech.com) for facilitating the development and open source release of this fuzzing tool.

## Requirements

As the name suggests, this library is capable of fuzzing C# libraries in a Windows environment. It supports both IL and mixed-mode .dll assemblies, and .NET Framework libraries can also be instrumented and fuzzed with this tool. Linux and MacOS platforms are not supported by this fuzzer. All you will need is a Windows computer with some way to compile the C# code (either [.NET Core/Framework](https://dotnet.microsoft.com/download) or a Visual Studio installation).

In its current state, the WinSharpFuzz library only supports fuzzing with libFuzzer. A zipped binary is available to use that sends libFuzzer inputs to code instrumented with WinSharpFuzz, so no C/C++ compiler is required; however, if you want to build the binary from source then you will need a recent version of [Clang](https://llvm.org/builds/) installed.

## Installation

There are two tools and one library that we'll need to install.

To install the tools, use the following commands within a PowerShell instance:
- `dotnet tool install -g WinSharpFuzz.CommandLine`
- `dotnet tool install -g WinSharpFuzz.Instrument`

These will install everything needed to run the above commands (the -g flag installs them globally). 

As for the `WinSharpFuzz` library, the way you will go about installing it will depend on what environment 
you'll be building the WinSharpFuzz harness code. If you're building the project using the `dotnet` 
command-line tool, just use `dotnet add package WinSharpFuzz`.

## Usage

The process of fuzzing C# code using this library can be broken down into the following four steps: 

1. Writing a test harness that calls the library functions you want to fuzz
2. Instrumenting the dll libraries using `winsharpfuzz-instrument` command
3. Building an executable from the test harness + libraries
4. Running the executable using the `winsharpfuzz` command

We'll go through how to perform each of these steps in order.

#### Writing a Test Harness

In order to run fuzzing on a library, we need to build a .NET executable that will call the 
appropriate functions in that library. This executable will be passed inputs from the underlying 
fuzzing framework (such as WinAFL or libFuzzer), and it will report the control flow taken by its 
code (as well as any exceptions that were thrown).  

The following code shows a skeleton WinSharpFuzz test harness:

```cs
namespace TestExample1
{
    public class Program
    {
        public static void Main(string[] args)
        {
            Fuzzer.LibFuzzer.Initialize(() =>
            {
                // Initialize the library here
            });

            Fuzzer.LibFuzzer.Run(bytes =>
            {
                try
                {
                    // pass input bytes to library functions here
                } 
                catch (ExpectedException) 
                {
                    // Catch only exceptions that are meant to be thrown by the library
                }
            });

            Fuzzer.LibFuzzer.Cleanup(() =>
            {
                // Call any cleanup functions on the library here 
            });
        }
    }
}
```

As we can see, running fuzzing is as simple as calling `Fuzzer.LibFuzzer.Run()` from the main 
function, passing in a function (or in our case, a lambda) that accepts a ReadOnlySpan of bytes, 
and calling our library functions with the passed in bytes. The function passed into 
Fuzzer.LibFuzzer.Run will be called over and over again with unique data each time, and any 
unchecked exceptions or program crashes will be reported.

Occasionaly, you may want to fuzz a library class or method that has one-time initialization or 
cleanup procedures--the `Fuzzer.LibFuzzer.Initialize()` and `Fuzzer.LibFuzzer.Cleanup()` 
methods can be used to accomadate such a case. These methods are completely optional.

Any libraries methods or classes that have been instrumented cannot be called outside of these 
three functions; any attempt to do so will most likely result in a memory access violation. 

#### Instrumenting the Target Library

In order to provide targeted fuzzing, frameworks usually require some kind of feedback on what 
control path was taken during a given execution. WinSharpFuzz gathers this feedback by 
*instrumenting* the target C# library, or adding hooks at each possible control path in the 
library. To instrument a target library, use:

`winsharpfuzz-instrument \path\to\library.dll`

You only need to instrument each library once. If you have multiple other libraries that the target 
library depends on, you can choose whether you want to instrument those or not--but remember that 
any classes and methods called from uninstrumented libraries won't give any control flow feedback 
to the fuzzer. 

#### Building the Test Executable

This should be pretty straightforware--just make sure both `WinSharpFuzz` and `WinSharpFuzz.Common` 
libraries are added as references before you compile your project into an executable.

#### Running WinSharpFuzz with LibFuzzer

Once your project has successfully compiled, you can run libFuzzer using the following command:

`winsharpfuzz --target_path="\path\to\HarnessExecutable.exe" "\path\to\corpus" [other-libfuzzer-options]`

`"\path\to\corpus"` is optional; if specified, it is the path of a folder containing example inputs 
that will be mutated by the fuzzer to provide unique tests. These inputs can be a mixture of valid 
and invalid test cases, but they should not cause the program to crash.

Additional libFuzzer options can be used from this command, such as specifying the number of 
consecutive jobs to be run or maximum input size. More information on these options can be found 
[here](https://llvm.org/docs/LibFuzzer.html#options). It should be noted that some of the memory 
options (such as `-malloc_limit_mb`) will not be useful because of the independant way in which the 
libfuzzer and .NET executables operate.