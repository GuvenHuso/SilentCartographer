"""The disc image readers of the desktop ports (port/linux/src/xiso.c) and the Android app
(XisoExtractor.java) on images made here: a valid one at each partition offset, and ones that are not images,
are cut short, lack ui.map, hold names that must not be created, or whose directory tree points back at
itself. The Android reader's cases need a JDK."""
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
SECTOR = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'

DRIVER = r'''
#include <stdio.h>
#include "xiso.h"

static void progress(void *context, const char *file, unsigned long long done, unsigned long long total)
{
	unsigned long long *last = context;

	if (done < *last || done > total)
		printf("progress went wrong: %s %llu of %llu\n", file, done, total);
	*last = done;
}

int main(int argc, char **argv)
{
	char error[512];
	unsigned long long last = 0;

	if (!xiso_extract_maps(argv[1], argv[2], progress, &last, error, sizeof(error)))
	{
		printf("error: %s\n", error);
		return 1;
	}
	printf("copied %llu\n", last);
	return 0;
}
'''


JAVA_DRIVER = r'''
package com.halo.decomp;

import java.io.File;
import java.io.RandomAccessFile;

public final class XisoMain {
    public static void main(String[] arguments) throws Exception {
        long[] last = { 0 };

        try (RandomAccessFile image = new RandomAccessFile(arguments[0], "r")) {
            new File(arguments[1]).mkdirs();
            XisoExtractor.extractMaps(image.getChannel(), new File(arguments[1]), (file, done, total) -> {
                if (done < last[0] || done > total)
                    System.out.println("progress went wrong: " + file + " " + done + " of " + total);
                last[0] = done;
            });
        } catch (XisoExtractor.ExtractException exception) {
            System.out.println("error: " + exception.getMessage());
            System.exit(1);
        }
        System.out.println("copied " + last[0]);
    }
}
'''


@pytest.fixture(scope='module', params=['c', 'java'])
def extractor(request, tmp_path_factory):
    if request.param == 'java':
        return java_extractor(tmp_path_factory)
    return c_extractor(tmp_path_factory)


def java_extractor(tmp_path_factory):
    if not shutil.which('javac'):
        pytest.skip('no JDK')
    directory = tmp_path_factory.mktemp('xiso_java')
    (directory / 'XisoMain.java').write_text(JAVA_DRIVER)
    source = ROOT / 'port/android/app/src/main/java/com/halo/decomp/XisoExtractor.java'
    result = subprocess.run(['javac', '-d', str(directory / 'classes'), str(source), str(directory / 'XisoMain.java')],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return ['java', '-cp', str(directory / 'classes'), 'com.halo.decomp.XisoMain']


def c_extractor(tmp_path_factory):
    directory = tmp_path_factory.mktemp('xiso')
    (directory / 'driver.c').write_text(DRIVER)
    program = directory / 'xiso'
    command = [os.environ.get('CC', 'clang'), '-m32', '-std=gnu11', '-D_GNU_SOURCE', '-D_FILE_OFFSET_BITS=64',
               '-DHALO_LINUX_PLATFORM_LAYER', '-Wall', '-Werror', '-O1', '-g', '-fsanitize=address,undefined',
               '-I', str(ROOT / 'port/linux/src'), str(ROOT / 'port/linux/src/xiso.c'),
               str(ROOT / 'port/linux/src/posix_files.c'), str(directory / 'driver.c'), '-lpthread', '-o', str(program)]
    result = subprocess.run(command, capture_output=True, text=True)
    # (without the sanitizers where their 32-bit libraries are not installed)
    if result.returncode != 0:
        command.remove('-fsanitize=address,undefined')
        result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return [str(program)]


def entry(name, sector, size, directory=False, left=0, right=0):
    encoded = name.encode('latin-1')
    record = struct.pack('<HHIIBB', left, right, sector, size, 0x10 if directory else 0x20, len(encoded)) + encoded
    return record + b'\0' * (-len(record) % 4)


def table(entries):
    """a directory table as a balanced tree, the middle entry first; entries do not cross a sector"""
    ordered = sorted(entries, key=lambda item: item[0].lower())
    nodes = []

    def place(low, high):
        if low > high:
            return None
        middle = (low + high) // 2
        index = len(nodes)
        nodes.append([ordered[middle], None, None])
        nodes[index][1] = place(low, middle - 1)
        nodes[index][2] = place(middle + 1, high)
        return index

    place(0, len(ordered) - 1)
    offsets = []
    position = 0
    for node in nodes:
        size = len(entry(node[0][0], 0, 0))
        if position // SECTOR != (position + size - 1) // SECTOR:
            position = (position // SECTOR + 1) * SECTOR
        offsets.append(position)
        position += size
    data = bytearray(b'\xff' * (-(-position // SECTOR) * SECTOR))
    for index, node in enumerate(nodes):
        name, sector, size, directory = node[0]
        left = offsets[node[1]] // 4 if node[1] is not None else 0
        right = offsets[node[2]] // 4 if node[2] is not None else 0
        record = entry(name, sector, size, directory, left, right)
        data[offsets[index]:offsets[index] + len(record)] = record
    return bytes(data)


def make_image(path, maps, partition=0, extra_root=(), cut=0, maps_table=None):
    """an image of a maps folder ({name: bytes}) and a default.xbe, its game partition at partition"""
    chunks = {}
    next_sector = [0x30]

    def allocate(data):
        sector = next_sector[0]
        next_sector[0] += -(-len(data) // SECTOR) + 1
        chunks[sector] = data
        return sector

    files = [(name, allocate(data), len(data), False) for name, data in maps.items()]
    maps_bytes = maps_table if maps_table is not None else table(files)
    maps_sector = allocate(maps_bytes)
    root = table([('maps', maps_sector, len(maps_bytes), True), ('default.xbe', allocate(b'x' * 100), 100, False),
                  *extra_root])
    root_sector = allocate(root)
    descriptor = bytearray(SECTOR)
    descriptor[0:20] = MAGIC
    descriptor[0x7EC:0x7EC + 20] = MAGIC
    descriptor[20:28] = struct.pack('<II', root_sector, len(root))
    size = partition + next_sector[0] * SECTOR - cut
    with open(path, 'wb') as image:
        image.truncate(size)
        for sector, data in [(32, bytes(descriptor)), *chunks.items()]:
            image.seek(partition + sector * SECTOR)
            image.write(data[:max(0, size - (partition + sector * SECTOR))])
    return path


MAPS = {'ui.map': b'head' + b'u' * 5000, 'a10.map': os.urandom(3 * SECTOR + 17), 'loading.tga': b't' * 50,
        'bloodgulch.map': os.urandom(1 << 20 | 5)}


def run(extractor, image, destination):
    return subprocess.run([*extractor, str(image), str(destination)], capture_output=True, text=True)


@pytest.mark.parametrize('partition', [0, 0x02080000, 0x0FD90000, 0x18300000])
def test_maps_come_out_at_each_partition_offset(extractor, tmp_path, partition):
    image = make_image(tmp_path / 'halo.iso', MAPS, partition)
    result = run(extractor, image, tmp_path / 'data')
    assert result.returncode == 0, result.stdout + result.stderr
    assert sorted(os.listdir(tmp_path / 'data/maps')) == sorted(MAPS)
    for name, data in MAPS.items():
        assert (tmp_path / 'data/maps' / name).read_bytes() == data
    assert not (tmp_path / 'data/maps.partial').exists()
    assert f'copied {sum(map(len, MAPS.values()))}' in result.stdout


def test_an_existing_maps_folder_takes_the_files(extractor, tmp_path):
    (tmp_path / 'data/maps').mkdir(parents=True)
    (tmp_path / 'data/maps/ui.map').write_bytes(b'old')
    (tmp_path / 'data/maps.partial').mkdir()
    (tmp_path / 'data/maps.partial/stale.map').write_bytes(b'stale')
    result = run(extractor, make_image(tmp_path / 'halo.iso', MAPS), tmp_path / 'data')
    assert result.returncode == 0, result.stdout + result.stderr
    assert (tmp_path / 'data/maps/ui.map').read_bytes() == MAPS['ui.map']
    assert not (tmp_path / 'data/maps/stale.map').exists()


def test_a_file_that_is_not_an_image_is_refused(extractor, tmp_path):
    (tmp_path / 'x.iso').write_bytes(b'\0' * 0x20000)
    result = run(extractor, tmp_path / 'x.iso', tmp_path / 'data')
    assert result.returncode == 1 and 'not an Xbox disc image' in result.stdout


def test_an_image_without_ui_map_is_refused(extractor, tmp_path):
    maps = {name: data for name, data in MAPS.items() if name != 'ui.map'}
    result = run(extractor, make_image(tmp_path / 'halo.iso', maps), tmp_path / 'data')
    assert result.returncode == 1 and 'no ui.map' in result.stdout
    assert not (tmp_path / 'data/maps').exists()


def test_an_image_cut_short_is_refused(extractor, tmp_path):
    # the root's table is the image's last; cut into the maps before it
    image = make_image(tmp_path / 'halo.iso', MAPS)
    with open(image, 'r+b') as file:
        file.truncate(os.path.getsize(image) - 6 * SECTOR)
    result = run(extractor, image, tmp_path / 'data')
    assert result.returncode == 1, result.stdout
    assert not (tmp_path / 'data/maps').exists()


def test_names_that_must_not_be_created_are_left_out(extractor, tmp_path):
    maps = dict(MAPS, **{'../evil.map': b'e', '.hidden': b'h', 'a\\b.map': b'b', 'c:d.map': b'c'})
    result = run(extractor, make_image(tmp_path / 'halo.iso', maps), tmp_path / 'data')
    assert result.returncode == 0, result.stdout + result.stderr
    assert sorted(os.listdir(tmp_path / 'data/maps')) == sorted(MAPS)
    assert not (tmp_path / 'data/evil.map').exists()


def test_a_tree_that_points_back_at_itself_ends(extractor, tmp_path):
    # ui.map's left subtree is a10.map, whose right subtree is itself and whose left is past the table
    # (make_image puts ui.map's data at sector 0x30, then a10.map's after its 3 sectors and a gap)
    first = entry('ui.map', 0x30, len(MAPS['ui.map']), left=8)
    second = entry('a10.map', 0x34, 10, left=0x7000, right=0)
    looped = bytearray(b'\xff' * SECTOR)
    looped[0:len(first)] = first
    looped[32:32 + len(second)] = second
    looped[32 + 2:32 + 4] = struct.pack('<H', 8)
    result = run(extractor, make_image(tmp_path / 'halo.iso', {'ui.map': MAPS['ui.map'], 'a10.map': b'a' * 10},
                                       maps_table=bytes(looped)), tmp_path / 'data')
    assert result.returncode == 0, result.stdout + result.stderr
    assert sorted(os.listdir(tmp_path / 'data/maps')) == ['a10.map', 'ui.map']


if __name__ == '__main__':
    sys.exit(pytest.main([__file__, '-q']))
