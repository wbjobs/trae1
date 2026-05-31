package com.alibaba.polardb.index.hash;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Collection;
import java.util.SortedMap;
import java.util.TreeMap;

public class ConsistentHash<T> {

    private final HashFunction hashFunction;
    private final int numberOfReplicas;
    private final SortedMap<Long, T> circle = new TreeMap<>();

    public interface HashFunction {
        long hash(Object key);
    }

    public static class MD5HashFunction implements HashFunction {
        private static final ThreadLocal<MessageDigest> MD5_HOLDER = ThreadLocal.withInitial(() -> {
            try {
                return MessageDigest.getInstance("MD5");
            } catch (NoSuchAlgorithmException e) {
                throw new RuntimeException("MD5 algorithm not available", e);
            }
        });

        @Override
        public long hash(Object key) {
            MessageDigest md5 = MD5_HOLDER.get();
            md5.reset();
            byte[] bytes = md5.digest(String.valueOf(key).getBytes());
            long hash = 0;
            for (int i = 0; i < 8; i++) {
                hash |= ((long) (bytes[i] & 0xFF)) << (8 * i);
            }
            return hash & 0x7fffffffffffffffL;
        }
    }

    public ConsistentHash(int numberOfReplicas, Collection<T> nodes) {
        this(new MD5HashFunction(), numberOfReplicas, nodes);
    }

    public ConsistentHash(HashFunction hashFunction, int numberOfReplicas, Collection<T> nodes) {
        this.hashFunction = hashFunction;
        this.numberOfReplicas = numberOfReplicas;

        if (nodes != null) {
            for (T node : nodes) {
                add(node);
            }
        }
    }

    public void add(T node) {
        for (int i = 0; i < numberOfReplicas; i++) {
            long hash = hashFunction.hash(node.toString() + "#" + i);
            circle.put(hash, node);
        }
    }

    public void remove(T node) {
        for (int i = 0; i < numberOfReplicas; i++) {
            long hash = hashFunction.hash(node.toString() + "#" + i);
            circle.remove(hash);
        }
    }

    public T get(Object key) {
        if (circle.isEmpty()) {
            return null;
        }

        long hash = hashFunction.hash(key);
        if (!circle.containsKey(hash)) {
            SortedMap<Long, T> tailMap = circle.tailMap(hash);
            hash = tailMap.isEmpty() ? circle.firstKey() : tailMap.firstKey();
        }
        return circle.get(hash);
    }

    public long getHash(Object key) {
        return hashFunction.hash(key);
    }

    public boolean isEmpty() {
        return circle.isEmpty();
    }

    public int size() {
        return circle.size() / numberOfReplicas;
    }

    public SortedMap<Long, T> getCircle() {
        return new TreeMap<>(circle);
    }

    public HashRange getNodeHashRange(T node) {
        if (!circle.containsValue(node)) {
            return null;
        }

        long minHash = Long.MAX_VALUE;
        long maxHash = Long.MIN_VALUE;

        for (int i = 0; i < numberOfReplicas; i++) {
            long hash = hashFunction.hash(node.toString() + "#" + i);
            minHash = Math.min(minHash, hash);
            maxHash = Math.max(maxHash, hash);
        }

        return new HashRange(minHash, maxHash);
    }

    public static class HashRange {
        private final long start;
        private final long end;

        public HashRange(long start, long end) {
            this.start = start;
            this.end = end;
        }

        public long getStart() {
            return start;
        }

        public long getEnd() {
            return end;
        }

        public boolean contains(long hash) {
            if (start <= end) {
                return hash >= start && hash <= end;
            } else {
                return hash >= start || hash <= end;
            }
        }

        @Override
        public String toString() {
            return "[" + start + ", " + end + "]";
        }
    }

    public boolean isKeyMappedToNode(Object key, T node) {
        T mappedNode = get(key);
        return node != null && node.equals(mappedNode);
    }

    public java.util.Set<T> getNodes() {
        return new java.util.HashSet<>(circle.values());
    }
}
