package com.profile.config;

import java.io.Serializable;

public class RecalcConfig implements Serializable {

    private static final long serialVersionUID = 1L;

    private long startTimestamp;
    private long endTimestamp;
    private String savepointPath;
    private String newRedisHost;
    private int newRedisPort;
    private String newRedisPassword;
    private int newRedisDatabase;
    private String newKeyPrefix;
    private boolean verifyOnly;

    public long getStartTimestamp() { return startTimestamp; }
    public void setStartTimestamp(long startTimestamp) { this.startTimestamp = startTimestamp; }

    public long getEndTimestamp() { return endTimestamp; }
    public void setEndTimestamp(long endTimestamp) { this.endTimestamp = endTimestamp; }

    public String getSavepointPath() { return savepointPath; }
    public void setSavepointPath(String savepointPath) { this.savepointPath = savepointPath; }

    public String getNewRedisHost() { return newRedisHost; }
    public void setNewRedisHost(String newRedisHost) { this.newRedisHost = newRedisHost; }

    public int getNewRedisPort() { return newRedisPort; }
    public void setNewRedisPort(int newRedisPort) { this.newRedisPort = newRedisPort; }

    public String getNewRedisPassword() { return newRedisPassword; }
    public void setNewRedisPassword(String newRedisPassword) { this.newRedisPassword = newRedisPassword; }

    public int getNewRedisDatabase() { return newRedisDatabase; }
    public void setNewRedisDatabase(int newRedisDatabase) { this.newRedisDatabase = newRedisDatabase; }

    public String getNewKeyPrefix() { return newKeyPrefix; }
    public void setNewKeyPrefix(String newKeyPrefix) { this.newKeyPrefix = newKeyPrefix; }

    public boolean isVerifyOnly() { return verifyOnly; }
    public void setVerifyOnly(boolean verifyOnly) { this.verifyOnly = verifyOnly; }
}
