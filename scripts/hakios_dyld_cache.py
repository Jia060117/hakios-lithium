"""Read-only inspection of the original arm64 dyld cache for versioned ABI audits."""
import mmap
import struct


class Cache:
    def __init__(self, path):
        self.path = path
        self.file = path.open('rb')
        self.data = mmap.mmap(self.file.fileno(), 0, access=mmap.ACCESS_READ)
        assert self.data[:16] == b'dyld_v1   arm64\0'
        off, count, images_off, images_count = struct.unpack_from('<4I', self.data, 16)
        self.maps = [struct.unpack_from('<3Q2I', self.data, off + i * 32) for i in range(count)]
        self.images = {}
        for i in range(images_count):
            addr, _, _, name, _ = struct.unpack_from('<3Q2I', self.data, images_off + i * 32)
            self.images[self.string(name)] = addr
        self.local_off, self.local_size = struct.unpack_from('<2Q', self.data, 72)
        self.uuid = self.data[88:104].hex()
        slide_off, slide_count = struct.unpack_from('<2I', self.data, 312)
        self.slide_maps = [struct.unpack_from('<6Q2I', self.data, slide_off + i * 56) for i in range(slide_count)]

    def string(self, off):
        assert 0 <= off < len(self.data)
        end = self.data.find(b'\0', off, min(off + 16384, len(self.data)))
        assert end >= off, 'Unterminated string'
        return self.data[off:end].decode('utf-8', errors='replace')

    def offset(self, address, size=1):
        for base, length, offset, _, _ in self.maps:
            if base <= address and address + size <= base + length:
                return offset + address - base
        raise ValueError('Address outside cache mappings: ' + hex(address))

    def read(self, address, size):
        off = self.offset(address, size)
        return self.data[off:off + size]

    def pointer(self, address):
        raw, = struct.unpack('<Q', self.read(address, 8))
        for base, length, offset, slide, slide_size, flags, maximum, initial in self.slide_maps:
            if base <= address < base + length and slide_size:
                version, page_size, starts, starts_count, extras, extras_count, delta, add = struct.unpack_from('<6I2Q', self.data, slide)
                assert version == 2, 'Audit requires version-2 non-authenticated arm64 slides'
                value = raw & ~delta
                return value + add if value else 0
        return raw

    def symbols(self, image):
        target = self.offset(self.images[image])
        no, nc, so, ss, eo, ec = struct.unpack_from('<6I', self.data, self.local_off)
        assert no + nc * 16 <= self.local_size and so + ss <= self.local_size
        matches = []
        for i in range(ec):
            dylib, start, count = struct.unpack_from('<3I', self.data, self.local_off + eo + i * 12)
            if dylib == target:
                assert start + count <= nc
                for j in range(start, start + count):
                    name, kind, section, desc, value = struct.unpack_from('<IBBHQ', self.data, self.local_off + no + j * 16)
                    assert name < ss
                    matches.append((self.string(self.local_off + so + name), value, kind))
        assert matches, 'No original local symbols for ' + image
        header = self.offset(self.images[image], 32)
        magic, cpu, sub, kind, commands, command_bytes, flags, reserved = struct.unpack_from('<8I', self.data, header)
        assert magic == 0xfeedfacf and cpu == 0x100000c
        cursor = header + 32
        for _ in range(commands):
            command, length = struct.unpack_from('<2I', self.data, cursor)
            assert length >= 8 and cursor + length <= header + 32 + command_bytes
            if command == 2:
                symoff, count, stroff, strsize = struct.unpack_from('<4I', self.data, cursor + 8)
                assert symoff + count * 16 <= len(self.data) and stroff + strsize <= len(self.data)
                for j in range(count):
                    name, typ, section, desc, value = struct.unpack_from('<IBBHQ', self.data, symoff + j * 16)
                    assert name < strsize
                    if value and typ & 0x0e == 0x0e:
                        matches.append((self.string(stroff + name), value, typ))
            cursor += length
        return sorted(set(matches), key=lambda row: (row[1], row[0]))

    def close(self):
        self.data.close()
        self.file.close()
