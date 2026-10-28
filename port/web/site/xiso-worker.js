/*
XISO-WORKER.JS

Copies the game's maps folder into the site's Origin Private File System,
where the game finds it (/data/maps: port/web/src/web_main.c), from the
player's own Xbox disc image of Halo: Combat Evolved (an "xiso", .iso), as
port/linux/src/xiso.c does for the desktop ports, or from a maps folder
already copied out of one. It runs in a Worker, which can write with
synchronous OPFS access handles (Safari has no OPFS writable streams), and
reads the image a slice at a time: an image is 4 to 7 GB, and nothing of it
is kept but the maps.

The disc reading was written for SilentCartographer from a description of
the Xbox's disc file system (XDVDFS), not from another program's code, as
xiso.c's was: 2048-byte sectors, counted from the game partition's start (at
one of PARTITION_OFFSETS); the partition's sector 32 is its volume
descriptor, MAGIC at its start and at MAGIC_END_OFFSET with the root
directory's sector and size between; and a directory is a table of entries,
each a node of a binary search tree whose root is at the table's start (left
and right subtree offsets in 4-byte units, 0 for none; the data's first
sector and size; attributes; the name's length and the name). The image is
the player's file, so it is untrusted: each table is bounded, every entry is
checked against its table and every extent against the image, and a table's
walk visits each offset once.

Each map is checked as the game checks it when it loads it (cache_files.c):
"head" and "foot" signatures, the Xbox's cache version, and the build of a
retail disc (01.10.12.2276) or of this decompilation's (01.01.14.2342).

Messages from the page: { type: 'image', file } or { type: 'folder', files }
(a maps folder's File objects). To the page: { type: 'progress', file, done,
total }, then { type: 'done', files, bytes } or { type: 'error', message }.
*/

'use strict';

const SECTOR_SIZE = 2048;
const DESCRIPTOR_SECTOR = 32;
// where the game partition starts: an xiso, then images of a whole disc of
// each of the Xbox's and the Xbox 360's disc formats
const PARTITION_OFFSETS = [0, 0x18300000, 0x0FD90000, 0x02080000];
const MAGIC = 'MICROSOFT*XBOX*MEDIA';
const MAGIC_END_OFFSET = 0x7EC;
const ROOT_SECTOR_OFFSET = 20;
const ROOT_SIZE_OFFSET = 24;
const ENTRY_HEADER_SIZE = 14;
const DIRECTORY = 0x10;
// larger than any directory of a game disc
const MAXIMUM_TABLE_SIZE = 4 * 1024 * 1024;
// Halo's maps folder holds about 30 files
const MAXIMUM_FILES = 512;
const COPY_CHUNK = 4 * 1024 * 1024;
const MAP_HEADER_SIZE = 0x800;
const CACHE_VERSION = 5;
const BUILDS = ['01.10.12.2276', '01.01.14.2342'];
// written last: the page starts the game only when it is there
const COMPLETE_MARKER = '.complete';

// size bytes of a File or Blob at offset, as a Uint8Array (shorter where the
// file ends first)
async function readAt(file, offset, size) {
  return new Uint8Array(await file.slice(offset, offset + size).arrayBuffer());
}

function text(bytes, start, length) {
  let result = '';
  for (let index = start; index < start + length && index < bytes.length; index++) {
    result += String.fromCharCode(bytes[index]);
  }
  return result;
}

function u16(bytes, at) {
  return bytes[at] | (bytes[at + 1] << 8);
}

function u32(bytes, at) {
  return (bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (bytes[at + 3] << 24)) >>> 0;
}

// the game partition's start and its root directory's sector and size
async function findVolume(file) {
  for (const partition of PARTITION_OFFSETS) {
    const at = partition + DESCRIPTOR_SECTOR * SECTOR_SIZE;
    if (at + SECTOR_SIZE > file.size) continue;
    const sector = await readAt(file, at, SECTOR_SIZE);
    if (sector.length !== SECTOR_SIZE || text(sector, 0, MAGIC.length) !== MAGIC ||
        text(sector, MAGIC_END_OFFSET, MAGIC.length) !== MAGIC) {
      continue;
    }
    return { partition, rootSector: u32(sector, ROOT_SECTOR_OFFSET), rootSize: u32(sector, ROOT_SIZE_OFFSET) };
  }
  throw new Error('The file is not an Xbox disc image (no XDVDFS volume was found in it).');
}

// a name that is safe to create in the maps folder: no separators or control
// characters, and nothing hidden (the complete marker, "..")
function plainName(name) {
  return name.length > 0 && name[0] !== '.' && !/[\u0000-\u001f\u007f/\\:]/.test(name);
}

// a directory's entries, { name, sector, size, directory }, its tree walked
// from the table's start with each offset visited once
function walkDirectory(table) {
  const units = Math.ceil(table.length / 4);
  const seen = new Uint8Array(units);
  const pending = [0];
  const entries = [];
  while (pending.length > 0) {
    const unit = pending.pop();
    const offset = unit * 4;
    if (unit >= units || seen[unit] || offset + ENTRY_HEADER_SIZE > table.length) continue;
    seen[unit] = 1;
    const left = u16(table, offset);
    const right = u16(table, offset + 2);
    const nameLength = table[offset + 13];
    // sector padding, or a name past the table
    if ((left === 0xFFFF && right === 0xFFFF) || nameLength === 0 ||
        offset + ENTRY_HEADER_SIZE + nameLength > table.length) {
      continue;
    }
    if (entries.length < MAXIMUM_FILES) {
      entries.push({
        name: text(table, offset + ENTRY_HEADER_SIZE, nameLength),
        sector: u32(table, offset + 4),
        size: u32(table, offset + 8),
        directory: (table[offset + 12] & DIRECTORY) !== 0,
      });
    }
    // (0 is no subtree: nothing points back at the root)
    if (left !== 0 && left < units) pending.push(left);
    if (right !== 0 && right < units) pending.push(right);
  }
  return entries;
}

// where an extent starts in the image, or -1 if it does not lie inside it
function extentStart(file, volume, sector, size) {
  const start = volume.partition + sector * SECTOR_SIZE;
  return start <= file.size && size <= file.size - start ? start : -1;
}

// a directory's entries; damaged if its table is empty, too large or not
// inside the image
async function readDirectory(file, volume, sector, size, what) {
  const start = extentStart(file, volume, sector, size);
  const entries = size >= ENTRY_HEADER_SIZE && size <= MAXIMUM_TABLE_SIZE && start >= 0 ?
    walkDirectory(await readAt(file, start, size)) : [];
  if (entries.length === 0) throw new Error(`The disc image's ${what} is damaged.`);
  return entries;
}


// why a map's header is not one the game loads, or null
function mapProblem(header) {
  if (header.length < MAP_HEADER_SIZE) return 'it is too short';
  if (text(header, 0, 4) !== 'daeh' || text(header, 0x7FC, 4) !== 'toof') return 'it is not a map';
  if (u32(header, 4) !== CACHE_VERSION) return 'it is not an Xbox map (its cache version is ' + u32(header, 4) + ')';
  const build = text(header, 0x40, 32).replace(/\0.*$/s, '');
  if (!BUILDS.includes(build)) return 'its build, ' + JSON.stringify(build) + ', is not one the game loads';
  return null;
}

// the files to copy, each { name, size, read(offset, size) }; the maps are
// checked, other files (loading.tga) copied as they are
async function checkedFiles(candidates) {
  const files = [];
  for (const candidate of candidates) {
    const lower = candidate.name.toLowerCase();
    if (!plainName(candidate.name)) continue;
    if (lower.endsWith('.map')) {
      const problem = mapProblem(await candidate.read(0, MAP_HEADER_SIZE));
      if (problem) throw new Error(`${candidate.name} cannot be used: ${problem}.`);
      files.push(candidate);
    } else if (lower === 'loading.tga') {
      files.push(candidate);
    }
  }
  if (!files.some((file) => file.name.toLowerCase() === 'ui.map')) {
    throw new Error('There is no ui.map among the maps: this is not Halo: Combat Evolved for the Xbox.');
  }
  return files;
}

async function imageFiles(file) {
  const volume = await findVolume(file);
  const root = await readDirectory(file, volume, volume.rootSector, volume.rootSize, 'root folder');
  const maps = root.find((entry) => entry.directory && entry.name.toLowerCase() === 'maps');
  if (!maps) throw new Error('The disc image has no maps folder: is it an image of Halo: Combat Evolved?');
  const candidates = [];
  for (const entry of await readDirectory(file, volume, maps.sector, maps.size, 'maps folder')) {
    if (entry.directory || !plainName(entry.name)) continue;
    if (candidates.some((other) => other.name.toLowerCase() === entry.name.toLowerCase())) continue;
    const start = extentStart(file, volume, entry.sector, entry.size);
    if (start < 0) throw new Error(`The disc image ends before ${entry.name} does: is the image complete?`);
    candidates.push({ name: entry.name, size: entry.size, read: (at, size) => readAt(file, start + at, size) });
  }
  return checkedFiles(candidates);
}

async function folderFiles(list) {
  return checkedFiles(list.map((file) => ({ name: file.name, size: file.size, read: (at, size) => readAt(file, at, size) })));
}

async function writeFile(directory, source, onChunk) {
  const handle = await directory.getFileHandle(source.name, { create: true });
  const access = await handle.createSyncAccessHandle();
  try {
    access.truncate(0);
    let position = 0;
    while (position < source.size) {
      const count = Math.min(COPY_CHUNK, source.size - position);
      const bytes = await source.read(position, count);
      if (bytes.length !== count) throw new Error(`Could not read ${source.name} (is the file complete?).`);
      let written = 0;
      while (written < count) {
        const result = access.write(bytes.subarray(written), { at: position + written });
        if (!result) throw new Error(`Could not write ${source.name}: is the device full?`);
        written += result;
      }
      position += count;
      onChunk(count);
    }
    access.flush();
  } finally {
    access.close();
  }
}

async function copy(files) {
  const total = files.reduce((sum, entry) => sum + entry.size, 0);
  const storage = await navigator.storage.getDirectory();
  if (navigator.storage.estimate) {
    const { quota, usage } = await navigator.storage.estimate();
    if (quota && quota - usage < total) {
      throw new Error(`The browser lets this site keep ${Math.floor((quota - usage) / 1e6)} MB more, ` +
        `and the maps take ${Math.ceil(total / 1e6)} MB.`);
    }
  }
  // a new copy replaces whatever an earlier, perhaps interrupted, one left
  await storage.removeEntry('maps', { recursive: true }).catch(() => {});
  const directory = await storage.getDirectoryHandle('maps', { create: true });
  let done = 0;
  let lastReport = 0;
  for (const entry of files) {
    await writeFile(directory, entry, (count) => {
      done += count;
      const now = Date.now();
      if (now - lastReport > 100 || done === total) {
        lastReport = now;
        postMessage({ type: 'progress', file: entry.name, done, total });
      }
    });
  }
  const marker = await directory.getFileHandle(COMPLETE_MARKER, { create: true });
  const access = await marker.createSyncAccessHandle();
  access.truncate(0);
  access.write(new TextEncoder().encode(JSON.stringify({ files: files.map((f) => f.name), bytes: total })), { at: 0 });
  access.flush();
  access.close();
  return { files: files.length, bytes: total };
}

if (typeof onmessage !== 'undefined' || typeof WorkerGlobalScope !== 'undefined') {
  onmessage = async (event) => {
    try {
      const files = event.data.type === 'folder' ? await folderFiles(event.data.files) : await imageFiles(event.data.file);
      postMessage({ type: 'done', ...(await copy(files)) });
    } catch (error) {
      postMessage({ type: 'error', message: error && error.message ? error.message : String(error) });
    }
  };
}

// (for the tests: port/web/tests)
if (typeof module !== 'undefined') {
  module.exports = { findVolume, walkDirectory, mapProblem, imageFiles, folderFiles, plainName };
}
