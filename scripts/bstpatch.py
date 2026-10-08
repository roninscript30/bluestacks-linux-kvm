#!/usr/bin/env python3
"""In-place fixups for BlueStacks binaries that do not work as-is under Wine.

  bstpatch.py stub-method FILE Namespace.Type Method
      Turn a void .NET method into an immediate `ret`. Used for
      ServiceManager.SetServicePermissions in the installer's HD-Common.dll,
      which needs ServiceController.ServiceHandle (unimplemented in Wine Mono).

  bstpatch.py rename-import FILE OLD.dll NEW.dll
      Point a PE import descriptor at another DLL (NEW must not be longer
      than OLD). Used to route BstkSVC.exe's DNSAPI.dll import to bstdns.dll.

  bstpatch.py rename-import-func FILE DLL OLD NEW
      Bind an imported function to another export of the same DLL (NEW must
      not be longer than OLD). Used to map BstkProxyStub.dll's NdrStubCall3
      (NDR64, unimplemented in Wine) to NdrStubCall2: MIDL "all protocols"
      stubs also carry the NDR20 format strings, the only syntax Wine speaks.

  bstpatch.py writable-section FILE SECTION
      Mark a PE section writable. Used for BstkProxyStub.dll's .rdata: Wine's
      rpcrt4 (init_psfactory) fills the NULL slots of MIDL's delegating
      proxy/stub vtables in place, which faults when they are const data.

  bstpatch.py force-hyperv HD-Player.exe
      HD-Player.exe only selects BlueStacks' Hyper-V (WHPX) VM path when CPUID
      leaf 0x40000000 reports "Microsoft Hv", which bare-metal Linux never
      does. Make that selection unconditional.

  bstpatch.py driverless-fallback BstkRT.dll
      SUPLib only switches to its (backported) driverless mode when the
      caller asks for it. Let it fall back whenever the support driver cannot
      be opened, which under Wine is always.

  bstpatch.py nem-skip-cpuid-probe BstkVMM.dll
      VirtualBox's NEM probe (nemR3WinInitProbeAndLoad) insists on CPUID
      saying we run inside a Hyper-V partition. Jump straight to loading
      WinHvPlatform.dll, whose answers (our KVM bridge) are what matter.

  bstpatch.py wine-proxy-vtables FILE
      Wine does not export ObjectStublessClientN / NdrProxyForwardingFunctionN
      (calling them aborts with "unimplemented function"). Instead, rpcrt4
      fills proxy vtable slots that hold -1 with its stubless thunks and NULL
      slots of delegated vtables with its forwarding thunks. Rewrite every
      vtable slot that points at those imports accordingly (and drop the
      slot's base relocation so the loader leaves the new value alone).

Both commands are idempotent. Only the standard library is used.
"""

import re
import struct
import sys


def fail(msg):
    sys.exit(f"bstpatch: {msg}")


class PE:
    def __init__(self, data):
        self.data = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            fail("not a PE file")
        nsect, opt_size = struct.unpack_from("<H12xH", data, pe + 6)
        opt = pe + 24
        magic = struct.unpack_from("<H", data, opt)[0]
        self.pe32plus = magic != 0x10B
        self.ddir = opt + (112 if self.pe32plus else 96)
        self.image_base = struct.unpack_from("<Q" if self.pe32plus else "<I",
                                             data, opt + (24 if self.pe32plus else 28))[0]
        self.sections = []
        self.section_headers = {}
        self.code_sections = []  # (rva, raw offset, raw size)
        for i in range(nsect):
            s = opt + opt_size + i * 40
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, s + 8)
            self.sections.append((va, max(vsize, rawsize), rawptr))
            self.section_headers[bytes(data[s:s + 8]).rstrip(b"\0").decode()] = s
            if struct.unpack_from("<I", data, s + 36)[0] & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
                self.code_sections.append((va, rawptr, min(vsize, rawsize)))

    def directory(self, index):
        return struct.unpack_from("<II", self.data, self.ddir + index * 8)

    def off(self, rva):
        for va, size, rawptr in self.sections:
            if va <= rva < va + size:
                return rva - va + rawptr
        fail(f"RVA {rva:#x} not in any section")

    def rva(self, off):
        for va, size, rawptr in self.sections:
            if rawptr <= off < rawptr + size:
                return off - rawptr + va
        fail(f"file offset {off:#x} not in any section")

    def rip_target(self, disp_off):
        """String/data RVA referenced by a rip-relative disp32 at file offset disp_off."""
        disp = struct.unpack_from("<i", self.data, disp_off)[0]
        return self.rva(disp_off + 4) + disp

    def cstr(self, off):
        return bytes(self.data[off:self.data.index(b"\0", off)])


def import_descriptors(pe):
    """Yield (descriptor offset, DLL name offset, DLL name) for each import."""
    rva, _ = pe.directory(1)
    if rva == 0:
        fail("no import directory")
    p = pe.off(rva)
    while True:
        name_rva = struct.unpack_from("<I", pe.data, p + 12)[0]
        if name_rva == 0:
            return
        q = pe.off(name_rva)
        yield p, q, pe.cstr(q).decode()
        p += 20


def overwrite_name(data, off, old, new):
    data[off:off + len(old)] = new.encode().ljust(len(old), b"\0")


def rename_import(data, old, new):
    if len(new) > len(old):
        fail(f"{new} is longer than {old}")
    for _, q, name in import_descriptors(PE(data)):
        if name.lower() == new.lower():
            print(f"import {old} -> {new}: already renamed")
            return
        if name.lower() == old.lower():
            overwrite_name(data, q, name, new)
            print(f"import {old} -> {new}: renamed")
            return
    fail(f"no import of {old}")


def rename_import_func(data, dll, old, new):
    if len(new) > len(old):
        fail(f"{new} is longer than {old}")
    pe = PE(data)
    entry_size, ordinal_flag = (8, 1 << 63) if pe.pe32plus else (4, 1 << 31)
    for p, _, name in import_descriptors(pe):
        if name.lower() != dll.lower():
            continue
        ilt_rva, _, _, _, iat_rva = struct.unpack_from("<IIIII", data, p)
        t = pe.off(ilt_rva or iat_rva)
        while True:
            entry = int.from_bytes(data[t:t + entry_size], "little")
            if entry == 0:
                break
            if not entry & ordinal_flag:
                q = pe.off(entry & 0x7FFFFFFF) + 2  # skip the hint
                func = pe.cstr(q).decode()
                if func == new:
                    print(f"import {dll}!{old} -> {new}: already renamed")
                    return
                if func == old:
                    overwrite_name(data, q, func, new)
                    print(f"import {dll}!{old} -> {new}: renamed")
                    return
            t += entry_size
    fail(f"no import of {dll}!{old}")


def wine_proxy_vtables(data):
    pe = PE(data)
    if not pe.pe32plus:
        fail("only 64-bit images are supported")

    # IAT slot RVA -> replacement value for the imports Wine lacks
    replacement = {}
    for p, _, _ in import_descriptors(pe):
        ilt_rva, _, _, _, iat_rva = struct.unpack_from("<IIIII", data, p)
        t, slot = pe.off(ilt_rva or iat_rva), iat_rva
        while (entry := struct.unpack_from("<Q", data, t)[0]) != 0:
            if not entry >> 63:
                func = pe.cstr(pe.off(entry & 0x7FFFFFFF) + 2).decode()
                if re.fullmatch(r"ObjectStublessClient\d+", func):
                    replacement[slot] = 0xFFFFFFFFFFFFFFFF
                elif re.fullmatch(r"NdrProxyForwardingFunction\d+", func):
                    replacement[slot] = 0
            t, slot = t + 8, slot + 8
    if not replacement:
        print("proxy vtables: nothing to do")
        return

    # Import thunks (`[rex.W] jmp qword ptr [rip+disp32]`) that jump through those slots
    thunks = {}
    for va, raw, size in pe.code_sections:
        i = data.find(b"\xff\x25", raw, raw + size)
        while i != -1:
            rva = va + i - raw
            target = rva + 6 + struct.unpack_from("<i", data, i + 2)[0]
            if target in replacement:
                thunks[pe.image_base + rva] = replacement[target]
                if data[i - 1] == 0x48:
                    thunks[pe.image_base + rva - 1] = replacement[target]
            i = data.find(b"\xff\x25", i + 1, raw + size)

    # Every absolute pointer to a thunk has a DIR64 base relocation: rewrite those slots
    reloc_rva, reloc_size = pe.directory(5)
    if reloc_rva == 0:
        fail("no base relocations")
    p, end = pe.off(reloc_rva), pe.off(reloc_rva) + reloc_size
    patched = 0
    while p < end:
        page, block = struct.unpack_from("<II", data, p)
        if block == 0:
            break
        for e in range(p + 8, p + block, 2):
            reloc = struct.unpack_from("<H", data, e)[0]
            if reloc >> 12 != 10:  # IMAGE_REL_BASED_DIR64
                continue
            slot = pe.off(page + (reloc & 0xFFF))
            value = struct.unpack_from("<Q", data, slot)[0]
            if value in thunks:
                struct.pack_into("<Q", data, slot, thunks[value])
                struct.pack_into("<H", data, e, 0)  # IMAGE_REL_BASED_ABSOLUTE: no-op
                patched += 1
        p += block
    print(f"proxy vtables: {patched} slots rewritten")


def find_unique(data, pattern, what):
    hits = list(re.finditer(pattern, data, re.S))
    if len(hits) != 1:
        fail(f"{what}: expected 1 match, found {len(hits)} (unsupported BlueStacks version?)")
    return hits[0]


def force_hyperv(data):
    pe = PE(data)
    # al = (vendor == "Microsoft Hv"); rdx = al ? "hyperv" : "vbox"
    #   mov al,1 / jmp +2 / xor al,al / lea rcx,"hyperv" / lea rdx,"vbox" / test al,al / cmovne rdx,rcx
    pattern = rb"\xb0\x01\xeb\x02(\x32\xc0|\xb0\x01)\x48\x8d\x0d....\x48\x8d\x15....\x84\xc0\x48\x0f\x45\xd1"
    hits = [m for m in re.finditer(pattern, data, re.S)
            if pe.cstr(pe.off(pe.rip_target(m.start() + 9))) == b"hyperv"
            and pe.cstr(pe.off(pe.rip_target(m.start() + 16))) == b"vbox"]
    if len(hits) != 1:
        fail(f"hypervisor selection: expected 1 match, found {len(hits)} (unsupported BlueStacks version?)")
    o = hits[0].start(1)
    already = data[o:o + 2] == b"\xb0\x01"
    data[o:o + 2] = b"\xb0\x01"  # xor al,al -> mov al,1
    print(f"hypervisor selection: {'already forced' if already else 'forced'} to hyperv")


def driverless_fallback(data):
    if b"Switching to driverless mode" not in data:
        fail("no driverless mode in this SUPLib (unsupported BlueStacks version?)")
    # test sil,0xc (SUPR3INIT_F_DRIVERLESS_MASK) / je skip / cmp ebx,-1
    m = find_unique(data, rb"\x40\xf6\xc6\x0c(\x0f\x84....|\x66\x0f\x1f\x44\x00\x00)\x83\xfb\xff", "driverless check")
    o = m.start(1)
    already = data[o] == 0x66
    data[o:o + 6] = b"\x66\x0f\x1f\x44\x00\x00"  # je -> 6-byte nop
    print(f"driverless fallback: {'already enabled' if already else 'enabled'}")


def nem_skip_cpuid_probe(data):
    if b"Not in a hypervisor partition" not in data:
        fail("no NEM CPUID probe found (unsupported BlueStacks version?)")
    # cmp r9d,0x40000005 / jae <load WinHvPlatform.dll>: the last check of the probe
    m = find_unique(data, rb"\x41\x81\xf9\x05\x00\x00\x40\x73(.)", "hypervisor leaf range check")
    target = m.end() + struct.unpack("b", m.group(1))[0]
    window = data[max(0, m.start() - 0x200):m.start()]
    base = max(0, m.start() - 0x200)
    # xor eax,eax / xor ecx,ecx / cpuid / dec eax: the first check of the probe
    start = window.rfind(b"\x33\xc0\x33\xc9\x0f\xa2\xff\xc8")
    if start == -1:
        for i in range(len(window) - 7):
            if window[i:i + 3] == b"\x33\xdb\xe9" and \
                    base + i + 7 + struct.unpack_from("<i", window, i + 3)[0] == target:
                print("NEM CPUID probe: already skipped")
                return
        fail("NEM CPUID probe start not found (unsupported BlueStacks version?)")
    o = base + start
    # xor ebx,ebx (the loader loop below indexes with rbx) / jmp target / nop
    data[o:o + 8] = b"\x33\xdb\xe9" + struct.pack("<i", target - (o + 7)) + b"\x90"
    print("NEM CPUID probe: skipped")


def writable_section(data, section):
    pe = PE(data)
    if section not in pe.section_headers:
        fail(f"no {section} section")
    s = pe.section_headers[section] + 36
    flags = struct.unpack_from("<I", data, s)[0]
    already = flags & 0x80000000  # IMAGE_SCN_MEM_WRITE
    struct.pack_into("<I", data, s, flags | 0x80000000)
    print(f"section {section}: {'already writable' if already else 'made writable'}")


def stub_method(data, full_type, method_name):
    namespace, _, type_name = full_type.rpartition(".")
    pe = PE(data)
    cli_rva, _ = pe.directory(14)
    if cli_rva == 0:
        fail("not a .NET assembly")
    md = pe.off(struct.unpack_from("<I", data, pe.off(cli_rva) + 8)[0])

    # Metadata root -> stream headers
    if struct.unpack_from("<I", data, md)[0] != 0x424A5342:
        fail("bad metadata signature")
    vlen = struct.unpack_from("<I", data, md + 12)[0]
    p = md + 16 + vlen
    nstreams = struct.unpack_from("<H", data, p + 2)[0]
    p += 4
    streams = {}
    for _ in range(nstreams):
        s_off = struct.unpack_from("<I", data, p)[0]
        name = pe.cstr(p + 8)
        streams[name.decode()] = md + s_off
        p = (p + 8 + len(name) + 4) & ~3
    if "#~" not in streams:
        fail("uncompressed (#-) metadata is not supported")
    strings, blobs = streams["#Strings"], streams["#Blob"]

    def get_str(idx):
        return pe.cstr(strings + idx).decode()

    # #~ table stream: row counts, then rows of each present table in order
    t = streams["#~"]
    heap_sizes = data[t + 6]
    valid = struct.unpack_from("<Q", data, t + 8)[0]
    p = t + 24
    rows = [0] * 64
    for i in range(64):
        if valid >> i & 1:
            rows[i] = struct.unpack_from("<I", data, p)[0]
            p += 4
    if heap_sizes & 0x40:
        p += 4
    if rows[0x05]:
        fail("MethodPtr indirection is not supported")

    str_sz = 4 if heap_sizes & 1 else 2
    guid_sz = 4 if heap_sizes & 2 else 2
    blob_sz = 4 if heap_sizes & 4 else 2

    def idx_sz(table):
        return 4 if rows[table] > 0xFFFF else 2

    def coded_sz(bits, tables):
        return 4 if max(rows[x] for x in tables) >= 1 << (16 - bits) else 2

    schemas = {
        0x00: [2, str_sz, guid_sz, guid_sz, guid_sz],                     # Module
        0x01: [coded_sz(2, [0x00, 0x1A, 0x23, 0x01]), str_sz, str_sz],    # TypeRef
        0x02: [4, str_sz, str_sz, coded_sz(2, [0x02, 0x01, 0x1B]),        # TypeDef
               idx_sz(0x04), idx_sz(0x06)],
        0x03: [idx_sz(0x04)],                                             # FieldPtr
        0x04: [2, str_sz, blob_sz],                                       # Field
        0x05: [idx_sz(0x06)],                                             # MethodPtr
        0x06: [4, 2, 2, str_sz, blob_sz, idx_sz(0x08)],                   # MethodDef
    }
    table_off = {}
    for tid in range(0x07):
        table_off[tid] = p
        p += rows[tid] * sum(schemas[tid])

    def read_row(tid, i):  # 0-based row index
        q = table_off[tid] + i * sum(schemas[tid])
        out = []
        for size in schemas[tid]:
            out.append(struct.unpack_from("<I" if size == 4 else "<H", data, q)[0])
            q += size
        return out

    type_idx = next((i for i in range(rows[0x02])
                     if get_str(read_row(0x02, i)[1]) == type_name
                     and get_str(read_row(0x02, i)[2]) == namespace), None)
    if type_idx is None:
        fail(f"type {full_type} not found")
    first = read_row(0x02, type_idx)[5] - 1
    last = read_row(0x02, type_idx + 1)[5] - 1 if type_idx + 1 < rows[0x02] else rows[0x06]

    def skip_compressed(b):
        return b + (1 if data[b] < 0x80 else 2 if data[b] < 0xC0 else 4)

    found = False
    for m in range(first, last):
        rva, _, _, name, sig, _ = read_row(0x06, m)
        if get_str(name) != method_name:
            continue
        found = True
        # Signature blob: [length][callconv][generic count?][param count][return type]
        b = skip_compressed(blobs + sig)
        callconv = data[b]
        b += 1
        if callconv & 0x10:
            b = skip_compressed(b)
        b = skip_compressed(b)
        if data[b] != 0x01:
            fail(f"{full_type}.{method_name} does not return void")

        body = pe.off(rva)
        if data[body] & 3 == 2:  # tiny header: size in the upper 6 bits
            code = body + 1
            already = data[body] == 0x06 and data[code] == 0x2A
            data[body] = 0x06
        else:  # fat header: drop exception clauses (MoreSects), shrink code to 1 byte
            flags = struct.unpack_from("<H", data, body)[0]
            code = body + (flags >> 12) * 4
            already = struct.unpack_from("<I", data, body + 4)[0] == 1 and data[code] == 0x2A
            struct.pack_into("<H", data, body, flags & ~0x8)
            struct.pack_into("<I", data, body + 4, 1)
        data[code] = 0x2A  # ret
        print(f"{full_type}.{method_name}: {'already stubbed' if already else 'stubbed'}")
    if not found:
        fail(f"method {full_type}.{method_name} not found")


def main():
    commands = {  # name -> (function, number of arguments after FILE)
        "stub-method": (stub_method, 2),
        "rename-import": (rename_import, 2),
        "rename-import-func": (rename_import_func, 3),
        "writable-section": (writable_section, 1),
        "wine-proxy-vtables": (wine_proxy_vtables, 0),
        "force-hyperv": (force_hyperv, 0),
        "driverless-fallback": (driverless_fallback, 0),
        "nem-skip-cpuid-probe": (nem_skip_cpuid_probe, 0),
    }
    if len(sys.argv) < 3 or sys.argv[1] not in commands \
            or len(sys.argv) != 3 + commands[sys.argv[1]][1]:
        fail("usage: bstpatch.py {stub-method FILE Namespace.Type Method"
             " | rename-import FILE OLD.dll NEW.dll | rename-import-func FILE DLL OLD NEW"
             " | writable-section FILE SECTION | wine-proxy-vtables FILE | force-hyperv FILE"
             " | driverless-fallback FILE | nem-skip-cpuid-probe FILE}")
    func = commands[sys.argv[1]][0]
    path = sys.argv[2]
    with open(path, "rb") as f:
        data = bytearray(f.read())
    func(data, *sys.argv[3:])
    with open(path, "wb") as f:
        f.write(data)


if __name__ == "__main__":
    main()
