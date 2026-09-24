using System;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace PortableDemo;

public static class DynamicTaskBoardCodec
{
    public static string Serialize(DynamicTaskBoardState state) =>
        JsonSerializer.Serialize(state ?? throw new ArgumentNullException(nameof(state)), DynamicTaskJsonContext.Default.DynamicTaskBoardState);
    public static DynamicTaskBoardState Restore(string json) =>
        JsonSerializer.Deserialize(json, DynamicTaskJsonContext.Default.DynamicTaskBoardState) ??
        throw new JsonException("Dynamic task state cannot be null.");
}

[JsonSourceGenerationOptions(RespectRequiredConstructorParameters = true, UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow)]
[JsonSerializable(typeof(DynamicTaskBoardState))]
internal partial class DynamicTaskJsonContext : JsonSerializerContext;
