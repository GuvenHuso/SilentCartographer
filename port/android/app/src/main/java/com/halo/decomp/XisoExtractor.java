package com.halo.decomp;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Copies the maps folder out of an Xbox disc image (an "xiso", or an image of
 * a whole disc), as the desktop games do (port/linux/src/xiso.c, whose
 * checks this has too).
 *
 * Written for SilentCartographer from a description of the Xbox's disc file
 * system (XDVDFS), not from another program's code: 2048-byte sectors,
 * counted from the game partition's start (at one of PARTITION_OFFSETS); the
 * partition's sector 32 is its volume descriptor, VOLUME_MAGIC at its start
 * and at MAGIC_END_OFFSET with the root directory's sector and size between;
 * and a directory is a table of entries, each a node of a binary search tree
 * whose root is at the table's start (left and right subtree offsets in
 * 4-byte units, 0 for none; the data's first sector and size; attributes,
 * DIRECTORY for a directory; the name's length and the name).
 *
 * The image is untrusted: each table is bounded, every entry is checked
 * against its table and every extent against the image, and a table's walk
 * visits each offset once.
 *
 * The files are written to maps.partial first and moved into maps once they
 * all are, so an interrupted extraction never leaves maps/ui.map behind.
 */
final class XisoExtractor {
    private static final int SECTOR_SIZE = 2048;
    private static final long DESCRIPTOR_SECTOR = 32;
    private static final byte[] VOLUME_MAGIC = "MICROSOFT*XBOX*MEDIA".getBytes(StandardCharsets.US_ASCII);
    private static final int MAGIC_END_OFFSET = 0x7EC;
    private static final int ROOT_SECTOR_OFFSET = 20;
    private static final int ROOT_SIZE_OFFSET = 24;
    private static final int ENTRY_HEADER_SIZE = 14;
    private static final int DIRECTORY = 0x10;
    /* larger than any directory of a game disc */
    private static final long MAXIMUM_TABLE_SIZE = 4L * 1024 * 1024;
    /* Halo's maps folder holds about 30 files */
    private static final int MAXIMUM_FILES = 512;
    private static final int COPY_SIZE = 1024 * 1024;
    /* where the game partition starts: an xiso, then images of a whole disc
       of each of the Xbox's and the Xbox 360's disc formats */
    private static final long[] PARTITION_OFFSETS = { 0, 0x18300000L, 0x0FD90000L, 0x02080000L };

    interface Progress {
        void report(String file, long done, long total);
    }

    /** a reason the player can act on */
    static final class ExtractException extends IOException {
        ExtractException(String message) {
            super(message);
        }
    }

    static final class Entry {
        final String name;
        final long sector;
        final long size;
        final boolean directory;

        Entry(String name, long sector, long size, boolean directory) {
            this.name = name;
            this.sector = sector;
            this.size = size;
            this.directory = directory;
        }
    }

    private final FileChannel image;
    private final long imageSize;
    private long partition;

    private XisoExtractor(FileChannel image) throws IOException {
        this.image = image;
        this.imageSize = image.size();
    }

    /** copies the image's maps folder to destination/maps */
    static void extractMaps(FileChannel image, File destination, Progress progress) throws IOException {
        new XisoExtractor(image).extract(destination, progress);
    }

    private void extract(File destination, Progress progress) throws IOException {
        long[] root = findPartition();
        Entry mapsFolder = null;
        List<Entry> files = new ArrayList<>();
        long total = 0;
        long done = 0;
        boolean hasUi = false;

        List<Entry> rootEntries = listTable(readTable(root[0], root[1]));
        if (rootEntries.isEmpty())
            throw new ExtractException("The disc image's root folder is damaged.");
        for (Entry entry : rootEntries) {
            if (entry.directory && entry.name.equalsIgnoreCase("maps")) {
                mapsFolder = entry;
                break;
            }
        }
        if (mapsFolder == null)
            throw new ExtractException("The disc image has no maps folder: is it an image of Halo: Combat Evolved?");
        List<Entry> mapsEntries = listTable(readTable(mapsFolder.sector, mapsFolder.size));
        if (mapsEntries.isEmpty())
            throw new ExtractException("The disc image's maps folder is damaged.");

        // plain names only, each once, each inside the image
        for (Entry entry : mapsEntries) {
            boolean seen = false;

            if (entry.directory || !isPlainName(entry.name))
                continue;
            for (Entry other : files)
                seen |= other.name.equalsIgnoreCase(entry.name);
            if (seen)
                continue;
            if (!extentFits(entry.sector, entry.size))
                throw new ExtractException("The disc image ends before " + entry.name + " does: is the image complete?");
            hasUi |= entry.name.equalsIgnoreCase("ui.map");
            total += entry.size;
            files.add(entry);
        }
        if (!hasUi)
            throw new ExtractException("The disc image's maps folder has no ui.map: is it an image of Halo: Combat Evolved?");
        if (destination.getUsableSpace() > 0 && destination.getUsableSpace() < total)
            throw new ExtractException("There is not enough free space for the maps folder ("
                + ((total + (1 << 20) - 1) >> 20) + " MB).");

        File partial = new File(destination, "maps.partial");
        File maps = new File(destination, "maps");
        if (!partial.isDirectory() && !partial.mkdirs())
            throw new ExtractException("The folder " + partial + " could not be made.");
        File[] stale = partial.listFiles();
        if (stale != null) {
            for (File file : stale)
                file.delete();
        }
        ByteBuffer buffer = ByteBuffer.allocate(COPY_SIZE);
        for (Entry file : files) {
            long source = partition + file.sector * SECTOR_SIZE;
            long left = file.size;

            progress.report(file.name, done, total);
            try (FileOutputStream output = new FileOutputStream(new File(partial, file.name))) {
                while (left > 0) {
                    int count = (int) Math.min(left, COPY_SIZE);

                    buffer.clear();
                    buffer.limit(count);
                    if (!readAt(source, buffer))
                        throw new ExtractException("The disc image could not be read at " + file.name + ": is the image complete?");
                    output.write(buffer.array(), 0, count);
                    source += count;
                    left -= count;
                    done += count;
                    progress.report(file.name, done, total);
                }
            }
        }
        moveIntoPlace(files, partial, maps);
    }

    /** reads buffer's remaining bytes at offset (from the image's start); false if the image ends first */
    private boolean readAt(long offset, ByteBuffer buffer) throws IOException {
        if (offset < 0 || offset > imageSize || buffer.remaining() > imageSize - offset)
            return false;
        while (buffer.hasRemaining()) {
            int count = image.read(buffer, offset);

            if (count <= 0)
                return false;
            offset += count;
        }
        return true;
    }

    /** whether size bytes from the partition's sector lie inside the image */
    private boolean extentFits(long sector, long size) {
        long start = partition + sector * SECTOR_SIZE;

        return start >= 0 && start <= imageSize && size <= imageSize - start;
    }

    /** the root directory's sector and size, with partition set */
    private long[] findPartition() throws IOException {
        ByteBuffer sector = ByteBuffer.allocate(SECTOR_SIZE).order(ByteOrder.LITTLE_ENDIAN);

        for (long offset : PARTITION_OFFSETS) {
            sector.clear();
            if (!readAt(offset + DESCRIPTOR_SECTOR * SECTOR_SIZE, sector))
                continue;
            if (!magicAt(sector, 0) || !magicAt(sector, MAGIC_END_OFFSET))
                continue;
            partition = offset;
            return new long[] {
                sector.getInt(ROOT_SECTOR_OFFSET) & 0xFFFFFFFFL,
                sector.getInt(ROOT_SIZE_OFFSET) & 0xFFFFFFFFL,
            };
        }
        throw new ExtractException("The file is not an Xbox disc image (no XDVDFS volume was found in it).");
    }

    private static boolean magicAt(ByteBuffer buffer, int offset) {
        for (int index = 0; index < VOLUME_MAGIC.length; index++) {
            if (buffer.get(offset + index) != VOLUME_MAGIC[index])
                return false;
        }
        return true;
    }

    /** a directory's table, or null if it is empty, too large or not inside the image */
    private ByteBuffer readTable(long sector, long size) throws IOException {
        if (size < ENTRY_HEADER_SIZE || size > MAXIMUM_TABLE_SIZE || !extentFits(sector, size))
            return null;
        ByteBuffer table = ByteBuffer.allocate((int) size).order(ByteOrder.LITTLE_ENDIAN);
        if (!readAt(partition + sector * SECTOR_SIZE, table))
            return null;
        table.clear();
        return table;
    }

    /** a directory's entries, its tree walked from the table's start with each offset visited once */
    static List<Entry> listTable(ByteBuffer table) {
        List<Entry> entries = new ArrayList<>();

        if (table == null)
            return entries;
        int size = table.limit();
        int units = (size + 3) / 4;
        boolean[] seen = new boolean[units];
        ArrayDeque<Integer> pending = new ArrayDeque<>();
        pending.push(0);
        while (!pending.isEmpty()) {
            int unit = pending.pop();
            int offset = unit * 4;

            if (unit >= units || seen[unit] || offset + ENTRY_HEADER_SIZE > size)
                continue;
            seen[unit] = true;
            int left = table.getShort(offset) & 0xFFFF;
            int right = table.getShort(offset + 2) & 0xFFFF;
            int nameLength = table.get(offset + 13) & 0xFF;
            // sector padding, or a name past the table
            if ((left == 0xFFFF && right == 0xFFFF) || nameLength == 0 || offset + ENTRY_HEADER_SIZE + nameLength > size)
                continue;
            if (entries.size() < MAXIMUM_FILES) {
                byte[] name = new byte[nameLength];
                for (int index = 0; index < nameLength; index++)
                    name[index] = table.get(offset + ENTRY_HEADER_SIZE + index);
                entries.add(new Entry(new String(name, StandardCharsets.ISO_8859_1),
                    table.getInt(offset + 4) & 0xFFFFFFFFL, table.getInt(offset + 8) & 0xFFFFFFFFL,
                    (table.get(offset + 12) & DIRECTORY) != 0));
            }
            // (0 is no subtree: nothing points back at the root)
            if (left != 0 && left < units)
                pending.push(left);
            if (right != 0 && right < units)
                pending.push(right);
        }
        return entries;
    }

    /** a name that is safe to create in the maps folder: no separators or control characters, nothing hidden */
    static boolean isPlainName(String name) {
        if (name.isEmpty() || name.charAt(0) == '.')
            return false;
        for (int index = 0; index < name.length(); index++) {
            char character = name.charAt(index);

            if (character < 0x20 || character == 0x7F || character == '/' || character == '\\' || character == ':')
                return false;
        }
        return true;
    }

    /** maps.partial becomes maps; into a maps folder already there (the app makes one) the files move one by one */
    private static void moveIntoPlace(List<Entry> files, File partial, File maps) throws IOException {
        String[] existing = maps.list();

        if (existing != null && existing.length == 0)
            maps.delete();
        if (!maps.exists() && partial.renameTo(maps))
            return;
        if (!maps.isDirectory() && !maps.mkdirs())
            throw new ExtractException("The maps folder could not be put in place.");
        for (Entry file : files) {
            File to = new File(maps, file.name);
            File[] namesakes = maps.listFiles((directory, name) -> name.toLowerCase(Locale.ROOT)
                .equals(file.name.toLowerCase(Locale.ROOT)));

            if (namesakes != null) {
                for (File namesake : namesakes)
                    namesake.delete();
            }
            if (!new File(partial, file.name).renameTo(to))
                throw new ExtractException("The maps folder could not be put in place.");
        }
        partial.delete();
    }
}
