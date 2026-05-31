package com.logservice.model;

public class LineMetadata {
    private final long lineId;
    private final long blockId;
    private final int offsetInBlock;
    private final int length;
    private final long timestamp;

    public LineMetadata(long lineId, long blockId, int offsetInBlock, int length, long timestamp) {
        this.lineId = lineId;
        this.blockId = blockId;
        this.offsetInBlock = offsetInBlock;
        this.length = length;
        this.timestamp = timestamp;
    }

    public long getLineId() { return lineId; }
    public long getBlockId() { return blockId; }
    public int getOffsetInBlock() { return offsetInBlock; }
    public int getLength() { return length; }
    public long getTimestamp() { return timestamp; }
}
