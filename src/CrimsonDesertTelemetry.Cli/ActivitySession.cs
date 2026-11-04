using System.ComponentModel;
using System.Diagnostics;
using CrimsonDesertTelemetry.Core;

namespace CrimsonDesertTelemetry.Cli;

/// <summary>Independent 20 Hz heartbeat; pose/GPU failures cannot stop this worker.</summary>
internal sealed class ActivitySession : IDisposable
{
    private readonly CancellationTokenSource _stop = new();
    private readonly Task _task;
    private SimulationActivityObservation _latest = SimulationActivityObservation.Unknown("waiting-for-observer");
    public string SessionId { get; }
    public SimulationActivityObservation Current => Volatile.Read(ref _latest).Fresh(Environment.TickCount64);
    public ActivitySession(Process process, TelemetryServerState state)
    {
        var pid = process.Id;
        var born = process.StartTime.ToFileTimeUtc();
        SessionId = $"{pid:x}-{born:x}";
        state.BeginActivitySession(SessionId);
        _task = Task.Run(async () =>
        {
            using var reader = new SimulationActivityReader(pid, born);
            try
            {
                using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(50));
                do
                {
                    if (process.HasExited) break;
                    var value = reader.Capture();
                    Volatile.Write(ref _latest, value);
                    state.PublishActivity(SessionId, value);
                } while (await timer.WaitForNextTickAsync(_stop.Token));
            }
            catch (OperationCanceledException) when (_stop.IsCancellationRequested) { }
            catch (Exception e) when (e is InvalidOperationException or Win32Exception) { }
            finally
            {
                var unavailable = SimulationActivityObservation.Unknown("session-ended");
                Volatile.Write(ref _latest, unavailable);
                state.PublishActivity(SessionId, unavailable);
            }
        });
    }
    public void Dispose()
    {
        _stop.Cancel();
        // MMF reads are bounded; a busy publication must not prevent shutdown.
        if (_task.Wait(TimeSpan.FromSeconds(1))) _stop.Dispose();
        else _ = _task.ContinueWith(_ => _stop.Dispose(), TaskScheduler.Default);
    }
}
