namespace Xui.Experimental.Portable;

public sealed class Reveal : Element
{
    private readonly string name;
    private readonly Element content;
    private (bool Open, RevealMotion Motion) state = (false, RevealMotion.Default);

    public string Name => Read(name);
    public Element Content => Read(content);
    public bool Open { get => Read(state).Open; set => SetOpen(value); }
    public RevealMotion Motion { get => Read(state).Motion; set => SetMotion(value); }

    internal Reveal(Host host, Element content, string name) : base(host, ElementKind.Reveal)
    {
        this.content = content;
        this.name = Values.Text(name);
    }

    public bool TrySetOpen(bool value) => TrySetState(value, Motion);
    public Reveal SetOpen(bool value) => SetState(value, Motion);
    public Reveal SetMotion(RevealMotion value) => SetState(Open, value);

    public bool TrySetState(bool open, RevealMotion motion) => Owner.RevealOperation(() => TrySetStateCore(open, motion));

    private bool TrySetStateCore(bool open, RevealMotion motion)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        if (state == (open, motion)) return true;
        bool committed = false;
        try
        {
            ArgumentNullException.ThrowIfNull(motion);
            if (state.Motion != motion) Owner.VerifyRevealMotionSupport(this, motion);
            if (state.Open != open && !Owner.CanSetRevealOpen(this, open))
            {
                if (open) throw new InvalidOperationException("A native Reveal veto may only reject closing focused or composing content.");
                return false;
            }
            state = (open, motion);
            if (!open) Owner.ResetRangePreviews(this);
            committed = true;
            Owner.Update(this, ElementProperty.RevealState);
            return true;
        }
        catch (Exception error) { throw new KeyedUpdateException(committed, error); }
    }

    public Reveal SetState(bool open, RevealMotion motion)
    {
        if (!TrySetState(open, motion))
            throw new KeyedUpdateException(false, new InvalidOperationException("Reveal cannot close while its native focus or composition prevents safe hiding."));
        return this;
    }

}
