package com.profile.store.bitmap;

import org.roaringbitmap.RoaringBitmap;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Iterator;

/**
 * RoaringBitmap 序列化工具。
 * <p>
 * 采用官方 portable format 进行序列化，序列化后的字节数组可直接存入 Redis。
 * 序列化前调用 {@link RoaringBitmap#runOptimize()} 以进一步压缩数据，
 * 适合大规模稀疏/连续位图存储。
 */
public final class BitmapSerde {

    private BitmapSerde() {}

    public static byte[] serialize(RoaringBitmap bitmap) {
        if (bitmap == null) {
            return new byte[0];
        }
        bitmap.runOptimize();
        int size = bitmap.serializedSize();
        ByteBuffer buffer = ByteBuffer.allocate(size);
        bitmap.serialize(buffer);
        byte[] result = new byte[size];
        buffer.flip();
        buffer.get(result);
        return result;
    }

    public static RoaringBitmap deserialize(byte[] data) {
        if (data == null || data.length == 0) {
            return new RoaringBitmap();
        }
        RoaringBitmap bitmap = new RoaringBitmap();
        bitmap.deserialize(ByteBuffer.wrap(data));
        return bitmap;
    }

    public static int[] toArray(RoaringBitmap bitmap) {
        if (bitmap == null) return new int[0];
        return bitmap.toArray();
    }

    public static int[] pageToArray(RoaringBitmap bitmap, int offset, int limit) {
        if (bitmap == null || limit <= 0) return new int[0];
        int total = bitmap.getCardinality();
        if (offset >= total) return new int[0];
        int end = Math.min(offset + limit, total);
        int[] result = new int[end - offset];
        Iterator<Integer> it = bitmap.iterator();
        int idx = 0;
        int pos = 0;
        while (it.hasNext() && pos < end) {
            int v = it.next();
            if (pos >= offset) {
                result[idx++] = v;
            }
            pos++;
        }
        if (idx < result.length) {
            int[] trimmed = new int[idx];
            System.arraycopy(result, 0, trimmed, 0, idx);
            return trimmed;
        }
        return result;
    }
}
