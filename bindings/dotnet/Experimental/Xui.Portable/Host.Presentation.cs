namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    private ThemeSettings? theme;

    /// <summary>Null inherits the native host appearance; explicit System follows the operating-system scheme.</summary>
    public ThemeSettings? Theme
    {
        get { VerifyAccess(); return theme; }
        set
        {
            VerifyMutation();
            if (building is not null || reconciling != 0)
                throw new InvalidOperationException("Complete construction and keyed updates before changing the host theme.");
            if (theme == value) return;
            var current = attachment;
            var backend = current is null ? null : ThemeBackend(current.Backend, value);
            if (backend is not null)
                InputOperation(() => { backend.ValidateTheme(value); return true; });
            theme = value;
            if (current is not null && backend is not null)
                UpdateAttachment(current, () => backend.ApplyTheme(value));
        }
    }

    private static IThemeBackend? ThemeBackend(IBackend backend, ThemeSettings? value)
    {
        if (backend is IThemeBackend presentation) return presentation;
        if (value is not null)
            throw new NotSupportedException("Explicit theme selection requires an IThemeBackend.");
        return null;
    }

    private void ValidateThemeBackend(IBackend backend)
    {
        if (theme is not null && ThemeBackend(backend, theme) is { } presentation)
            InputOperation(() => { presentation.ValidateTheme(theme); return true; });
    }

    private void ApplyAttachedTheme(IBackend backend)
    {
        if (theme is not null && ThemeBackend(backend, theme) is { } presentation)
            InputOperation(() => { presentation.ApplyTheme(theme); return true; });
    }

    internal void VerifyTypographySupport(Control control, Typography? value)
    {
        if (attachment is { } current && current.Peers.TryGetValue(control, out var peer))
            ValidateTypographyPeer(peer, value);
    }

    private void ValidatePresentationPeer(Element element, IElementPeer peer)
    {
        if (element is Control { Typography: { } typography })
            ValidateTypographyPeer(peer, typography);
    }

    private void ValidateTypographyPeer(IElementPeer peer, Typography? value)
    {
        if (peer is IPresentationPeer presentation)
            InputOperation(() => { presentation.ValidateTypography(value); return true; });
        else if (value is not null)
            throw new NotSupportedException("Explicit typography requires an IPresentationPeer.");
    }
}
