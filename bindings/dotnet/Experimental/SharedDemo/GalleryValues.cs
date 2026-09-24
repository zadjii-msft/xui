using System;
using System.Globalization;

namespace PortableDemo;

internal static class GalleryValues
{
    public static string Text(string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        if (value.Contains('\0')) throw new ArgumentException("Text cannot contain NUL.", nameof(value));
        return value;
    }

    public static T Defined<T>(T value) where T : struct, Enum =>
        Enum.IsDefined(value) ? value : throw new ArgumentOutOfRangeException(nameof(value));

    public static int Slot(int value) => value is >= 1 and <= 3
        ? value : throw new ArgumentOutOfRangeException(nameof(value), "Choose a priority slot from 1 to 3.");

    public static decimal? Amount(string text)
    {
        string value = text.Trim();
        int dot = value.IndexOf('.');
        if (value.Length == 0 || dot == 0 || dot == value.Length - 1 ||
            (dot >= 0 && value.Length - dot - 1 > 2)) return null;
        foreach (char c in value)
            if (c != '.' && (c < '0' || c > '9')) return null;
        return decimal.TryParse(value, NumberStyles.AllowDecimalPoint, CultureInfo.InvariantCulture, out decimal amount) &&
            amount <= 999999.99m ? amount : null;
    }

    public static int? Minutes(string text, int minimum, int maximum)
    {
        string value = text.Trim();
        foreach (char c in value)
            if (c < '0' || c > '9') return null;
        return int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out int minutes) &&
            minutes >= minimum && minutes <= maximum ? minutes : null;
    }

    public static int? StartMinute(string text)
    {
        string value = text.Trim();
        if (value.Length != 5 || value[2] != ':') return null;
        if (value[0] < '0' || value[0] > '9' || value[1] < '0' || value[1] > '9' ||
            value[3] < '0' || value[3] > '9' || value[4] < '0' || value[4] > '9') return null;
        int? hours = Minutes(value[..2], 0, 23);
        int? minutes = Minutes(value[3..], 0, 59);
        return hours * 60 + minutes;
    }

    public static string Money(decimal amount) => "$" + amount.ToString("0.00", CultureInfo.InvariantCulture);
    public static string Number(int value) => value.ToString(CultureInfo.InvariantCulture);
    public static string Clock(int minute) =>
        ((minute / 60) % 24).ToString("00", CultureInfo.InvariantCulture) + ":" +
        (minute % 60).ToString("00", CultureInfo.InvariantCulture);
}
