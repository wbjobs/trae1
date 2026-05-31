package com.configcenter.server.store;

public final class GrayPaths {

    public static final String GRAY = "/gray";
    public static final String ACTIVE = "/gray/active";
    public static final String CLIENTS = "/clients";

    private GrayPaths() {}

    public static String batchKey(String prefix, String batchId) {
        return prefix + GRAY + "/batches/" + batchId;
    }

    public static String batchesPrefix(String prefix) {
        return prefix + GRAY + "/batches/";
    }

    public static String activeKey(String prefix, String app, String profile, String key) {
        return prefix + ACTIVE + "/" + app + "/" + profile + "/" + key;
    }

    public static String activePrefix(String prefix) {
        return prefix + ACTIVE + "/";
    }

    public static String clientKey(String prefix, String instanceId) {
        return prefix + CLIENTS + "/" + instanceId;
    }

    public static String clientsPrefix(String prefix) {
        return prefix + CLIENTS + "/";
    }

    public static String clientsByAppPrefix(String prefix, String app) {
        return prefix + CLIENTS + "/_byApp/" + app + "/";
    }
}
