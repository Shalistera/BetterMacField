"""Scene-record editing for the native-assets vegetation export (iOS 1.6.3)."""
import hashlib
from pathlib import Path
import struct
import lz4.block


def selected_scene(raw, selected):
    """Serialize only selected type-9 records, ownership and component rows.

    Copy inline values verbatim; rebuild every referenced object. No unused
    source buffer is retained. Unknown reference layouts stop the export.
    """
    flat = Flat(raw)
    root = flat.fields(flat.u(0))
    records = entities(raw)
    selected = set(selected)
    assert selected <= records.keys()
    result = bytearray(4)

    def align(size, residue=0):
        result.extend(bytes((residue - len(result)) % size))

    def link(at, target):
        assert target > at
        struct.pack_into('<I', result, at, target - at)

    def vector(data, count, alignment=4):
        align(alignment, (-4) % alignment)
        at = len(result)
        result.extend(struct.pack('<I', count) + data)
        return at

    def copy_vector(field, width=1, string=False):
        at, count = flat.vector(field)
        return vector(raw[at:at + count * width] + (b'\0' if string else b''), count, max(4, width))

    def pointers(items, copy):
        items = list(items)
        at = vector(bytes(len(items) * 4), len(items))
        for index, item in enumerate(items):
            link(at + 4 + index * 4, copy(item))
        return at

    def table(original, allowed, refs, values=None):
        fields = flat.fields(original)
        assert fields.keys() <= set(allowed), (tuple(fields), tuple(allowed))
        vtable = original - flat.i(original)
        align(2)
        v = len(result)
        result.extend(raw[vtable:vtable + flat.h(vtable)])
        align(8, original % 8)
        at = len(result)
        result.extend(raw[original:original + flat.h(vtable + 2)])
        struct.pack_into('<i', result, at, at - v)
        for key, value in (values or {}).items():
            destination = at + fields[key] - original
            result[destination:destination + len(value)] = value
        for key, copy in refs.items():
            if key in fields:
                link(at + fields[key] - original, copy(fields[key]))
        return at

    def resource(item):
        flag, at = item
        if flag == 17:
            return table(at, {0}, {0: lambda p: table(flat.ref(p), {0}, {0: copy_vector})})
        width = {2: 1, 13: 4, 14: 24, 15: 16}[flag]
        align(8, at % 8)
        destination = len(result)
        result.extend(raw[at:at + width])
        return destination

    def payload(field):
        fields = flat.fields(flat.ref(field))
        at, count = flat.vector(fields[4])
        flags = raw[at:at + count]
        return table(flat.ref(field), {3, 4, 5}, {
            3: lambda p: copy_vector(p, 4), 4: copy_vector,
            5: lambda p: pointers(zip(flags, flat.tables(p)), resource)})

    def entity(eid):
        kind, original = records[eid]
        assert flat.u(flat.fields(original)[2]) == 9
        def empty(field):
            assert flat.vector(field)[1] == 0
            return vector(b'', 0)
        return table(original, range(6), {
            0: lambda p: copy_vector(p, string=True), 3: payload, 5: empty})

    def uints(values):
        values = list(values)
        return vector(struct.pack('<' + 'I' * len(values), *values), len(values))

    buckets = [t for t in flat.tables(root[2]) if selected.intersection(flat.uints(flat.fields(t)[5]))]
    def bucket(original):
        # Type, layer and scenario are inline integers; bounds are inline too.
        return table(original, range(6), {
            5: lambda p: uints(eid for eid in flat.uints(p) if eid in selected)})

    groups = entity_groups(raw)
    indices = [i for i, group in enumerate(groups) if selected.intersection(group['members'])]
    rows = component_rows(raw)
    def members(index):
        return [eid for eid in groups[index]['members'] if eid in selected]
    def member_table(index):
        return table(flat.tables(root[6])[index], {0}, {0: lambda p: uints(members(index))})
    def group_table(index):
        ids = members(index)
        descriptors = groups[index]['descriptors']
        blob = b''.join(rows[eid][d & 65535] for d in descriptors for eid in ids)
        bounds = [struct.unpack('<6f', b''.join(rows[eid][k] for k in range(6))) for eid in ids]
        low = [min(b[k] - b[k + 3] for b in bounds) for k in range(3)]
        high = [max(b[k] + b[k + 3] for b in bounds) for k in range(3)]
        combined = [(a + b) / 2 for a, b in zip(low, high)] + [(b - a) / 2 for a, b in zip(low, high)]
        return table(flat.tables(root[7])[index], range(5), {
            3: lambda p: copy_vector(p, 8),
            4: lambda p: table(flat.ref(p), {0}, {0: lambda p: vector(blob, len(blob))})},
            {1: struct.pack('<I', len(ids)), 2: struct.pack('<6f', *combined)})

    ids = sorted(selected)
    at = table(flat.u(0), range(8), {
        2: lambda p: pointers(buckets, bucket), 3: lambda p: uints(ids),
        4: lambda p: vector(bytes(records[eid][0] for eid in ids), len(ids)),
        5: lambda p: pointers(ids, entity),
        6: lambda p: pointers(indices, member_table), 7: lambda p: pointers(indices, group_table)})
    struct.pack_into('<I', result, 0, at)
    compact = bytes(result)
    assert entities(compact).keys() == selected
    assert render_references(compact) == {eid: value for eid, value in render_references(raw).items() if eid in selected}
    assert component_rows(compact) == {eid: value for eid, value in rows.items() if eid in selected}
    return compact


class Flat:
    def __init__(self,data):self.data=data
    def u(self,p):return struct.unpack_from('<I',self.data,p)[0]
    def i(self,p):return struct.unpack_from('<i',self.data,p)[0]
    def h(self,p):return struct.unpack_from('<H',self.data,p)[0]
    def ref(self,p):return p+self.u(p)
    def fields(self,t):
        v=t-self.i(t);return {n:t+self.h(v+4+n*2) for n in range((self.h(v)-4)//2) if self.h(v+4+n*2)}
    def vector(self,p):
        v=self.ref(p);return v+4,self.u(v)
    def string(self,p):
        v,n=self.vector(p);return self.data[v:v+n].decode('utf8')
    def tables(self,p):
        v,n=self.vector(p);return [self.ref(v+i*4) for i in range(n)]
    def uints(self,p):
        v,n=self.vector(p);return list(struct.unpack_from('<'+str(n)+'I',self.data,v))

def compress(raw):
    """Convert standard LZ4 tokens and distances to the game's streaming format."""
    block = lz4.block.compress(raw, store_size=False)
    output, pos = bytearray(struct.pack('<I', len(raw))), 0
    while pos < len(block):
        token = block[pos]
        pos += 1
        literal, match = token >> 4, token & 15
        output.append((literal & 3) | ((literal & 12) << 2) |
                      ((match & 3) << 2) | ((match & 12) << 4))
        literal_count = literal
        if literal == 15:
            while True:
                value = block[pos]
                pos += 1
                output.append(value)
                literal_count += value
                if value != 255:
                    break
        output.extend(block[pos:pos + literal_count])
        pos += literal_count
        if pos == len(block):
            break
        output.extend(block[pos:pos + 2][::-1])
        pos += 2
        if match == 15:
            while True:
                value = block[pos]
                pos += 1
                output.append(value)
                if value != 255:
                    break
    assert pos == len(block) and decode(output) == raw
    return bytes(output)

def entity_groups(raw):
    f = Flat(raw)
    root = f.fields(f.u(0))
    result = []
    for table, entity_set in zip(f.tables(root[7]), f.tables(root[6])):
        fields = f.fields(table)
        member_field = f.fields(entity_set)[0]
        members = f.uints(member_field)
        start, count = f.vector(fields[3])
        descriptors = struct.unpack_from('<' + str(count) + 'Q', raw, start)
        assert int.from_bytes(raw[fields[0]:fields[0] + 16], 'little') == sum(1 << (x & 65535) for x in descriptors)
        result.append({'fields': fields, 'memberField': member_field, 'members': members,
                       'descriptors': descriptors, 'blobField': f.fields(f.ref(fields[4]))[0]})
    return result

def entities(raw):
    f = Flat(raw)
    root = f.fields(f.u(0))
    ids = f.uints(root[3])
    at, count = f.vector(root[4])
    tables = f.tables(root[5])
    assert len(ids) == count == len(tables) and len(set(ids)) == count
    return dict(zip(ids, zip(raw[at:at + count], tables)))

def chunk_groups(raw):
    f = Flat(raw)
    root = f.fields(f.u(0))
    result = {}
    for table in f.tables(root[2]):
        fields = f.fields(table)
        key = (f.u(fields[0]) if 0 in fields else 0,
               struct.unpack_from('<ii', raw, fields[3]))
        assert key not in result
        result[key] = (fields, f.uints(fields[5]))
    return result

def transplant(mobile, pc, grass_ids, replacements=None):
    mf, pf = Flat(mobile), Flat(pc)
    mr = mf.fields(mf.u(0))
    me, pe = entities(mobile), entities(pc)
    added = grass_ids - me.keys()
    ids = list(me) + sorted(added)
    # A new root precedes both unchanged buffers so all uoffsets stay forward.
    changed = bytearray(struct.pack('<I', 24))
    changed.extend(struct.pack('<10H', 20, 40, 4, 8, 16, 20, 24, 28, 32, 36))
    changed.extend(struct.pack('<i', 20) + bytes(36))
    changed[28:32] = mobile[mr[0]:mr[0] + 4]
    changed[32:40] = mobile[mr[1]:mr[1] + 8]
    def vector(data, count):
        changed.extend(bytes((-len(changed)) % 4))
        at = len(changed)
        changed.extend(struct.pack('<I', count) + data)
        return at
    def link(at, target):
        assert target > at
        struct.pack_into('<I', changed, at, target - at)
    link(44, vector(struct.pack('<' + 'I' * len(ids), *ids), len(ids)))
    link(48, vector(bytes((me[eid] if eid in me else pe[eid])[0] for eid in ids), len(ids)))
    pointers = vector(bytes(len(ids) * 4), len(ids)) + 4
    link(52, pointers - 4)
    changed.extend(bytes((-len(changed)) % 8))
    mobile_at = len(changed)
    changed.extend(mobile)
    changed.extend(bytes((-len(changed)) % 8))
    pc_at = len(changed)
    changed.extend(pc)
    for field, target in ((2, 40), (6, 56), (7, 60)):
        link(target, mobile_at + mf.ref(mr[field]))
    for i, eid in enumerate(ids):
        owner, offset = (me, mobile_at) if eid in me else (pe, pc_at)
        link(pointers + i * 4, offset + owner[eid][1])
        if eid in grass_ids and eid in me:
            destination = mf.fields(me[eid][1])[3] + mobile_at
            payload = pf.ref(pf.fields(pe[eid][1])[3]) + pc_at
            link(destination, payload)
    if added:
        mg, pg = chunk_groups(mobile), chunk_groups(pc)
        for key, (_, pc_ids) in pg.items():
            incoming = added.intersection(pc_ids)
            if not incoming:
                continue
            fields, existing = mg[key]
            updated = existing + sorted(incoming)
            link(mobile_at + fields[5], vector(struct.pack('<' + 'I' * len(updated), *updated), len(updated)))
    # Only unavailable LOD0 asset hashes are redirected, keeping batch identity.
    for pointer, key in (replacements or {}).items():
        struct.pack_into('<Q', changed, pc_at + pointer, key)
    result = bytes(changed)
    actual = entities(result)
    assert set(actual) == set(me) | grass_ids
    for eid in set(me) - grass_ids:
        assert actual[eid][1] == mobile_at + me[eid][1]
    if added:
        ag = chunk_groups(result)
        for key, (_, pc_ids) in chunk_groups(pc).items():
            if added.intersection(pc_ids):
                assert added.intersection(pc_ids) <= set(ag[key][1])
    return result

def render_references(raw):
    flat = Flat(raw)
    result = {}
    for eid, (kind, table) in entities(raw).items():
        fields = flat.fields(table)
        if flat.u(fields.get(2, 0)) != 9:
            continue
        payload = flat.fields(flat.ref(fields[3]))
        at, count = flat.vector(payload[4])
        flags = raw[at:at + count]
        values = []
        for flag, p in zip(flags, flat.tables(payload[5])):
            if flag == 17:
                inner = flat.fields(flat.ref(flat.fields(p)[0]))
                start, size = flat.vector(inner[0])
                values.append(raw[start:start + size])
            else:
                # Other original entity payloads stay in their untouched source
                # buffer. Exported records use the strict layout above.
                values.append(raw[p:p + {2: 1, 13: 4, 14: 24, 15: 16}.get(flag, 16)])
        result[eid] = (kind, flat.string(fields[0]), flat.uints(payload[3]), flags, values)
    return result

def component_rows(raw):
    flat, result = Flat(raw), {}
    for group in entity_groups(raw):
        members = group['members']
        start, size = flat.vector(group['blobField'])
        offset = 0
        for descriptor in group['descriptors']:
            kind, width = descriptor & 65535, (descriptor >> 16) & 65535
            for index, eid in enumerate(members):
                at = start + offset + index * width
                result.setdefault(eid, {})[kind] = raw[at:at + width]
            offset += width * len(members)
        assert offset == size
    return result

def with_missing_ownership(mobile, pc, additions):
    """Copy absent authored chunk buckets before registering the selected entities."""
    native, desktop = chunk_groups(mobile), chunk_groups(pc)
    missing = {key for key, (_, ids) in desktop.items() if additions.intersection(ids) and key not in native}
    if not missing:
        return mobile
    mf, pf = Flat(mobile), Flat(pc)
    mr, pr = mf.fields(mf.u(0)), pf.fields(pf.u(0))
    native_tables = mf.tables(mr[2])
    desktop_tables = dict(zip(desktop, pf.tables(pr[2])))
    changed = bytearray(struct.pack('<I', 24))
    changed.extend(struct.pack('<10H', 20, 40, 4, 8, 16, 20, 24, 28, 32, 36))
    changed.extend(struct.pack('<i', 20) + bytes(36))
    changed[28:32], changed[32:40] = mobile[mr[0]:mr[0] + 4], mobile[mr[1]:mr[1] + 8]

    def vector(data, count):
        changed.extend(bytes((-len(changed)) % 4))
        at = len(changed)
        changed.extend(struct.pack('<I', count) + data)
        return at

    def link(field, target):
        assert target > field
        struct.pack_into('<I', changed, field, target - field)

    count = len(native_tables) + len(missing)
    pointers = vector(bytes(count * 4), count) + 4
    link(40, pointers - 4)
    changed.extend(bytes((-len(changed)) % 8))
    mobile_at = len(changed)
    changed.extend(mobile)
    changed.extend(bytes((-len(changed)) % 8))
    pc_at = len(changed)
    changed.extend(pc)
    for field, target in ((3, 44), (4, 48), (5, 52), (6, 56), (7, 60)):
        link(target, mobile_at + mf.ref(mr[field]))
    tables = [mobile_at + table for table in native_tables]
    for key in sorted(missing):
        tables.append(pc_at + desktop_tables[key])
        # Transplant will fill only this candidate's IDs into the copied bucket.
        link(pc_at + desktop[key][0][5], vector(b'', 0))
    for index, table in enumerate(tables):
        link(pointers + index * 4, table)
    actual = chunk_groups(changed)
    assert actual.keys() == native.keys() | missing
    assert all(actual[key][1] == ids for key, (_, ids) in native.items())
    assert all(actual[key][1] == [] for key in missing)
    assert render_references(changed) == render_references(mobile)
    assert component_rows(changed) == component_rows(mobile)
    return bytes(changed)

def with_missing_archetypes(mobile, pc, additions, native_descriptors=None):
    """Expose empty original PC archetypes for insert_renderers to populate."""
    native, desktop = entity_groups(mobile), entity_groups(pc)
    known = {group['descriptors'] for group in native}
    missing = {group['descriptors']: index for index, group in enumerate(desktop)
               if additions.intersection(group['members']) and group['descriptors'] not in known}
    if not missing:
        return mobile
    pc_rows = component_rows(pc)
    for index in missing.values():
        incoming = additions.intersection(desktop[index]['members'])
        for descriptor in desktop[index]['descriptors']:
            if descriptor in native_descriptors:
                continue
            assert descriptor == (260 << 16) | 49
            assert all(not any(pc_rows[eid][49]) for eid in incoming)
    mf, pf = Flat(mobile), Flat(pc)
    mr, pr = mf.fields(mf.u(0)), pf.fields(pf.u(0))
    changed = bytearray(struct.pack('<I', 24))
    changed.extend(struct.pack('<10H', 20, 40, 4, 8, 16, 20, 24, 28, 32, 36))
    changed.extend(struct.pack('<i', 20) + bytes(36))
    changed[28:32], changed[32:40] = mobile[mr[0]:mr[0] + 4], mobile[mr[1]:mr[1] + 8]

    def vector(data, count):
        changed.extend(bytes((-len(changed)) % 4))
        at = len(changed)
        changed.extend(struct.pack('<I', count) + data)
        return at

    def link(field, target):
        assert target > field
        struct.pack_into('<I', changed, field, target - field)

    count, pointers = len(native) + len(missing), {}
    for field, at in ((6, 56), (7, 60)):
        pointers[field] = vector(bytes(count * 4), count) + 4
        link(at, pointers[field] - 4)
    changed.extend(bytes((-len(changed)) % 8))
    mobile_at = len(changed)
    changed.extend(mobile)
    changed.extend(bytes((-len(changed)) % 8))
    pc_at = len(changed)
    changed.extend(pc)
    for field, at in ((2, 40), (3, 44), (4, 48), (5, 52)):
        link(at, mobile_at + mf.ref(mr[field]))
    for field in (6, 7):
        tables = [mobile_at + table for table in mf.tables(mr[field])]
        tables += [pc_at + pf.tables(pr[field])[index] for index in missing.values()]
        for index, table in enumerate(tables):
            link(pointers[field] + index * 4, table)
    for index in missing.values():
        group = desktop[index]
        link(pc_at + group['memberField'], vector(b'', 0))
        link(pc_at + group['blobField'], vector(b'', 0))
        struct.pack_into('<I', changed, pc_at + group['fields'][1], 0)
    assert render_references(changed) == render_references(mobile)
    assert component_rows(changed) == component_rows(mobile)
    assert len(entity_groups(changed)) == count
    return bytes(changed)

def insert_renderers(mobile, pc, selected):
    """Append PC component rows to matching existing native archetypes."""
    assert selected.isdisjoint(entities(mobile))
    changed = bytearray(transplant(mobile, pc, selected))
    snapshot = bytes(changed)
    groups = entity_groups(snapshot)
    templates = {g['descriptors']: i for i, g in enumerate(groups)}
    pc_groups = {eid: g for g in entity_groups(pc) for eid in g['members']}
    original_rows, desktop_rows = component_rows(snapshot), component_rows(pc)
    incoming = {}
    for eid in selected:
        index = templates[pc_groups[eid]['descriptors']]
        incoming.setdefault(index, []).append(eid)

    def vector(data, count):
        changed.extend(bytes((-len(changed)) % 4))
        at = len(changed)
        changed.extend(struct.pack('<I', count) + data)
        return at

    def link(field, data, count):
        at = vector(data, count)
        struct.pack_into('<I', changed, field, at - field)

    for index, additions in incoming.items():
        group = groups[index]
        members = group['members'] + sorted(additions)
        original = {eid: original_rows[eid] for eid in group['members']}
        restored = {eid: desktop_rows[eid] for eid in additions}
        rows = original | restored
        blob = bytearray()
        for descriptor in group['descriptors']:
            kind, size = descriptor & 65535, (descriptor >> 16) & 65535
            for eid in members:
                value = rows[eid][kind]
                assert len(value) == size
                blob.extend(value)
        link(group['memberField'], struct.pack('<' + 'I' * len(members), *members), len(members))
        link(group['blobField'], blob, len(blob))
        struct.pack_into('<I', changed, group['fields'][1], len(members))
        bounds = [struct.unpack('<6f', b''.join(rows[eid][k] for k in range(6))) for eid in members]
        low = [min(b[k] - b[k + 3] for b in bounds) for k in range(3)]
        high = [max(b[k] + b[k + 3] for b in bounds) for k in range(3)]
        combined = [(a + b) / 2 for a, b in zip(low, high)] + [(b - a) / 2 for a, b in zip(low, high)]
        struct.pack_into('<6f', changed, group['fields'][2], *combined)
    actual_rows = component_rows(changed)
    assert all(actual_rows[eid] == row for eid, row in original_rows.items())
    assert all(actual_rows[eid] == desktop_rows[eid] for eid in selected)

    return bytes(changed)

def decode(packed):
    """Translate the game's LZ4 token layout, then use the native decoder."""
    size, = struct.unpack_from('<I', packed)
    data = bytearray(packed[4:])
    p = 0
    while p < len(data):
        token = data[p]
        literal = (token & 3) | ((token >> 2) & 12)
        match = ((token >> 2) & 3) | ((token >> 4) & 12)
        data[p] = literal * 16 + match
        p += 1
        if literal == 15:
            while True:
                value = data[p]
                p += 1
                literal += value
                if value != 255:
                    break
        p += literal
        if p == len(data):
            break
        data[p:p + 2] = data[p:p + 2][::-1]
        p += 2
        if match == 15:
            while True:
                value = data[p]
                p += 1
                if value != 255:
                    break
    assert p == len(data)
    return lz4.block.decompress(bytes(data), uncompressed_size=size)

def read(row):
    assert not row['encrypted']
    source = Path(row['root']) / row['catalog'] / (row['chunk'] + '.chk')
    assert source.stat().st_size == row['chunk_bytes']
    with source.open('rb') as stream:
        stream.seek(row['offset'])
        packed = stream.read(row['size'])
    assert len(packed) == row['size'] and hashlib.md5(packed).hexdigest() == row['md5']
    return decode(packed)
