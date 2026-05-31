package com.sharding.sync.config;

public final class DataSourceContextHolder {

    private static final ThreadLocal<String> HOLDER = new ThreadLocal<>();

    private DataSourceContextHolder() {
    }

    public static void set(String key) {
        HOLDER.set(key);
    }

    public static String get() {
        return HOLDER.get();
    }

    public static void clear() {
        HOLDER.remove();
    }
}
