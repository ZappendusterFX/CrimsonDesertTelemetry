using System.Diagnostics;
using System.Numerics;
using System.Text;

namespace CrimsonDesertTelemetry.Core;

public readonly record struct PlayerOrientationAddresses(ulong WorldSystemGlobalAddress);

/// <summary>
/// Reads the validated player physics-root basis for the supported build.
/// A failed or changing chain returns no orientation; it never publishes a stale value.
/// </summary>
public sealed class PlayerOrientationReader
{
    private readonly PlayerOrientationAddresses _addresses;
    private readonly PlayerRootDefinition _definition;

    private PlayerOrientationReader(PlayerOrientationAddresses addresses, PlayerRootDefinition definition)
    {
        _addresses = addresses;
        _definition = definition;
    }

    /// <summary>Construct the same guarded reader from an already resolved address (also for offline memory fixtures).</summary>
    public static PlayerOrientationReader FromResolvedAddress(ulong worldSystemGlobalAddress, PlayerRootDefinition definition,
        bool allowResearchCandidate = false)
    {
        BuildProfileValidation.ValidatePlayerRoot(definition, trusted: !allowResearchCandidate);
        if (worldSystemGlobalAddress is < 0x10000 or > 0x00007FFFFFFFFFFF || (worldSystemGlobalAddress & 7) != 0)
            throw new InvalidDataException("Invalid resolved world-system global address.");
        return new PlayerOrientationReader(new(worldSystemGlobalAddress), definition);
    }

    public static PlayerOrientationReader? Resolve(Process process, string executable, BuildDefinition definition,
        bool allowResearchCandidate = false)
    {
        var root = definition.PlayerRoot;
        if (root is null || root.WorldSystemPattern.Purpose != "world-system") return null;
        BuildProfileValidation.ValidateRuntimeAnchor(root.WorldSystemPattern, definition,
            allowResearchCandidate, "Player orientation is not validated for this build.");
        var moduleBase = checked((ulong)process.MainModule!.BaseAddress.ToInt64());
        var relative = StaticPositionProbe.ResolveUniqueRipTarget(executable, root.WorldSystemPattern);
        return FromResolvedAddress(checked(moduleBase + relative), root,
            allowResearchCandidate && definition.Status == "research");
    }

    /// <summary>
    /// Why the most recent <see cref="Read"/> produced no orientation, or null when it
    /// succeeded. Failing closed is deliberate; failing silently is not. A dozen
    /// separate validations can reject a sample and they are indistinguishable from
    /// the outside, which makes a stale build definition impossible to tell apart from
    /// a broken pointer chain without attaching a debugger to the game.
    /// </summary>
    public string? LastFailureReason { get; private set; }

    public PlayerOrientationSnapshot? Read(IReadOnlyProcessMemory reader, (float X, float Y, float Z) worldPosition)
    {
        try
        {
            var first = ReadChain(reader);
            var transform = ReadTransform(reader, first.PhysicsAddress, worldPosition);
            // The game can replace the actor/physics object while a sample is in flight.
            // Confirm the same leaf before publishing the basis.
            var second = ReadChain(reader);
            if (second.PhysicsAddress != first.PhysicsAddress)
            {
                LastFailureReason = "The physics object was replaced while the sample was in flight.";
                return null;
            }
            LastFailureReason = null;
            return ToSnapshot(transform);
        }
        catch (Exception exception) when (exception is InvalidDataException or OverflowException or
            System.ComponentModel.Win32Exception)
        {
            LastFailureReason = exception.Message;
            return null;
        }
    }

    /// <summary>Search only the final field of the RTTI-verified player owner.</summary>
    public int? FindPhysicsOffset(IReadOnlyProcessMemory reader, (float X, float Y, float Z) worldPosition,
        int referenceOffset)
    {
        if (referenceOffset is < 0x88 or > 0x3F80 || referenceOffset % 8 != 0)
            throw new InvalidDataException("Player physics recovery reference is outside bounds.");
        var owner = ReadOwner(reader);
        int? found = null;
        for (var offset = referenceOffset - 0x80; offset <= referenceOffset + 0x80; offset += 8)
        {
            try
            {
                var physics = ReadPointer(reader, owner + (ulong)offset);
                _ = ReadTransform(reader, physics, worldPosition);
                if (found is not null)
                    throw new InvalidOperationException("Ambiguous player physics fields.");
                found = offset;
            }
            catch (Exception error) when (error is InvalidDataException or OverflowException or
                System.ComponentModel.Win32Exception) { }
        }
        if (ReadOwner(reader) != owner)
            throw new InvalidDataException("Player owner changed during physics field recovery.");
        return found;
    }

    private ChainResult ReadChain(IReadOnlyProcessMemory reader)
    {
        var owner = ReadOwner(reader);
        var physics = ReadNext(reader, owner, _definition.OwnerToPhysicsOffset, "owner", "physics");
        return new ChainResult(physics);
    }

    private ulong ReadOwner(IReadOnlyProcessMemory reader)
    {
        // Each hop names itself. The chain has six offsets and any one of them can be
        // invalidated by a game patch; "the chain failed" does not narrow that down,
        // "control -> owner (+0x140)" does.
        var current = ReadPointer(reader, _addresses.WorldSystemGlobalAddress);
        var manager = ReadNext(reader, current, _definition.WorldSystemToActorManagerOffset,
            "worldSystem", "actorManager");
        CheckType(reader, manager, 0, "actorManager");
        var actor = ReadNext(reader, manager, _definition.ActorManagerToPlayerActorOffset,
            "actorManager", "playerActor");
        CheckType(reader, actor, 1, "playerActor");
        var intermediate = ReadNext(reader, actor, _definition.PlayerActorToIntermediateOffset,
            "playerActor", "intermediate");
        var control = ReadNext(reader, intermediate, _definition.IntermediateToControlOffset,
            "intermediate", "control");
        CheckType(reader, control, 2, "control");

        return ReadNext(reader, control, _definition.ControlToOwnerOffset, "control", "owner");
    }

    private TransformResult ReadTransform(IReadOnlyProcessMemory reader, ulong physics,
        (float X, float Y, float Z) worldPosition)
    {
        if (_definition.TransformLayout == "sqt-v1")
        {
            var sqtLength = Math.Max(_definition.PositionOffset + 0x0C, _definition.QuaternionOffset + 0x10);
            var sqtBytes = reader.Read(ToIntPtr(physics), sqtLength);
            var quaternion = new Quaternion(
                BitConverter.ToSingle(sqtBytes, _definition.QuaternionOffset),
                BitConverter.ToSingle(sqtBytes, _definition.QuaternionOffset + 4),
                BitConverter.ToSingle(sqtBytes, _definition.QuaternionOffset + 8),
                BitConverter.ToSingle(sqtBytes, _definition.QuaternionOffset + 12));
            var sqtPosition = Vector(sqtBytes, _definition.PositionOffset);
            if (!Finite(sqtPosition) || !float.IsFinite(quaternion.X) || !float.IsFinite(quaternion.Y) ||
                !float.IsFinite(quaternion.Z) || !float.IsFinite(quaternion.W) ||
                Math.Abs(quaternion.LengthSquared() - 1) > .002f)
                throw new InvalidDataException(
                    $"Player transform quaternion at physics+0x{_definition.QuaternionOffset:X} is invalid: " +
                    $"({quaternion.X:G6}, {quaternion.Y:G6}, {quaternion.Z:G6}, {quaternion.W:G6}), " +
                    $"length squared {quaternion.LengthSquared():G6}.");
            ValidatePosition(sqtPosition, worldPosition);
            var up = Vector3.Transform(Vector3.UnitY, quaternion);
            var forward = -Vector3.Transform(Vector3.UnitZ, quaternion);
            if (!Unit(up) || !Unit(forward) || Math.Abs(Vector3.Dot(up, forward)) > .002f)
                throw new InvalidDataException("Player quaternion produced an invalid basis.");
            return new TransformResult(forward, up);
        }
        if (_definition.TransformLayout != "basis-v1")
            throw new InvalidDataException("Unsupported player transform layout.");

        var length = Math.Max(_definition.PositionOffset + 0x10,
            Math.Max(_definition.BasisXOffset + 0x0C, Math.Max(_definition.BasisZOffset + 0x0C, _definition.BasisYOffset + 0x0C)));
        var bytes = reader.Read(ToIntPtr(physics), length);
        var x = Vector(bytes, _definition.BasisXOffset);
        var y = Vector(bytes, _definition.BasisYOffset);
        var z = Vector(bytes, _definition.BasisZOffset);
        var position = Vector(bytes, _definition.PositionOffset);
        if (!Finite(x) || !Finite(y) || !Finite(z) || !Finite(position))
            throw new InvalidDataException("Player physics basis is not finite.");
        if (Math.Abs(x.LengthSquared() - 1) > .002f || Math.Abs(y.LengthSquared() - 1) > .002f ||
            Math.Abs(z.LengthSquared() - 1) > .002f || Math.Abs(Vector3.Dot(x, y)) > .002f ||
            Math.Abs(Vector3.Dot(x, z)) > .002f || Math.Abs(Vector3.Dot(y, z)) > .002f)
            throw new InvalidDataException("Player physics basis is not orthonormal.");
        var determinant = Vector3.Dot(Vector3.Cross(x, y), z);
        if (Math.Abs(determinant - 1) > .003f)
            throw new InvalidDataException("Player physics basis has an invalid handedness.");
        ValidatePosition(position, worldPosition);
        return new TransformResult(-z, y);
    }

    /// <summary>
    /// How far the physics chunk-local position may sit from the player global,
    /// after removing the 1000-unit chunk origin, before the object is rejected as
    /// not the player's.
    /// </summary>
    /// <remarks>
    /// This is an identity check, not a freshness check: it establishes that the
    /// object the chain reached belongs to the player rather than to some other
    /// actor. The two values are written at different points within a frame, so a
    /// sample taken between those writes sees them differ by one frame of travel.
    /// Measured while riding: 3.6% of samples exceeded the previous 0.15 tolerance,
    /// peaking at 0.28 units, and an immediate re-read rescued none of them because
    /// it lands inside the same sub-millisecond window. Demanding same-frame
    /// agreement therefore drops perfectly good samples for a property the reader
    /// does not need — an orientation one frame old is indistinguishable to any
    /// consumer.
    /// Two units still rejects any unrelated object by a wide margin: a position
    /// would have to land within two units of an exact multiple of 1000 from the
    /// player on both horizontal axes and within three units in height.
    /// </remarks>
    private const float PositionAgreementTolerance = 2f;

    private static void ValidatePosition(Vector3 position, (float X, float Y, float Z) worldPosition)
    {
        var delta = new Vector3(worldPosition.X, worldPosition.Y, worldPosition.Z) - position;
        if (!Finite(delta) || Math.Abs(delta.Y) > 3 || Math.Abs(delta.X) > 1_000_000 || Math.Abs(delta.Z) > 1_000_000 ||
            Math.Abs(delta.X - MathF.Round(delta.X / 1000) * 1000) > PositionAgreementTolerance ||
            Math.Abs(delta.Z - MathF.Round(delta.Z / 1000) * 1000) > PositionAgreementTolerance)
            throw new InvalidDataException(
                $"Player transform position ({position.X:F3}, {position.Y:F3}, {position.Z:F3}) does not match the " +
                $"player position ({worldPosition.X:F3}, {worldPosition.Y:F3}, {worldPosition.Z:F3}); " +
                $"delta ({delta.X:F3}, {delta.Y:F3}, {delta.Z:F3}).");
    }

    private PlayerOrientationSnapshot ToSnapshot(TransformResult transform)
    {
        var forward = transform.Forward;
        var horizontalLength = MathF.Sqrt(forward.X * forward.X + forward.Z * forward.Z);
        float? heading = horizontalLength < .1f
            ? null
            : NormalizeDegrees(MathF.Atan2(forward.X, forward.Z) * 180 / MathF.PI);
        return new PlayerOrientationSnapshot("player-physics-root", ToVector(forward),
            ToVector(transform.Up), heading);
    }

    private void CheckType(IReadOnlyProcessMemory reader, ulong address, int expectedIndex, string step)
    {
        if (_definition.ExpectedTypeNames.Count <= expectedIndex) return;
        var expected = _definition.ExpectedTypeNames[expectedIndex];
        var actual = TypeName(reader, address);
        if (!string.Equals(actual, expected, StringComparison.Ordinal))
            throw new InvalidDataException(
                $"Player chain step '{step}' at 0x{address:X} is {actual ?? "not a typed object"}, expected {expected}.");
    }

    private static ulong ReadNext(IReadOnlyProcessMemory reader, ulong address, int offset,
        string from, string to)
    {
        if (address is < 0x10000 or > 0x00007FFFFFFFFFFF)
            throw new InvalidDataException(
                $"Player chain step '{from}' -> '{to}' cannot be followed: '{from}' is 0x{address:X}.");
        return ReadPointer(reader, checked(address + checked((ulong)offset)));
    }

    private static ulong ReadPointer(IReadOnlyProcessMemory reader, ulong address) =>
        BitConverter.ToUInt64(reader.Read(ToIntPtr(address), sizeof(ulong)));

    private static string? TypeName(IReadOnlyProcessMemory reader, ulong address)
    {
        var vtable = ReadPointer(reader, address);
        if (vtable < 8) return null;
        var locator = ReadPointer(reader, vtable - 8);
        var bytes = reader.Read(ToIntPtr(locator), 24);
        if (BitConverter.ToUInt32(bytes, 0) != 1) return null;
        var imageBase = checked(locator - BitConverter.ToUInt32(bytes, 20));
        var nameAddress = checked(imageBase + BitConverter.ToUInt32(bytes, 12) + 16);
        var name = Encoding.ASCII.GetString(reader.Read(ToIntPtr(nameAddress), 160));
        var end = name.IndexOf('\0');
        return end > 0 ? name[..end] : null;
    }

    private static Vector3 Vector(byte[] bytes, int offset) => new(
        BitConverter.ToSingle(bytes, offset), BitConverter.ToSingle(bytes, offset + 4),
        BitConverter.ToSingle(bytes, offset + 8));

    private static bool Finite(Vector3 value) => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);
    private static bool Unit(Vector3 value) => Finite(value) && Math.Abs(value.LengthSquared() - 1) < .002f;
    private static CameraVector3 ToVector(Vector3 value) => new(value.X, value.Y, value.Z);
    private static float NormalizeDegrees(float value) => (value % 360 + 360) % 360;

    private static IntPtr ToIntPtr(ulong address)
    {
        if (address is < 0x10000 or > 0x00007FFFFFFFFFFF)
            throw new InvalidDataException($"Implausible player pointer 0x{address:X}.");
        return new IntPtr(unchecked((long)address));
    }

    private readonly record struct ChainResult(ulong PhysicsAddress);
    private readonly record struct TransformResult(Vector3 Forward, Vector3 Up);
}
