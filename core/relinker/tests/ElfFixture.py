import struct


def make_elf(nid):
    image = bytearray(0x1000)
    base = 0x400000
    strings = b"\0" + nid.encode("utf-8") + b"\0"
    tags = [
        (5, base + 0x400), (10, len(strings)),
        (6, base + 0x500), (11, 24),
        (7, base + 0x600), (8, 24), (9, 24), (0, 0),
    ]
    ident = b"\x7fELF\x02\x01\x01\x09" + bytes(8)
    struct.pack_into("<16sHHIQQQIHHHHHH", image, 0, ident, 2, 62, 1,
                     base + 0x800, 64, 0, 0, 64, 56, 8, 64, 0, 0)
    struct.pack_into("<IIQQQQQQ", image, 64, 1, 5, 0, base, base,
                     len(image), len(image), 0x1000)
    struct.pack_into("<IIQQQQQQ", image, 120, 2, 6, 0x200,
                     base + 0x200, base + 0x200, len(tags) * 16, len(tags) * 16, 8)
    for index in range(2, 8):
        struct.pack_into("<IIQQQQQQ", image, 64 + index * 56, 4, 0, 0, 0, 0, 1, 1, 1)
    for index, tag in enumerate(tags):
        struct.pack_into("<qQ", image, 0x200 + index * 16, *tag)
    image[0x400:0x400 + len(strings)] = strings
    struct.pack_into("<IBBHQQ", image, 0x500 + 24, 1, 0x12, 0, 0, 0, 0)
    struct.pack_into("<QQq", image, 0x600, base + 0x700, (1 << 32) | 6, 0)
    image[0x800] = 0xC3
    return image
