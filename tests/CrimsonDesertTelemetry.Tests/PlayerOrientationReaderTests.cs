using System.Text;
using CrimsonDesertTelemetry.Core;

internal static class PlayerOrientationReaderTests
{
    public static void RealChain()
    {
        var fixture = new Fixture();
        var snapshot = fixture.Reader.Read(fixture, Fixture.Player);
        Check(snapshot is { HeadingDegrees: 180 } && snapshot.Forward.Z == -1 && snapshot.Up.Y == 1,
            "Real guarded pointer-chain walk did not produce the player basis: " + fixture.Reader.LastFailureReason);
        // A changed owner-to-physics offset must be diagnosed, not substituted by a camera pose.
        fixture.Q(Fixture.Owner + (ulong)fixture.Root.OwnerToPhysicsOffset, 0);
        Check(fixture.Reader.Read(fixture, Fixture.Player) is null && fixture.Reader.LastFailureReason is not null,
            "Broken physics pointer retained an old orientation.");
        var originalOffset = fixture.Root.OwnerToPhysicsOffset;
        fixture.Q(Fixture.Owner + (ulong)originalOffset + 0x10, Fixture.Physics);
        Check(fixture.Reader.FindPhysicsOffset(fixture, Fixture.Player, originalOffset) == originalOffset + 0x10,
            "Unique shifted physics field was not recovered.");
        fixture.Root.OwnerToPhysicsOffset += 0x10;
        Check(fixture.Reader.Read(fixture, Fixture.Player) is not null, "Recovered field did not pass the full player reader.");
        fixture.Q(Fixture.Owner + (ulong)originalOffset + 0x18, Fixture.Physics);
        try { fixture.Reader.FindPhysicsOffset(fixture, Fixture.Player, originalOffset); }
        catch (InvalidOperationException) { return; }
        throw new InvalidOperationException("Ambiguous physics fields were accepted.");
    }

    public static void IdentityAndRaces()
    {
        var wrongType = new Fixture();
        wrongType.Put(0x143010, Encoding.ASCII.GetBytes("wrong-manager\0"));
        Check(wrongType.Reader.Read(wrongType, Fixture.Player) is null &&
              wrongType.Reader.LastFailureReason!.Contains("actorManager"), "Wrong RTTI identity accepted.");
        var wrongPosition = new Fixture();
        wrongPosition.F(Fixture.Physics + (ulong)wrongPosition.Root.PositionOffset, -500.5f);
        Check(wrongPosition.Reader.Read(wrongPosition, Fixture.Player) is null &&
              wrongPosition.Reader.LastFailureReason!.Contains("does not match"), "Plausible non-player pose accepted.");
        Check(wrongPosition.Reader.FindPhysicsOffset(wrongPosition, Fixture.Player,
            wrongPosition.Root.OwnerToPhysicsOffset) is null, "Physics recovery accepted a non-player transform.");
        var wrongBasis = new Fixture();
        wrongBasis.F(Fixture.Physics + (ulong)wrongBasis.Root.BasisXOffset + 4, 1);
        Check(wrongBasis.Reader.Read(wrongBasis, Fixture.Player) is null &&
              wrongBasis.Reader.LastFailureReason!.Contains("orthonormal"), "Invalid basis accepted.");
        Check(wrongBasis.Reader.FindPhysicsOffset(wrongBasis, Fixture.Player,
            wrongBasis.Root.OwnerToPhysicsOffset) is null, "Physics recovery accepted an invalid basis.");
        var racing = new Fixture { ReplaceLeafOnSecondRead = true };
        Check(racing.Reader.Read(racing, Fixture.Player) is null &&
              racing.Reader.LastFailureReason!.Contains("replaced"), "Replaced leaf published orientation.");
    }

    public static void ControlledActorSwitch()
    {
        const ulong kliff = 0x150000, oongka = 0x200000, damiane = 0x210000;
        const ulong oongkaPhysics = 0x210800, damianePhysics = 0x220800;
        var fixture = new Fixture();
        var controlled = fixture.ReaderForControlledActor();
        fixture.AttachActor(oongka, oongkaPhysics, (-862.837f, 657.852f, 208.964f), opposite: true);
        fixture.AttachActor(damiane, damianePhysics, (-244, 657, 197));

        fixture.SetPosition(Fixture.Player);
        Check(controlled.ReadPose(fixture, Fixture.Positions)?.Orientation.HeadingDegrees == 180,
            "The selected Kliff actor did not produce a pose.");
        var position = (-9862.837f, 657.515f, 208.964f);
        fixture.SetPosition(position);
        fixture.Q(Fixture.ControlledActorField, oongka);
        Check(fixture.Reader.Read(fixture, position) is null, "The old Kliff chain unexpectedly matched the new position.");
        var switched = controlled.ReadPose(fixture, Fixture.Positions);
        Check(switched is { } oongkaPose && oongkaPose.Position == position &&
              oongkaPose.Orientation.HeadingDegrees == 0, "The selected Oongka actor did not replace Kliff's pose.");
        fixture.SetPosition((-10244, 657, 197));
        fixture.Q(Fixture.ControlledActorField, damiane);
        Check(controlled.ReadPose(fixture, Fixture.Positions)?.Orientation.HeadingDegrees == 180,
            "The selected Damiane actor did not replace Oongka's pose.");

        fixture.Q(Fixture.ControlledActorField, 0);
        fixture.SetPosition(Fixture.Player);
        Check(controlled.ReadPose(fixture, Fixture.Positions) is null,
            "The controlled reader fell back to the original actor during a switch.");
        fixture.Q(Fixture.ControlledActorField, kliff);
        Check(controlled.ReadPose(fixture, Fixture.Positions) is not null, "The reader did not reacquire Kliff.");

        // An actor can change before a stable pose is published, even if the
        // observed physics leaf has not changed yet.
        fixture.AttachActor(oongka, Fixture.Physics, (-507, 610, -369));
        fixture.ActorOnNextPositionRead = oongka;
        Check(controlled.ReadPose(fixture, Fixture.Positions) is null &&
              controlled.LastFailureReason!.Contains("replaced"), "A switch during the position read published a mixed pose.");
    }

    public static void PrivateCandidateGate()
    {
        var research = BuildDefinition.LoadAll("not-a-profile-directory")
            .Single(d => d.SteamBuildId == "25477059");
        // Exercise the private gate with an in-memory candidate; the real build
        // profile is now locally validated and must stay releasable.
        research.Status = "research";
        research.NativeCapture!.Status = "research";
        foreach (var anchor in research.Patterns.Append(research.PlayerRoot!.WorldSystemPattern))
            anchor.Confidence = "candidate";
        var candidate = research.PlayerRoot!;
        Check(candidate.WorldSystemPattern.Confidence == "candidate", "Expected a candidate player anchor.");
        Reject(() => PlayerOrientationReader.FromResolvedAddress(0x110000, candidate),
            "A candidate orientation was accepted by the standard reader.");
        _ = PlayerOrientationReader.FromResolvedAddress(0x110000, candidate, allowResearchCandidate: true);
        foreach (var anchor in research.Patterns.Append(candidate.WorldSystemPattern))
        {
            Reject(() => BuildProfileValidation.ValidateRuntimeAnchor(anchor, research, false, "unvalidated"),
                "A candidate anchor was accepted without private mode.");
            BuildProfileValidation.ValidateRuntimeAnchor(anchor, research, true, "unvalidated");
        }
        research.Status = "locally-validated";
        Reject(() => BuildProfileValidation.ValidateRuntimeAnchor(candidate.WorldSystemPattern, research, true,
                "unvalidated"), "A candidate anchor was accepted in a validated profile.");
    }

    private static void Reject(Action action, string message)
    {
        try { action(); }
        catch (InvalidDataException) { return; }
        throw new InvalidOperationException(message);
    }

    private sealed class Fixture : IReadOnlyProcessMemory
    {
        public const ulong Owner = 0x170000, Physics = 0x180000;
        public const ulong ControlledActorField = 0x1900D8;
        public static readonly StaticPositionAddresses Positions = new(0x1A0000, 0x1A0008);
        public static readonly (float X, float Y, float Z) Player = (-10507, 610, -4369);
        private readonly Dictionary<ulong, byte> _bytes = [];
        public PlayerRootDefinition Root { get; } = UpdateCheckTests.Current().PlayerRoot!;
        public PlayerOrientationReader Reader { get; }
        public bool ReplaceLeafOnSecondRead { get; set; }
        public ulong? ActorOnNextPositionRead { get; set; }
        private int _leafReads;
        public Fixture()
        {
            Q(0x110000, 0x120000);
            Q(0x120000 + (ulong)Root.WorldSystemToActorManagerOffset, 0x130000);
            Q(0x130000 + (ulong)Root.ActorManagerToPlayerActorOffset, 0x150000);
            Q(0x150000 + (ulong)Root.PlayerActorToIntermediateOffset, 0x155000);
            Q(0x155000 + (ulong)Root.IntermediateToControlOffset, 0x160000);
            Q(0x160000 + (ulong)Root.ControlToOwnerOffset, Owner);
            Q(Owner + (ulong)Root.OwnerToPhysicsOffset, Physics);
            var typedObjects = new[] { 0x130000UL, 0x150000UL, 0x160000UL };
            for (var index = 0; index < 3; index++)
                SetType(typedObjects[index], index, Root.ExpectedTypeNames[index]);
            SetPhysics(Physics, (-507, 610, -369), opposite: false);
            Reader = PlayerOrientationReader.FromResolvedAddress(0x110000, Root);
        }
        public PlayerOrientationReader ReaderForControlledActor()
        {
            Q(0x130058, 0x190000);
            SetType(0x190000, 3, ".?AVClientUserActor@pa@@");
            Q(ControlledActorField, 0x150000);
            return PlayerOrientationReader.FromResolvedAddress(0x110000, Root, useControlledActor: true);
        }
        public void AttachActor(ulong actor, ulong physics, (float X, float Y, float Z) position, bool opposite = false)
        {
            var intermediate = actor + 0x200;
            var control = actor + 0x400;
            var owner = actor + 0x800;
            SetType(actor, 1, Root.ExpectedTypeNames[1]);
            Q(actor + (ulong)Root.PlayerActorToIntermediateOffset, intermediate);
            Q(intermediate + (ulong)Root.IntermediateToControlOffset, control);
            SetType(control, 2, Root.ExpectedTypeNames[2]);
            Q(control + (ulong)Root.ControlToOwnerOffset, owner);
            Q(owner + (ulong)Root.OwnerToPhysicsOffset, physics);
            SetPhysics(physics, position, opposite);
        }
        public void SetPosition((float X, float Y, float Z) position)
        {
            F(Positions.XyAddress, position.X);
            F(Positions.XyAddress + 4, position.Y);
            F(Positions.ZAddress, position.Z);
        }
        private void SetType(ulong address, int index, string name)
        {
            var vtable = (ulong)(0x141000 + index * 0x100);
            var locator = (ulong)(0x142000 + index * 0x100);
            var type = (ulong)(0x143000 + index * 0x100);
            Q(address, vtable); Q(vtable - 8, locator);
            Put(locator, new byte[24]); U(locator, 1); U(locator + 12, (uint)(type - 0x140000));
            U(locator + 20, (uint)(locator - 0x140000));
            Put(type + 16, new byte[160]); Put(type + 16, Encoding.ASCII.GetBytes(name + '\0'));
        }
        private void SetPhysics(ulong address, (float X, float Y, float Z) position, bool opposite)
        {
            Put(address, new byte[0xA0]);
            F(address + (ulong)Root.BasisXOffset, opposite ? -1 : 1);
            F(address + (ulong)Root.BasisYOffset + 4, 1);
            F(address + (ulong)Root.BasisZOffset + 8, opposite ? -1 : 1);
            F(address + (ulong)Root.PositionOffset, position.X);
            F(address + (ulong)Root.PositionOffset + 4, position.Y);
            F(address + (ulong)Root.PositionOffset + 8, position.Z);
        }
        public byte[] Read(IntPtr address, int length)
        {
            var start = checked((ulong)address.ToInt64());
            if (start == Positions.XyAddress && ActorOnNextPositionRead is { } next)
            {
                Q(ControlledActorField, next);
                ActorOnNextPositionRead = null;
            }
            if (start == Owner + (ulong)Root.OwnerToPhysicsOffset && ++_leafReads == 2 && ReplaceLeafOnSecondRead)
                return BitConverter.GetBytes(Physics + 0x1000);
            var result = new byte[length];
            for (var index = 0; index < length; index++)
                if (!_bytes.TryGetValue(start + (ulong)index, out result[index]))
                    throw new InvalidDataException("Outside fixture memory.");
            return result;
        }
        public float ReadSingle(ulong address) => BitConverter.ToSingle(Read(new((long)address), 4));
        public IReadOnlyList<MemoryRegion> GetWritableRegions() => throw new InvalidOperationException("The real chain must not heap-scan.");
        public void Put(ulong address, byte[] bytes)
        { for (var index = 0; index < bytes.Length; index++) _bytes[address + (ulong)index] = bytes[index]; }
        public void Q(ulong address, ulong value) => Put(address, BitConverter.GetBytes(value));
        private void U(ulong address, uint value) => Put(address, BitConverter.GetBytes(value));
        public void F(ulong address, float value) => Put(address, BitConverter.GetBytes(value));
    }
    private static void Check(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
}
