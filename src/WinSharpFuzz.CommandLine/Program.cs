using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Diagnostics;
using System.Threading;
using System.Reflection;

namespace WinSharpFuzz.CommandLine
{
	public class Program
	{
		private const string Usage = @"Usage: dotnet winsharpfuzz --target_path=[path-to-binary] [args ...]

path-to-binary:
  The path to a WinSharpFuzz test harness executable

args:
  Any additional libFuzzer flags or options desired.
  For libFuzzer flag options, see https://llvm.org/docs/LibFuzzer.html
  
Examples:
  dotnet winsharpfuzz --target_path=RegexFuzzer.exe
  dotnet winsharpfuzz --target_path='C:\Users\TestUser\Code\RegexFuzzer.exe' -jobs=8
  (note that in both examples, 'RegexFuzzer.exe' should be WinSharpFuzz harness code that calls instrumented libraries)";

		public static int Main(string[] args)
		{
			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
				Console.WriteLine("WinSharpFuzz is meant for use only in the Windows platform.");
				Console.WriteLine("To fuzz .NET code in Linux/Unix, see SharpFuzz (www.nuget.org/packages/SharpFuzz)");
				return 0;
			}

			if (args.Length == 0 || args[0] == "--help" || args[0] == "-h")
			{
				Console.WriteLine(Usage);
				return 0;
			}

			string ExecFilePath = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);

			switch (RuntimeInformation.ProcessArchitecture)
            {
				case Architecture.X64:
					ExecFilePath += "\\winsharpfuzz-libfuzzer-x64.exe";
					break;

				case Architecture.X86:
					ExecFilePath += "\\winsharpfuzz-libfuzzer-x86.exe";
					break;

				default:
					Console.WriteLine("Desired architecture unsupported (x86 or x64 required; ARM currently unsupported).");
					return 0;
            }

			Process p = new Process()
			{
				EnableRaisingEvents = true,
				StartInfo = new ProcessStartInfo
				{
					Arguments = String.Join(' ', args),
					CreateNoWindow = true,
					FileName = ExecFilePath,
					RedirectStandardError = true,
					UseShellExecute = false,
                },
			};

			Console.WriteLine("Executing process...");

            if (!p.Start())
            {
				Console.WriteLine("Child process unexpectedly failed to start up. Aborting...");
				return 1;
            }

			Console.CancelKeyPress += (object sender, ConsoleCancelEventArgs a) =>
			{
				Console.WriteLine("Interrupt detected--stopping WinSharpFuzz instance");
				p.Kill(true); // TODO: replace with simpler SIGINT to child

				p.WaitForExit();
				p.Close();
				System.Environment.Exit(0);
			};

			RedirectToConsole(p.StandardError);

			p.WaitForExit();
			p.Close();

			Console.WriteLine("WinSharpFuzz process exited.");
			return 0;
		}

		private static void RedirectToConsole(StreamReader input)
        {
			new Thread(a =>
			{
				string line = input.ReadLine();
				while (line != null)
                {
					Console.WriteLine(line);
					line = input.ReadLine();
                }
			}).Start();
        }
	}
}
