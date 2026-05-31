package com.profile.store.bitmap;

import org.roaringbitmap.RoaringBitmap;

import java.util.Collection;
import java.util.List;

/**
 * RoaringBitmap 集合运算工具。
 * <p>
 * 所有运算均返回新的 RoaringBitmap 实例，避免修改输入位图。
 * 对于1亿用户规模，RoaringBitmap 的 AND/OR/ANDNOT 运算在几十毫秒内即可完成。
 */
public final class BitmapOperations {

    private BitmapOperations() {}

    public static RoaringBitmap and(Collection<RoaringBitmap> bitmaps) {
        if (bitmaps == null || bitmaps.isEmpty()) return new RoaringBitmap();
        RoaringBitmap[] arr = bitmaps.toArray(new RoaringBitmap[0]);
        return RoaringBitmap.and(arr);
    }

    public static RoaringBitmap or(Collection<RoaringBitmap> bitmaps) {
        if (bitmaps == null || bitmaps.isEmpty()) return new RoaringBitmap();
        RoaringBitmap[] arr = bitmaps.toArray(new RoaringBitmap[0]);
        return RoaringBitmap.or(arr);
    }

    public static RoaringBitmap andNot(RoaringBitmap left, RoaringBitmap right) {
        if (left == null) return new RoaringBitmap();
        if (right == null) return left.clone();
        return RoaringBitmap.andNot(left, right);
    }

    public static RoaringBitmap xor(RoaringBitmap left, RoaringBitmap right) {
        if (left == null && right == null) return new RoaringBitmap();
        if (left == null) return right.clone();
        if (right == null) return left.clone();
        return RoaringBitmap.xor(left, right);
    }

    public static RoaringBitmap not(RoaringBitmap source, int universeMax) {
        RoaringBitmap universe = new RoaringBitmap();
        universe.add(0, (long) universeMax + 1);
        return andNot(universe, source);
    }

    public static int cardinality(RoaringBitmap bitmap) {
        return bitmap == null ? 0 : bitmap.getCardinality();
    }

    public static List<RoaringBitmap> sortBySize(List<RoaringBitmap> bitmaps) {
        bitmaps.sort((a, b) -> Integer.compare(a.getCardinality(), b.getCardinality()));
        return bitmaps;
    }
}
