package com.configcenter.server.store;

import com.configcenter.server.model.ConfigKeyEntity;

public final class KeyPaths {

    public static final String APPS = "/apps";
    public static final String CURRENT = "/current";
    public static final String HISTORY = "/history";

    private KeyPaths() {}

    public static String appsIndex(String prefix) {
        return prefix + APPS;
    }

    public static String currentKey(String prefix, ConfigKeyEntity key) {
        return prefix + CURRENT + "/" + key.getApplication()
                + "/" + key.getProfile() + "/" + key.getKey();
    }

    public static String currentPrefix(String prefix) {
        return prefix + CURRENT + "/";
    }

    public static String currentAppPrefix(String prefix, String app) {
        return prefix + CURRENT + "/" + app + "/";
    }

    public static String currentProfilePrefix(String prefix, String app, String profile) {
        return prefix + CURRENT + "/" + app + "/" + profile + "/";
    }

    public static String historyPrefix(String prefix, ConfigKeyEntity key) {
        return prefix + HISTORY + "/" + key.getApplication()
                + "/" + key.getProfile() + "/" + key.getKey() + "/";
    }

    public static String historyKey(String prefix, ConfigKeyEntity key, long version) {
        return prefix + HISTORY + "/" + key.getApplication()
                + "/" + key.getProfile() + "/" + key.getKey() + "/" + version;
    }
}
