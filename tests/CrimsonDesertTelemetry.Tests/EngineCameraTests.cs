using CrimsonDesertTelemetry.Core;

internal static class EngineCameraTests
{
    public static void Mapping()
    {
        var fixture = new Fixture();
        var camera = fixture.Reader.Capture((10, 20, 30));
        Check(camera.Camera.Position == new CameraVector3(10, 20, 30) &&
            camera.Camera.Right == new CameraVector3(1, 0, 0) && camera.Camera.Up == new CameraVector3(0, 1, 0) &&
            camera.Camera.Forward == new CameraVector3(0, 0, 1), "Wrong native vector mapping.");
        Check(Math.Abs(camera.Camera.FieldOfViewRadians * 180 / MathF.PI - 50) < .001f &&
            Math.Abs(camera.Camera.AspectRatio - 16f / 9) < .0001f, "Wrong native projection mapping.");
        Check(camera.Camera.FarPlane == float.MaxValue && camera.ConsensusCopies == 1 &&
            camera.ValidCopies == 1 && camera.DistinctStates == 1 && !camera.Rediscovered,
            "Far plane or single-source quality mismatch.");
        F(fixture.Source, 0x864, 12345);
        Check(fixture.Reader.Capture((10, 20, 30)).Camera.FarPlane == float.MaxValue,
            "Unvalidated raw far field was exported.");
    }

    public static void RebasedColdStart()
    {
        var first = new Fixture();
        var second = new Fixture(0x180000000, 0x900000);
        Check(first.Reader.Capture((10, 20, 30)).Camera.Address != second.Reader.Capture((10, 20, 30)).Camera.Address,
            "Camera reader reused the previous process address.");
    }

    public static void InstructionGuards()
    {
        var fixture = new Fixture();
        var definition = fixture.Definition;
        var code = fixture.Memory.Blocks[fixture.Base + definition.CameraReferenceRva];
        code[7] ^= 1;
        Reject(() => fixture.CreateReader(), "Wrong camera instruction accepted.");
        code[7] ^= 1;
        var displacement = BitConverter.ToInt32(code, 3);
        BitConverter.GetBytes(displacement + 8).CopyTo(code, 3);
        Reject(() => fixture.CreateReader(), "Wrong RIP target accepted.");
        BitConverter.GetBytes(displacement).CopyTo(code, 3);
        definition.Layout = "unknown";
        Reject(() => fixture.CreateReader(), "Unsupported native layout accepted.");
    }

    public static void RootsAndTypes()
    {
        var fixture = new Fixture();
        fixture.Memory.Q(fixture.Base + fixture.Definition.CameraGlobalRva, fixture.Camera + 8);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Conflicting roots accepted.");
        fixture.Memory.Q(fixture.Base + fixture.Definition.CameraGlobalRva, fixture.Camera);
        fixture.Memory.Q(fixture.Camera, fixture.Base + fixture.Definition.CameraVtableRva + 8);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Wrong camera vtable accepted.");
        fixture.Memory.Q(fixture.Camera, fixture.Base + fixture.Definition.CameraVtableRva);
        fixture.Memory.Q(fixture.Context, fixture.Base + fixture.Definition.ContextVtableRva + 8);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Wrong context vtable accepted.");
        fixture.Memory.Q(fixture.Context, fixture.Base + fixture.Definition.ContextVtableRva);
        fixture.Memory.Q(fixture.Camera + 0x428, 0);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Null source pointer accepted.");
    }

    public static void InvalidData()
    {
        void Bad(Action<byte[]> mutate, string message)
        {
            var bytes = SourceBytes(); mutate(bytes);
            Reject(() => EngineCameraReader.Decode(bytes, 0x500000, (10, 20, 30)), message);
        }
        Bad(bytes => F(bytes, 0x3F4, 0), "Non-unit basis accepted.");
        Bad(bytes => F(bytes, 0x98, -1), "Reflected basis accepted.");
        Bad(bytes => F(bytes, 0x80, float.NaN), "Non-finite position accepted.");
        Bad(bytes => F(bytes, 0x80, 1000), "Distant camera accepted.");
        Bad(bytes => F(bytes, 0x860, 0), "Invalid near plane accepted.");
        Bad(bytes => F(bytes, 0x50C, 0), "Orthographic projection accepted.");
        Bad(bytes => F(bytes, 0x500, .3f), "Off-center projection accepted.");
        Bad(bytes => F(bytes, 0x4E0, -1), "Negative projection scale accepted.");
        Bad(bytes => F(bytes, 0x508, float.NaN), "Non-finite projection accepted.");
        Reject(() => EngineCameraReader.Decode(new byte[10], 0x500000, (10, 20, 30)), "Truncated source accepted.");
        Reject(() => EngineCameraReader.Decode(SourceBytes(), 0x500000, (float.NaN, 20, 30)), "Non-finite player accepted.");
    }

    public static void ConcurrentUpdates()
    {
        var fixture = new Fixture();
        var sourceReads = 0;
        fixture.Memory.BeforeRead = (address, _) =>
        {
            if (address == fixture.SourceAddress) F(fixture.Source, 0x80, 10 + ++sourceReads * .01f);
        };
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Changing fields accepted as coherent.");
        Check(sourceReads == 6, "Native capture exceeded its three-attempt budget.");
        fixture.Memory.BeforeRead = (address, _) =>
        {
            if (address == fixture.SourceAddress) fixture.Memory.U(fixture.Context + 0x40, ++fixture.Counter);
        };
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Changing render counter accepted.");
        fixture.Memory.BeforeRead = null;
        _ = fixture.Reader.Capture((10, 20, 30));
    }

    public static void LoadingAndRelocation()
    {
        var fixture = new Fixture();
        _ = fixture.Reader.Capture((10, 20, 30));
        fixture.Memory.Q(fixture.Camera + 0x428, 0);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Loading reused the last good camera.");
        var replacement = fixture.SourceAddress + 0x10000;
        fixture.Memory.Blocks[replacement] = SourceBytes();
        fixture.Memory.Q(fixture.Camera + 0x428, replacement);
        var frame = fixture.Reader.Capture((10, 20, 30));
        Check(frame.Rediscovered && frame.Camera.Address == replacement, "Replacement source was not followed.");
        Check(!fixture.Reader.Capture((10, 20, 30)).Rediscovered, "Relocation flag did not reset.");
    }

    public static void Freshness()
    {
        var fixture = new Fixture();
        _ = fixture.Reader.Capture((10, 20, 30));
        fixture.Now = TimeSpan.FromMilliseconds(900);
        _ = fixture.Reader.Capture((10, 20, 30));
        fixture.Now = TimeSpan.FromSeconds(1);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Stalled render context was still published.");
        fixture.Memory.U(fixture.Context + 0x40, ++fixture.Counter);
        _ = fixture.Reader.Capture((10, 20, 30));
        fixture.Now = TimeSpan.FromSeconds(2);
        fixture.Memory.U(fixture.Context + 0x40, uint.MaxValue);
        _ = fixture.Reader.Capture((10, 20, 30));
        fixture.Now = TimeSpan.FromSeconds(3);
        fixture.Memory.U(fixture.Context + 0x40, 0);
        _ = fixture.Reader.Capture((10, 20, 30));
    }

    public static void ResumeBoundary()
    {
        var fixture = new Fixture();
        _ = fixture.Reader.Capture((10, 20, 30));
        // A menu may have advanced the render frame while acquisition was paused.
        fixture.Memory.U(fixture.Context + 0x40, ++fixture.Counter);
        fixture.Reader.RequirePostResumeFrame();
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "The first cached menu camera passed resume.");
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "An unchanged resume baseline passed.");
        fixture.Memory.U(fixture.Context + 0x40, ++fixture.Counter);
        _ = fixture.Reader.Capture((10, 20, 30));
        _ = fixture.Reader.Capture((10, 20, 30));
    }

    public static void DirectCameraLayout()
    {
        var fixture = new DirectFixture();
        Check(fixture.Reader.Capture((10, 20, 30)).Camera.Address == DirectFixture.SourceAddress,
            "Direct camera layout did not resolve its source.");
        fixture.Memory.Q(DirectFixture.Camera, DirectFixture.Base + fixture.Definition.CameraVtableRva + 8);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Direct camera layout accepted the wrong camera type.");

        var updated = new DirectFixture(0x430, 0x2D0);
        const ulong oldSource = 0x220000;
        updated.Memory.Q(DirectFixture.Camera + 0x428, oldSource);
        updated.Memory.Blocks[oldSource] = SceneBytes();
        Check(updated.Reader.Capture((10, 20, 30)).Camera.Address == DirectFixture.SourceAddress,
            "Updated camera layout used the obsolete pointer despite a valid old-shaped source.");
        updated.Memory.Q(DirectFixture.Camera + 0x430, 0);
        Reject(() => updated.Reader.Capture((10, 20, 30)),
            "Updated camera silently fell back to the obsolete pointer.");
    }

    public static void SceneConstantsValidation()
    {
        var bytes = SceneBytes();
        var scene = SceneConstantsDecoder.Decode(bytes);
        Check(scene.FrameNumber == 42 && scene.ScreenWidth == 3840 && scene.ScreenHeight == 2160 &&
            scene.Camera.Position == new CameraVector3(10, 20, 30) && double.IsNaN(scene.Camera.DistanceFromPlayer),
            "Scene constants required a player anchor or mapped the wrong camera fields.");
        var distant = SceneConstantsDecoder.Decode(bytes, 0x90000, (-1000, 0, -1000));
        Check(distant.Camera.DistanceFromPlayer > 50 && distant.Camera.Address == 0x90000,
            "Self-identifying scene still depends on player proximity.");
        F(bytes, 0x870, 1000); F(bytes, 0x874, 2000); F(bytes, 0x878, 3000);
        Check(SceneConstantsDecoder.Decode(bytes).Camera.Position == scene.Camera.Position,
            "Rendering origin was substituted for the actual view position.");
        void Bad(Action<byte[]> change, string message)
        {
            var invalid = SceneBytes(); change(invalid);
            Reject(() => SceneConstantsDecoder.Decode(invalid), message);
        }
        Bad(data => F(data, 0x38, 1f / 2560), "Mismatched screen reciprocal was accepted.");
        Bad(data => F(data, 0x34, float.NaN), "Non-finite resolution was accepted.");
        Bad(data => F(data, 0xAC0, 1), "Wrong scene layout signature was accepted.");
        Bad(data => F(data, 0x410, 0), "World view translation disagreed with the camera.");
        Bad(data => F(data, 0x450, 10), "Relative view matrix silently introduced another origin.");
        Bad(data => F(data, 0x420, -1), "Relative and world camera bases disagreed.");
        Bad(data => { F(data, 0x408, -1); F(data, 0x448, -1); }, "View matrices disagreed with the view direction.");
        Reject(() => SceneConstantsDecoder.Decode(new byte[0x868]), "Truncated scene constants accepted.");
        Reject(() => SceneConstantsDecoder.Decode(new byte[0xB01]), "Unexpected scene constants layout accepted.");
    }

    public static void SceneConstantsFreshness()
    {
        var fixture = new DirectFixture();
        _ = fixture.Reader.Capture((10, 20, 30));
        fixture.Now = TimeSpan.FromSeconds(1);
        fixture.Memory.U(DirectFixture.Camera + (ulong)fixture.Definition.FrameCounterOffset, 101);
        Reject(() => fixture.Reader.Capture((10, 20, 30)),
            "Advancing context hid a stale SceneConstantBuffer.");
        BitConverter.GetBytes(uint.MaxValue).CopyTo(fixture.Source, 0x20);
        _ = fixture.Reader.Capture((10, 20, 30));
        fixture.Now = TimeSpan.FromSeconds(2);
        BitConverter.GetBytes(0u).CopyTo(fixture.Source, 0x20);
        _ = fixture.Reader.Capture((-1000, 0, -1000));
        var sourceReads = 0;
        fixture.Memory.BeforeRead = (address, _) =>
        {
            if (address == DirectFixture.SourceAddress)
                BitConverter.GetBytes(++sourceReads).CopyTo(fixture.Source, 0x20);
        };
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Two different scene frames accepted as coherent.");
        Check(sourceReads == 6, "Scene constants capture exceeded its bounded retry budget.");
    }

    private sealed class DirectFixture
    {
        public const ulong Base = 0x140000000;
        public const ulong Camera = 0x200000;
        public const ulong SourceAddress = 0x210000;
        public Memory Memory { get; } = new();
        public byte[] Source => Memory.Blocks[SourceAddress];
        public EngineCameraDefinition Definition { get; }
        public EngineCameraReader Reader { get; }
        public TimeSpan Now;

        public DirectFixture(int sourcePointerOffset = 0x428, int counterOffset = 0x2C8)
        {
            Definition = BuildDefinition.LoadAll(Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")))
                .Single(value => value.SteamBuildId == "25116796").EngineCamera
                ?? throw new InvalidOperationException("Direct camera definition is not embedded.");
            Definition.SourcePointerOffset = sourcePointerOffset;
            Definition.FrameCounterOffset = counterOffset;
            var code = Definition.CameraReferencePattern.Split(' ')
                .Select(token => token == "??" ? (byte)0 : Convert.ToByte(token, 16)).ToArray();
            BitConverter.GetBytes(checked((int)((long)Definition.CameraGlobalRva -
                (long)Definition.CameraReferenceRva - 7))).CopyTo(code, 3);
            Memory.Blocks[Base + Definition.CameraReferenceRva] = code;
            Memory.Q(Base + Definition.CameraGlobalRva, Camera);
            Memory.Q(Camera, Base + Definition.CameraVtableRva);
            Memory.U(Camera + checked((ulong)Definition.FrameCounterOffset), 100);
            Memory.Q(Camera + checked((ulong)Definition.SourcePointerOffset), SourceAddress);
            Memory.Blocks[SourceAddress] = SceneBytes();
            Reader = new EngineCameraReader(Memory, Base, Definition, () => Now);
        }
    }

    private sealed class Fixture
    {
        public ulong Base { get; }
        public ulong Context { get; }
        public ulong Camera { get; }
        public ulong SourceAddress { get; }
        public byte[] Source => Memory.Blocks[SourceAddress];
        public Memory Memory { get; } = new();
        public EngineCameraDefinition Definition { get; }
        public EngineCameraReader Reader { get; }
        public TimeSpan Now;
        public uint Counter = 100;

        public Fixture(ulong moduleBase = 0x140000000, ulong heap = 0x100000)
        {
            Base = moduleBase;
            Definition = BuildDefinition.LoadAll(Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")))
                .Single(d => d.SteamBuildId == "24994088").EngineCamera
                ?? throw new InvalidOperationException("Native definition is not embedded.");
            var application = heap + 0x10000;
            Context = heap + 0x20000; Camera = heap + 0x30000; SourceAddress = heap + 0x40000;
            Memory.Q(Base + Definition.MainRootGlobalRva, heap);
            Memory.Q(heap + 0x28, application);
            Memory.Q(application + 0x18, Context);
            Memory.Q(Context, Base + Definition.ContextVtableRva);
            Memory.U(Context + 0x40, Counter);
            Memory.Q(Context + 0xE0, Camera);
            Memory.Q(Base + Definition.CameraGlobalRva, Camera);
            Memory.Q(Camera, Base + Definition.CameraVtableRva);
            Memory.Q(Camera + 0x428, SourceAddress);
            Memory.Blocks[SourceAddress] = SourceBytes();
            Code(Definition.MainRootReferenceRva, Definition.MainRootReferencePattern, Definition.MainRootGlobalRva);
            Code(Definition.CameraReferenceRva, Definition.CameraReferencePattern, Definition.CameraGlobalRva);
            Reader = CreateReader();
        }

        public EngineCameraReader CreateReader() => new(Memory, Base, Definition, () => Now);
        private void Code(ulong rva, string pattern, ulong target)
        {
            var bytes = pattern.Split(' ').Select(token => token == "??" ? (byte)0 : Convert.ToByte(token, 16)).ToArray();
            BitConverter.GetBytes(checked((int)((long)target - (long)rva - 7))).CopyTo(bytes, 3);
            Memory.Blocks[Base + rva] = bytes;
        }
    }

    private sealed class Memory : IReadOnlyProcessMemory
    {
        public Dictionary<ulong, byte[]> Blocks { get; } = [];
        public Action<ulong, int>? BeforeRead;
        public void Q(ulong address, ulong value) => Blocks[address] = BitConverter.GetBytes(value);
        public void U(ulong address, uint value) => Blocks[address] = BitConverter.GetBytes(value);
        public byte[] Read(IntPtr address, int length)
        {
            var start = checked((ulong)address.ToInt64());
            BeforeRead?.Invoke(start, length);
            foreach (var (baseAddress, bytes) in Blocks)
                if (start >= baseAddress && start - baseAddress + (ulong)length <= (ulong)bytes.Length)
                    return bytes.AsSpan((int)(start - baseAddress), length).ToArray();
            throw new InvalidDataException("Unmapped test memory.");
        }
        public float ReadSingle(ulong address) => BitConverter.ToSingle(Read(new IntPtr((long)address), 4));
        public IReadOnlyList<MemoryRegion> GetWritableRegions() => throw new InvalidOperationException("Native reader must not scan memory.");
    }

    private static byte[] SourceBytes()
    {
        var bytes = new byte[0x868];
        F(bytes, 0x80, 10); F(bytes, 0x84, 20); F(bytes, 0x88, 30);
        F(bytes, 0x98, 1); F(bytes, 0x3E0, 1); F(bytes, 0x3F4, 1);
        F(bytes, 0x4E0, 1.20628512f); F(bytes, 0x4F4, 2.14450693f);
        F(bytes, 0x50C, 1); F(bytes, 0x518, .2f); F(bytes, 0x860, .2f);
        return bytes;
    }

    internal static byte[] SceneBytes()
    {
        var bytes = new byte[SceneConstantsDecoder.SourceLength];
        SourceBytes().CopyTo(bytes, 0);
        BitConverter.GetBytes(42u).CopyTo(bytes, 0x20);
        F(bytes, 0x30, 3840); F(bytes, 0x34, 2160);
        F(bytes, 0x38, 1f / 3840); F(bytes, 0x3C, 1f / 2160);
        F(bytes, 0x408, 1);
        F(bytes, 0x410, -10); F(bytes, 0x414, -20); F(bytes, 0x418, -30); F(bytes, 0x41C, 1);
        F(bytes, 0x420, 1); F(bytes, 0x434, 1); F(bytes, 0x448, 1); F(bytes, 0x45C, 1);
        F(bytes, 0xAC0, 6360000f);
        return bytes;
    }
    private static void F(byte[] bytes, int offset, float value) => BitConverter.GetBytes(value).CopyTo(bytes, offset);
    private static void Check(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    private static void Reject(Action action, string message)
    {
        try { action(); } catch (InvalidDataException) { return; }
        throw new InvalidOperationException(message);
    }
}
