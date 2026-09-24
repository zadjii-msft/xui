namespace Xui.Experimental.Portable;

/// <summary>Identity and zero-based position of one mounted row in its prepared logical source.</summary>
public readonly record struct VirtualItemInfo(string Key, int Index, int Count, long SourceVersion)
{
    public const int MaximumKeyLength = 4096;

    public void Validate()
    {
        ValidateKey(Key);
        ArgumentOutOfRangeException.ThrowIfNegative(Index);
        if (Count <= Index) throw new ArgumentOutOfRangeException(nameof(Count), "A row index must be inside its declared collection.");
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(SourceVersion);
    }

    internal static void ValidateKey(string key)
    {
        ArgumentException.ThrowIfNullOrEmpty(key);
        Values.Text(key);
        if (key.Length > MaximumKeyLength)
            throw new ArgumentOutOfRangeException(nameof(key), "A virtual item key cannot exceed 4096 UTF-16 code units.");
        for (int i = 0; i < key.Length; i++)
        {
            if (char.IsHighSurrogate(key[i]))
            {
                if (i + 1 >= key.Length || !char.IsLowSurrogate(key[i + 1]))
                    throw new ArgumentException("A virtual item key cannot contain an unpaired surrogate.", nameof(key));
                i++;
            }
            else if (char.IsLowSurrogate(key[i]))
                throw new ArgumentException("A virtual item key cannot contain an unpaired surrogate.", nameof(key));
        }
    }
}

/// <summary>Optional native list-item semantics for an actual row root, not its automation identifier.</summary>
public interface IVirtualItemPeer : IElementPeer
{
    void SetVirtualItemInfo(VirtualItemInfo info);
}
