#!/usr/bin/env python3
"""
sync_linfo.py -- keep DLConsole's LOCO_INFO layout in step with the C struct.

    python3 lococonfig/sync_linfo.py path/to/loco_config_vNN.cpp
    python3 lococonfig/sync_linfo.py path/to/loco_config_vNN.cpp --check

WHAT IT DOES
  1. Reads <packet name="LINFO"> from schema/kavach.xml: the layout the
     capture decoder (@linfo) and the Loco Configuration editor both use.
  2. Compiles the loco_config tool you give it (with a stub winsock2.h, so
     it builds on Linux/MSYS and SENDS NOTHING: socket()/sendto() are
     replaced and the datagram is written to a file instead) together with
     a checker that prints offsetof() and sizeof() of every C member the
     schema names.
  3. Compares, field by field: the schema's offset and size against the C
     struct's. A field in the wrong place, of the wrong width, misspelled,
     or missing from either side is reported BY NAME.
  4. Runs the tool twice, with the stack pre-filled with different junk,
     and compares the bytes. A byte that differs is uninitialised memory in
     the tool. In a char array after its NUL terminator that is harmless
     and zeroed with a warning; anywhere else it is an error (a member the
     tool never assigns).
  5. Checks the tool's datagram: header src/dest/message id and length,
     body equal to loco_info.bin, loco_info_crc correct.
  6. Unless --check: writes lococonfig/loco_defaults.json (the tool's values,
     the editor's defaults) and tests/fixtures/loco_info_default.bin (the
     golden body the lococonfig tests pack against).

Exit status 0 = schema and C struct agree (and files written); 1 = not.

Needs: python3 and g++ (any C++11 compiler: --compiler clang++).
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCHEMA = os.path.join(ROOT, 'schema', 'kavach.xml')
DEFAULTS = os.path.join(HERE, 'loco_defaults.json')
FIXTURE = os.path.join(ROOT, 'tests', 'fixtures', 'loco_info_default.bin')

# The editor's envelope (lococonfigcore.h). The tool's must match.
SOURCE_ID, DEST_ID, MESSAGE_ID, HEADER_BYTES = 28, 2, 120, 5
TOP_LEVEL_SECTION = 'loco_info'
CRC_FIELD = 'loco_info_crc'


# =============================================================================
#  The schema side
# =============================================================================

class Field:
    def __init__(self, key, name, section, c_path, kind, size, offset, group):
        self.key, self.name, self.section = key, name, section
        self.c_path, self.kind, self.size = c_path, kind, size
        self.offset, self.group = offset, group


def read_schema():
    root = ET.parse(SCHEMA).getroot()
    packets = [p for p in root.iter('packet') if p.get('name') == 'LINFO']
    if not packets:
        fail('schema/kavach.xml has no <packet name="LINFO">')
    fields, offset = [], 0
    section, c_member = '', ''
    for element in packets[0]:
        if element.tag == 'note':
            words = (element.get('text') or '').split()
            first = ''
            if words:
                first = words[0]
            if first == TOP_LEVEL_SECTION:
                section, c_member = '', ''
            else:
                section = first
                c_member = element.get('c-member') or ''
                if not c_member:
                    fail('the LINFO section note "%s" needs c-member="<the nested struct\'s C member>"' % first)
            continue
        if element.tag != 'field':
            fail('LINFO uses <%s>, which the editor does not pack' % element.tag)
        name = element.get('name')
        key = name
        if section:
            key = section + '.' + name
        c_name = element.get('c-name') or name
        c_path = c_name
        if c_member:
            c_path = c_member + '.' + c_name
        kind = element.get('type')
        if kind == 'float':
            size = 4
        elif kind == 'char':
            size = int(element.get('count', '0'))
        elif kind is None:
            bits = int(element.get('bits', '0'))
            if bits not in (8, 16, 32):
                fail('LINFO field %s: bits="%d" is not a C integer width (8, 16, 32)' % (key, bits))
            kind, size = 'u%d' % bits, bits // 8
        else:
            fail('LINFO field %s: type="%s" is not packed by the editor' % (key, kind))
        if any(f.key == key for f in fields):
            fail('LINFO has two fields keyed %s' % key)
        fields.append(Field(key, name, section, c_path, kind, size, offset, element.get('ui-group')))
        offset += size
    if not fields or fields[-1].name != CRC_FIELD or fields[-1].size != 4:
        fail('LINFO must end with the 32-bit %s' % CRC_FIELD)
    return fields, offset


# =============================================================================
#  The C side
# =============================================================================

STUB_WINSOCK = r'''
#pragma once
/* sync_linfo.py stub: lets the loco_config tool build off Windows and makes
   sure it sends nothing. socket() returns a dummy; sendto() writes the
   datagram to sync_linfo_sent.bin in the working directory instead. */
#include <cstdio>
#include <cstddef>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
typedef int SOCKET;
typedef struct { int unused; } WSADATA;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define MAKEWORD(a, b) ((unsigned short)(((a) & 0xff) | (((b) & 0xff) << 8)))
static inline int WSAStartup(unsigned short, WSADATA *) { return 0; }
static inline int WSACleanup() { return 0; }
static inline int WSAGetLastError() { return 0; }
static inline int closesocket(int) { return 0; }
static inline int sync_linfo_socket(int, int, int) { return 3; }
static inline long sync_linfo_sendto(int, const void *buffer, size_t length, int, const void *, unsigned)
{
    FILE *file = std::fopen("sync_linfo_sent.bin", "wb");
    if (file) { std::fwrite(buffer, 1, length, file); std::fclose(file); }
    return (long)length;
}
#define socket sync_linfo_socket
#define sendto sync_linfo_sendto
'''

CHECKER = r'''
#include <cstddef>
#include <cstdio>
#include <cstring>
#define main loco_tool_main
#include "%(tool)s"
#undef main

/* Fill a large stretch of stack with `pattern` so an uninitialised local in
   the tool picks it up, and two runs with different patterns differ. */
__attribute__((noinline)) static void dirty_stack(unsigned char pattern)
{
    volatile unsigned char junk[256 * 1024];
    for (size_t i = 0; i < sizeof junk; ++i) { junk[i] = pattern; }
}

int main(int argc, char **argv)
{
    std::printf("SIZE %%zu\n", sizeof(LOCO_INFO));
    std::printf("HEADER %%zu\n", sizeof(STRUCT_MESSAGE_HEADER));
%(fields)s
    if (argc < 2) { return 0; }
    dirty_stack((unsigned char)std::strtol(argv[1], nullptr, 16));
    char a0[] = "loco_config", a1[] = "127.0.0.1";
    char *args[] = { a0, a1, nullptr };
    return loco_tool_main(2, args);
}
'''


def build_checker(tool, fields, workdir, compiler, optimisation):
    include = os.path.join(workdir, 'include')
    os.makedirs(include, exist_ok=True)
    with open(os.path.join(include, 'winsock2.h'), 'w') as handle:
        handle.write(STUB_WINSOCK)
    with open(os.path.join(include, 'ws2tcpip.h'), 'w') as handle:
        handle.write('#pragma once\n#include "winsock2.h"\n')
    lines = []
    for index, field in enumerate(fields):
        lines.append('    std::printf("F %d %%zu %%zu\\n", offsetof(LOCO_INFO, %s), sizeof(((LOCO_INFO *)0)->%s));'
                     % (index, field.c_path, field.c_path))
    source = os.path.join(workdir, 'checker.cpp')
    with open(source, 'w') as handle:
        handle.write(CHECKER % {'tool': os.path.abspath(tool).replace('\\', '/'), 'fields': '\n'.join(lines)})
    binary = os.path.join(workdir, 'checker' + optimisation)
    command = [compiler, '-std=c++11', optimisation, '-w', '-I', include, source, '-o', binary]
    env = dict(os.environ, LC_ALL='C')
    result = subprocess.run(command, capture_output=True, text=True, env=env)
    if result.returncode != 0:
        explain_compile_error(result.stderr, fields)
    return binary


def explain_compile_error(stderr, fields):
    """Turn "has no member named 'x'" into the schema field that asked for it."""
    missing = sorted(set(re.findall(r"has no member named '(\w+)'", stderr)))
    if missing:
        lines = []
        for member in missing:
            asking = [f.key for f in fields if f.c_path.split('.')[-1] == member]
            lines.append('  schema field %s -> C member "%s" does not exist in LOCO_INFO'
                         % (', '.join(asking) or '?', member))
        fail('The schema names members the C struct does not have:\n' + '\n'.join(lines) +
             '\nFix the name, or add c-name="<C member>" to the <field> (or c-member= to its section note).')
    fail('The tool did not compile with the checker:\n' + stderr[-3000:])


def run_checker(binary, pattern, workdir):
    rundir = os.path.join(workdir, 'run_' + pattern)
    os.makedirs(rundir, exist_ok=True)
    result = subprocess.run([binary, pattern], capture_output=True, text=True, cwd=rundir)
    layout = {'fields': {}}
    for line in result.stdout.splitlines():
        parts = line.split()
        if parts[:1] == ['SIZE']:
            layout['size'] = int(parts[1])
        elif parts[:1] == ['HEADER']:
            layout['header'] = int(parts[1])
        elif parts[:1] == ['F'] and len(parts) == 4:
            layout['fields'][int(parts[1])] = (int(parts[2]), int(parts[3]))
    body_path = os.path.join(rundir, 'loco_info.bin')
    sent_path = os.path.join(rundir, 'sync_linfo_sent.bin')
    if not os.path.exists(body_path):
        fail('The tool ran but wrote no loco_info.bin (exit %d). Its output:\n%s'
             % (result.returncode, (result.stdout + result.stderr)[-2000:]))
    with open(body_path, 'rb') as handle:
        body = handle.read()
    sent = b''
    if os.path.exists(sent_path):
        with open(sent_path, 'rb') as handle:
            sent = handle.read()
    return layout, body, sent


# =============================================================================
#  Checks
# =============================================================================

def crc32(data):
    """crcFast() of the tool: reflected 0x04C11DB7, init 0, no final XOR."""
    remainder = 0
    for byte in data:
        remainder ^= byte
        for _ in range(8):
            if remainder & 1:
                remainder = (remainder >> 1) ^ 0xEDB88320
            else:
                remainder >>= 1
    return remainder


def check_layout(fields, body_size, layout):
    """Stop at the FIRST disagreement: every later offset shifts with it, so
    listing them would bury the one line that matters."""
    if layout.get('header') != HEADER_BYTES:
        fail('STRUCT_MESSAGE_HEADER is %s bytes in the tool; the editor sends %d'
             % (layout.get('header'), HEADER_BYTES))
    for index, field in enumerate(fields):
        offset, size = layout['fields'][index]
        offset -= HEADER_BYTES
        previous = 'the start of the struct'
        if index > 0:
            previous = fields[index - 1].key
        if offset != field.offset:
            gap = offset - field.offset
            if gap > 0:
                where = ('the C struct has %d more byte(s) between %s and %s than the schema: '
                         'add the new member(s) there' % (gap, previous, field.key))
            else:
                where = ('the schema has %d more byte(s) between %s and %s than the C struct: '
                         'a <field> there is extra, or wider than its C member' % (-gap, previous, field.key))
            fail('Schema and C struct disagree at %s (schema byte %d, C byte %d):\n  %s'
                 % (field.key, field.offset, offset, where))
        if size != field.size:
            fail('Schema and C struct disagree at %s: the schema says %s (%d byte(s)), the C member is %d '
                 'byte(s). Fix bits=/type=/count= on its <field>.' % (field.key, field.kind, field.size, size))
    c_body = layout.get('size', 0) - HEADER_BYTES
    if c_body != body_size:
        fail('the C struct body is %d bytes and the schema %d: it has members after %s that the schema '
             'does not describe' % (c_body, body_size, fields[-1].key))


def field_at(fields, offset):
    for field in fields:
        if field.offset <= offset < field.offset + field.size:
            return field
    return None


def reconcile_runs(fields, bodies):
    """Bytes that differ between runs are uninitialised memory in the tool.

    In a char array after its NUL terminator they are harmless (nothing reads
    past the NUL) and are zeroed with a warning, so the fixture and the
    defaults are reproducible. Anywhere else they are a member the tool never
    assigns, which is an error.
    """
    first = bodies[0]
    differing = set()
    for other in bodies[1:]:
        for offset in range(len(first)):
            if first[offset] != other[offset]:
                differing.add(offset)
    crc_field = fields[-1]
    fixed, warned, errors = bytearray(first), [], {}
    for offset in sorted(differing):
        field = field_at(fields, offset)
        if field is crc_field:
            continue   # follows from the other bytes; recomputed below
        if field.kind == 'char':
            terminator = first[field.offset:field.offset + field.size].find(b'\0')
            if 0 <= terminator < offset - field.offset:
                fixed[offset] = 0
                if field.key not in warned:
                    warned.append(field.key)
                continue
        errors.setdefault(field.key, offset)
    if errors:
        lines = ['%s (byte %d)' % (key, offset) for key, offset in errors.items()]
        fail('The tool leaves these members uninitialised (they change from run to run):\n  ' +
             '\n  '.join(lines) + '\nAssign them in the tool, or memset() the struct before filling it.')
    crc_offset = crc_field.offset
    fixed[crc_offset:crc_offset + 4] = struct.pack('<I', crc32(bytes(fixed[:crc_offset])))
    return bytes(fixed), warned


def check_datagram(body, sent, run_body):
    if not sent:
        fail('The tool built the body but never called sendto(); cannot check its message header')
    source, dest, message_id, length = struct.unpack_from('<BBBH', sent, 0)
    problems = []
    if (source, dest, message_id) != (SOURCE_ID, DEST_ID, MESSAGE_ID):
        problems.append('header is src %d -> dest %d msg %d; the editor sends %d -> %d msg %d'
                        % (source, dest, message_id, SOURCE_ID, DEST_ID, MESSAGE_ID))
    if length != len(body) + HEADER_BYTES or len(sent) != length:
        problems.append('message_length is %d and %d bytes were sent; the editor sends %d'
                        % (length, len(sent), len(body) + HEADER_BYTES))
    if sent[HEADER_BYTES:] != run_body:
        problems.append('the sent body differs from loco_info.bin')
    stored = struct.unpack_from('<I', run_body, len(run_body) - 4)[0]
    if stored != crc32(run_body[:-4]):
        problems.append('loco_info_crc 0x%08X is not the CRC of the body before it (0x%08X)'
                        % (stored, crc32(run_body[:-4])))
    if problems:
        fail('The tool\'s message does not match what the editor sends:\n  ' + '\n  '.join(problems))


# =============================================================================
#  Output
# =============================================================================

def float_text(raw):
    number = struct.unpack('<f', raw)[0]
    for precision in range(6, 10):
        text = '%.*g' % (precision, number)
        if struct.pack('<f', float(text)) == raw:
            return float(text)
    return number


def values_of(fields, body):
    values = {}
    for field in fields[:-1]:
        raw = body[field.offset:field.offset + field.size]
        if field.kind == 'float':
            values[field.key] = float_text(raw)
        elif field.kind == 'char':
            values[field.key] = raw.split(b'\0')[0].decode('latin-1')
        else:
            values[field.key] = int.from_bytes(raw, 'little')
    return values


def fail(message):
    print('sync_linfo: ' + message, file=sys.stderr)
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('tool', help='the loco_config_vNN.cpp that defines LOCO_INFO and its values')
    parser.add_argument('--check', action='store_true', help='check only; write nothing')
    parser.add_argument('--compiler', default='g++')
    options = parser.parse_args()

    if not os.path.exists(options.tool):
        fail('%s: no such file' % options.tool)
    if shutil.which(options.compiler) is None:
        fail('%s not found (use --compiler)' % options.compiler)

    fields, body_size = read_schema()
    workdir = tempfile.mkdtemp(prefix='sync_linfo_')
    try:
        debug = build_checker(options.tool, fields, workdir, options.compiler, '-O0')
        optimised = build_checker(options.tool, fields, workdir, options.compiler, '-O2')
        layout, body_a, sent_a = run_checker(debug, 'A5', workdir)
        check_layout(fields, body_size, layout)
        _, body_b, _ = run_checker(debug, '5A', workdir)
        _, body_c, _ = run_checker(optimised, '00', workdir)
        check_datagram(body_a, sent_a, body_a)
        body, warnings = reconcile_runs(fields, [body_a, body_b, body_c])
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    values = values_of(fields, body)
    previous = {}
    if os.path.exists(DEFAULTS):
        with open(DEFAULTS) as handle:
            previous = json.load(handle).get('values', {})
    added = [k for k in values if k not in previous]
    removed = [k for k in previous if k not in values]
    changed = [k for k in values if k in previous and previous[k] != values[k]]
    ungrouped = [f.key for f in fields[:-1] if not f.group]

    crc = struct.unpack_from('<I', body, body_size - 4)[0]
    print('LOCO_INFO: %d members, %d-byte body (+%d header), loco_info_crc 0x%08X'
          % (len(fields), body_size, HEADER_BYTES, crc))
    print('schema and C struct agree: every offset and size matches')
    for key in warnings:
        print('WARNING: %s: the bytes after its NUL are uninitialised in the tool (they changed between '
              'runs) -- zeroed here. Fix in the tool: memset() the struct before strcpy().' % key)
    for label, keys in (('new', added), ('removed', removed), ('value changed', changed)):
        if keys:
            print('%s: %s' % (label, ', '.join(keys)))
    if ungrouped:
        print('no ui-group (shown under "Other"): %s' % ', '.join(ungrouped))

    if options.check:
        if previous and (added or removed or changed):
            print('--check: lococonfig/loco_defaults.json is out of date; run without --check')
            sys.exit(1)
        return

    with open(options.tool, 'rb') as handle:
        tool_sha = hashlib.sha256(handle.read()).hexdigest()
    document = {
        '_comment': 'GENERATED by lococonfig/sync_linfo.py from the loco_config tool named below: '
                    'its values are the Loco Configuration editor\'s defaults. Do not edit by hand; '
                    'change the tool and re-run the script. Keys are kavach.xml LINFO field names, '
                    'prefixed by their section.',
        'source': os.path.basename(options.tool),
        'source_sha256': tool_sha,
        'body_bytes': body_size,
        'loco_info_crc': '0x%08X' % crc,
        'values': values,
    }
    with open(DEFAULTS, 'w') as handle:
        json.dump(document, handle, indent=1)
        handle.write('\n')
    os.makedirs(os.path.dirname(FIXTURE), exist_ok=True)
    with open(FIXTURE, 'wb') as handle:
        handle.write(body)
    print('wrote lococonfig/loco_defaults.json and tests/fixtures/loco_info_default.bin')


if __name__ == '__main__':
    main()
