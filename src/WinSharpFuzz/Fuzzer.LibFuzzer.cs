using System;
using System.IO;
using System.IO.Pipes;
using System.IO.MemoryMappedFiles;

namespace WinSharpFuzz
{
	/// <summary>
	/// American fuzzy lop instrumentation and fork server for .NET libraries.
	/// </summary>
	public static partial class Fuzzer
	{
		/// <summary>
		/// LibFuzzer class contains the libFuzzer runner. It enables users
		/// to fuzz their code with libFuzzer by using the libFuzzer-dotnet
		/// binary, which acts as a bridge between the libFuzzer and the
		/// managed code (it currently works only on Linux).
		/// </summary>
		public static class LibFuzzer
		{
			/// <summary>
			/// Initialize method runs any needed code before the main Run function. 
			/// While initialization code could just be called in the `main` function, 
			/// there are cases where initialization code is instrumented alongside 
			/// the target code and so can not be called without throwing an access 
			/// violation exception. This function allows for that code to be called 
			/// without the memory errors.
			/// </summary>z
			/// <param name="action">
			/// Some action that initializes the instrumented library.
			/// </param>
			public static unsafe void Initialize(Action action) {
				byte[] span = new byte[(1 << 16)];

				fixed(byte* tempMemory = span)
                {
					Common.Trace.SharedMem = tempMemory;
					Common.Trace.PrevLocation = 0;
					
					action();

					Common.Trace.SharedMem = null;
					Common.Trace.PrevLocation = 0;
                }
			}

			/// <summary>
			/// Run method starts the libFuzzer runner. It repeatedly executes
			/// the passed action and reports the execution result to libFuzzer.
			/// If the executable that is calling it is not running under libFuzzer,
			/// the action will be executed normally, and will receive its input
			/// from the file specified in the first command line parameter.
			/// </summary>
			/// <param name="action">
			/// Some action that calls the instrumented library. The span argument
			/// passed to the action contains the input data. If an uncaught
			/// exception escapes the call, crash is reported to libFuzzer.
			/// </param>
			public static unsafe void Run(ReadOnlySpanAction action)
			{
				ThrowIfNull(action, nameof(action));
				var mappedFileID = Environment.GetEnvironmentVariable("__LIBFUZZER_SHM_ID");

				if (mappedFileID is null)
				{
					RunWithoutLibFuzzer(action);
					return;
				}

				// Retrieve the names of the named pipes we set in our .cc program
				var readPipeName = Environment.GetEnvironmentVariable("__LIBFUZZER_CTL_PIPE");
				var writePipeName = Environment.GetEnvironmentVariable("__LIBFUZZER_ST_PIPE");

				// Console.WriteLine("CTL pipe name (.NET side): " + readPipeName);
				// Console.WriteLine("ST pipe name (.NET side): " + writePipeName);

				if (readPipeName is null || writePipeName is null)
				{
					RunWithoutLibFuzzer(action);
					return;
				}

				using (var mappedFile = System.IO.MemoryMappedFiles.MemoryMappedFile.OpenExisting(mappedFileID, MemoryMappedFileRights.ReadWrite, HandleInheritability.Inheritable))
				using (var readPipe = new NamedPipeClientStream(".", readPipeName, PipeDirection.In))
				using (var writePipe = new NamedPipeClientStream(".", writePipeName, PipeDirection.Out))
				{
					// Console.WriteLine("Connecting to st pipe (.NET side)...");
					writePipe.Connect();
					// Console.WriteLine("Connected to st pipe (.NET side).");

					// Console.WriteLine("Connecting to ctl pipe (.NET side)...");
					readPipe.Connect();
					// Console.WriteLine("Connected to ctl pipe (.NET side).");

					using (var r = new BinaryReader(readPipe))
					using (var w = new BinaryWriter(writePipe))
					{
						var viewAccessor = mappedFile.CreateViewAccessor(0, MapSize + DefaultBufferSize, MemoryMappedFileAccess.ReadWrite);
						var sharedMem = (byte*) viewAccessor.SafeMemoryMappedViewHandle.DangerousGetHandle();
						// var sharedMem = (byte*) mappedFile.SafeMemoryMappedFileHandle.DangerousGetHandle();
						var trace = new TraceWrapper(sharedMem);

						w.Write(0);

						try
						{
							var status = Fault.None;

							// The program instrumented with libFuzzer will exit
							// after the first error, so we should do the same.
							while (status != Fault.Crash)
							{
								trace.ResetPrevLocation();

								var size = r.ReadInt32();
								var data = new ReadOnlySpan<byte>(sharedMem + MapSize, size);

								// Console.WriteLine("Read " + size.ToString() + " from read pipe (.NET side)");

								try
								{
									action(data);
								}
								catch (Exception ex)
								{
									Console.Error.WriteLine(ex);
									status = Fault.Crash;
								}

								w.Write(status);
							}
						}
						catch
						{
							// Error communicating with the parent process, most likely
							// because it was terminated after the timeout expired, or
							// it was killed by the user. In any case, the exception
							// details don't matter here, so we can just exit silently.
							return;
						}
					}
				}
			}

			private static unsafe void RunWithoutLibFuzzer(ReadOnlySpanAction action)
			{
				var args = Environment.GetCommandLineArgs();

				if (args.Length <= 1)
				{
					Console.Error.WriteLine("You must specify the input path as the first command line argument when not running under libFuzzer.");
				}

				fixed (byte* sharedMem = new byte[MapSize])
				{
					new TraceWrapper(sharedMem);
					action(File.ReadAllBytes(args[1]));
				}
			}

			/// <summary>
			/// Cleanup method runs any needed code after the main Run function. 
			/// While cleanup code could just be called in the `main` function, 
			/// there are cases where cleanup code is instrumented alongside 
			/// the target code and so can not be called without throwing an access 
			/// violation exception. This function allows for that code to be called 
			/// without the memory errors.
			/// </summary>z
			/// <param name="action">
			/// Some action that cleans up the instrumented library.
			/// </param>
			public static unsafe void Cleanup(Action action) {
				byte[] span = new byte[(1 << 16)];

				fixed(byte* tempMemory = span)
                {
					Common.Trace.SharedMem = tempMemory;
					Common.Trace.PrevLocation = 0;
					
					action();

					Common.Trace.SharedMem = null;
					Common.Trace.PrevLocation = 0;
                }
			}
		}
	}
}
