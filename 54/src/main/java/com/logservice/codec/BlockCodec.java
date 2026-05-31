package com.logservice.codec;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.List;

public final class BlockCodec {
    public static final int MAGIC = 0x4C4F4742;
    public static final short VERSION = 1;
    public static final int HEADER_SIZE = 4 + 2 + 8 + 8 + 4 + 4 + 4 + 4;

    private BlockCodec() {}

    public static void writeBlock(RandomAccessFile raf, long blockId, long timestamp,
                                  byte[] compressedPayload, List<Integer> lineOffsets) throws IOException {
        ByteBuffer header = ByteBuffer.allocate(HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN);
        header.putInt(MAGIC);
        header.putShort(VERSION);
        header.putLong(blockId);
        header.putLong(timestamp);
        header.putInt(lineOffsets.size());
        header.putInt(compressedPayload.length);
        int offsetTableSize = lineOffsets.size() * Integer.BYTES;
        header.putInt(offsetTableSize);
        header.flip();
        raf.write(header.array());

        ByteBuffer offsets = ByteBuffer.allocate(offsetTableSize).order(ByteOrder.LITTLE_ENDIAN);
        for (int off : lineOffsets) offsets.putInt(off);
        offsets.flip();
        raf.write(offsets.array());

        raf.write(compressedPayload);

        int crc = crc32(compressedPayload);
        ByteBuffer footer = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN);
        footer.putInt(crc).flip();
        raf.write(footer.array());
    }

    public static BlockInfo readBlock(RandomAccessFile raf) throws IOException {
        byte[] headerBytes = new byte[HEADER_SIZE];
        int n = raf.read(headerBytes);
        if (n < HEADER_SIZE) return null;

        ByteBuffer header = ByteBuffer.wrap(headerBytes).order(ByteOrder.LITTLE_ENDIAN);
        int magic = header.getInt();
        if (magic != MAGIC) {
            throw new IOException("Invalid block magic: " + magic);
        }
        short version = header.getShort();
        if (version != VERSION) {
            throw new IOException("Unsupported block version: " + version);
        }
        long blockId = header.getLong();
        long timestamp = header.getLong();
        int lineCount = header.getInt();
        int payloadSize = header.getInt();
        int offsetTableSize = header.getInt();

        byte[] offsetsBytes = new byte[offsetTableSize];
        raf.readFully(offsetsBytes);
        ByteBuffer offsetsBuf = ByteBuffer.wrap(offsetsBytes).order(ByteOrder.LITTLE_ENDIAN);
        int[] lineOffsets = new int[lineCount];
        for (int i = 0; i < lineCount; i++) lineOffsets[i] = offsetsBuf.getInt();

        byte[] payload = new byte[payloadSize];
        raf.readFully(payload);

        byte[] footer = new byte[4];
        raf.readFully(footer);
        int storedCrc = ByteBuffer.wrap(footer).order(ByteOrder.LITTLE_ENDIAN).getInt();
        int actualCrc = crc32(payload);
        if (storedCrc != actualCrc) {
            throw new IOException("CRC mismatch for block " + blockId);
        }

        return new BlockInfo(blockId, timestamp, lineOffsets, payload);
    }

    public static int[] computeLineLengths(int[] lineOffsets, int totalRawSize) {
        int[] lengths = new int[lineOffsets.length];
        for (int i = 0; i < lineOffsets.length - 1; i++) {
            lengths[i] = lineOffsets[i + 1] - lineOffsets[i];
        }
        if (lineOffsets.length > 0) {
            lengths[lineOffsets.length - 1] = totalRawSize - lineOffsets[lineOffsets.length - 1];
        }
        return lengths;
    }

    private static int crc32(byte[] data) {
        java.util.zip.CRC32 crc = new java.util.zip.CRC32();
        crc.update(data);
        return (int) crc.getValue();
    }

    public static class BlockInfo {
        public final long blockId;
        public final long timestamp;
        public final int[] lineOffsets;
        public final byte[] compressedPayload;

        public BlockInfo(long blockId, long timestamp, int[] lineOffsets, byte[] compressedPayload) {
            this.blockId = blockId;
            this.timestamp = timestamp;
            this.lineOffsets = lineOffsets;
            this.compressedPayload = compressedPayload;
        }
    }
}
