"""Moves the date of a decompressed save, for the branches of the daily
statistics (0x54DE90) that need a new week, month or year: sets the tick of
the world ([+0x14], file offset 0x78) to the middle of the day DAY (from day
0 of year 0, 365 days a year) and the date fields ([+0x0..+0x5]) to it, as
0x5310D0 would. Everything else is left: the events of the save fall due at
once.

usage: python3 retouch_date.py <in.raw> <out.raw> YEAR DAY_OF_YEAR
e.g. 1301 363: the 30th of December of 1301; the 31st is day 6 of the week
(day number % 7), the 1st of January day 0.
"""
import struct
import sys

MONTH_START = (0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365)


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        sys.exit(2)
    data = bytearray(open(sys.argv[1], 'rb').read())
    year, day_of_year = int(sys.argv[3]), int(sys.argv[4])
    month = max(m for m in range(12) if MONTH_START[m] <= day_of_year)
    world = 0x60
    struct.pack_into('<BBHH', data, world, day_of_year - MONTH_START[month] + 1, month, year, day_of_year)
    struct.pack_into('<I', data, world + 0x18, (year * 365 + day_of_year) * 256 + 0x80)
    open(sys.argv[2], 'wb').write(data)
    print(f'day {(year * 365 + day_of_year)} (weekday {(year * 365 + day_of_year) % 7}) in {sys.argv[2]}')


if __name__ == '__main__':
    main()
