# sharpfuzz-windows

.NET fuzzing integration with libFuzzer, ported to the Windows platform


## Building the Project

In order to use the WinSharpFuzz library, we must first build it. This is relatively straightforward, 
and can be accomplished by building the project either in Visual Studio or by using the `dotnet` 
command in a powershell terminal. If using Visual Studio, open the WinSharpFuzz.sln project (make 
sure you haven't just opened the folder that the .sln file resides in). Click the 'Build' tab, then 
select 'Build Solution' (or just type Ctrl+Shift+B). If using a powershell terminal, simply navigate 
to the root directory of this project and execute the following: `dotnet build`. The resulting libraries 
can be found in 
`src/WinSharpFuzz/bin/Debug/netstandard2.0/*.dll`.

This build process also outputs a tool for instrumenting C# libraries so that their control paths 
can be detected by WinSharpFuzz. This tool can be found in 
`src/WinSharpFuzz.CommandLine/bin/Debug/netcoreapp5.0/WinSharpFuzz.CommandLine.exe`.

Lastly, we need to extract the libFuzzer adapter executable found in 
`libfuzzer-dotnet/libfuzzer-dotnet-windows.zip`. This can also be built from source if desired; for 
instructions on how to accomplish that, refer to the README found within the `libfuzzer-dotnet/` subdirectory.

## Usage

The process of fuzzing C# code using this library can be broken down into the following steps:


1. Writing a test harness that calls the library functions you want to fuzz

2. Instrumenting the dll libraries using `WinSharpFuzz.CommandLine.exe`

3. Building an executable from the test harness + libraries

4. Running the executable using `libfuzzer-dotnet`


We'll go through how to perform each of these steps in order.

#### Writing a Test Harness

In order to run fuzzing on a library, we need to build a .NET executable that will call 
the appropriate functions in that library. This executable will be passed inputs from 
the underlying fuzzing framework (such as WinAFL or libFuzzer), and it will report the 
control flow taken by its code (as well as any exceptions that were thrown). Thankfully, 
WinSharpFuzz provides a simple abstraction for this process.

The following code shows a simple test harness built for WinSharpFuzz:

```cs
using System;
using SharpFuzz;

namespace TestExample1
{
	public class Program
	{
		public static void Main(string[] args)
		{
			Fuzzer.LibFuzzer.Run(bytes =>
			{
				// pass input bytes to library functions here
			});
		}
	}
}

```

As we can see, running fuzzing is as simple as calling `Fuzzer.LibFuzzer.Run()` from the 
main function, passing in a function (or in our case, a lambda) that accepts a 
ReadOnlySpan of bytes, and calling our library functions with the passed in bytes. 
The function passed into Fuzzer.LibFuzzer.Run will be called over and over again with 
unique data each time, and any exceptions thrown within the function will be recorded as 
crashes.

Occasionaly, you may want to fuzz a library class or method that has one-time initialization or 
cleanup procedures. Adding these functions within the `Run` method would introduce that 
overhead initialization for *every single fuzzing execution*--certainly not behavior 
that is desired, especially if the initialization and cleanup procedures are at all 
expensive on time or resources. To provide a faster alternative to this, two other 
methods are available that can be used to call any setup or teardown functions needed:

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
				// pass input bytes to library functions here
			});

			Fuzzer.LibFuzzer.Cleanup(() =>
			{
				// Call any cleanup functions on the library here 
			});
		}
	}
}
```

`Fuzzer.LibFuzzer.Initialize()` can be used to initialize the library (or any classes 
within the library) that are to be used during the actual fuzzing, while 
`Fuzzer.LibFuzzer.Cleanup()` can be used to perform any necessary cleanup functions. 
Both methods are passed in a function (or, in this case, a lambda) with no input 
parameters and no return value.

Any libraries methods or classes that have been instrumented cannot be called 
outside of these three functions; any attempt to do so will most likely result in a 
memory access violation. 

Now, you may be wondering exactly what instrumentation is or why it results in this 
behavior--we'll cover that next.

#### Instrumenting the Target Library

In order to provide targeted fuzzing, frameworks usually require some kind of 
feedback on what control path was taken during a given execution. WinSharpFuzz 
gathers this feedback by *instrumenting* the target C# library, or adding hooks at 
each possible control path in the library. These hooks report which path of execution 
was taken, thereby giving the fuzzing framework clear feedback on where it should try 
to mutate its inputs next.

Thankfully, both the process of adding these hooks and the handling of the information 
they report are taken care of by WinSharpFuzz. The `WinSharpFuzz.CommandLine.exe` 
executable can be used to instrument dll files in place, while the `WinSharpFuzz` and 
`WinSharpFuzz.Common` libraries handle the information provided by this instrumentation.

To instrument a dll file, simply execute the following command:

`WinSharpFuzz.CommandLine.exe \path\to\library.dll`

(make sure to execute this within the folder containing `WinSharpFuzz.CommandLine.exe`, 
and change \path\to\library.dll to the path of the library you want to instrument).

You only need to instrument each library once. If you have multiple other libraries that 
the target library depends on, you can choose whether you want to instrument those or 
not--but remember that classes and methods from uninstrumented libraries won't give any
control flow feedback to the fuzzer.

#### Building the Test Executable

Once the harness is written up, the next step is to add the necessary libraries and build the 
project. The `WinSharpFuzz.dll` and `WinSharpFuzz.Common.dll` are both needed in order to 
include WinSharpFuzz in the project, so make sure to add both of those. They can be found 
in `src/WinSharpFuzz/WinSharpFuzz/bin/Debug/netstandard2.0/*.dll`.

TODO: explain how to add libraries both in .csproj and in Visual Studio

Now the harness should be all ready for building. You can build either with Ctrl+Shift+B 
in Visual studio or by executing `dotnet build` from powershell in the root of the project. 
Then determine the path to the resulting executable.

#### Running WinSharpFuzz with LibFuzzer

On its own, libFuzzer is meant for C/C++ projects. However, the code provided in 
`libfuzzer-dotnet/` bridges the gap between libFuzzer and C# code through the use 
of pipes and shared memory; from a high level, these allow the libfuzzer-dotnet binary 
to pass fuzzing inputs to the C# executable, which in turn passes back control flow 
information to the C++ binary. With this setup, fuzzing can be performed using the 
simple command:

`libfuzzer-dotnet.exe --target_path="\path\to\HarnessExecutable.exe" "\path\to\corpus"`

`"\path\to\corpus"` is optional; if specified, it should be a directory containing example inputs 
for the fuzzer to go off of. These inputs can be a mixture of valid and invalid cases.

Additional libFuzzer options can be used from this executable, such as specifying the number 
of consecutive jobs to be run or maximum input size. More information on these options can 
be found [here](https://llvm.org/docs/LibFuzzer.html#options). It should be noted that 
some of the memory options (such as `-malloc_limit_mb`) will not be useful 
because of the independant way in which the libfuzzer and .NET executables operate.




