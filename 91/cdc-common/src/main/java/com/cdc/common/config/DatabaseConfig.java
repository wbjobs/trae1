package com.cdc.common.config;

import com.fasterxml.jackson.annotation.JsonProperty;

public class DatabaseConfig {

    @JsonProperty("type")
    private DatabaseType type;

    @JsonProperty("hostname")
    private String hostname;

    @JsonProperty("port")
    private int port;

    @JsonProperty("username")
    private String username;

    @JsonProperty("password")
    private String password;

    @JsonProperty("database")
    private String database;

    @JsonProperty("serverId")
    private int serverId = 1;

    @JsonProperty("serverName")
    private String serverName = "db-server";

    public DatabaseType getType() {
        return type;
    }

    public void setType(DatabaseType type) {
        this.type = type;
    }

    public String getHostname() {
        return hostname;
    }

    public void setHostname(String hostname) {
        this.hostname = hostname;
    }

    public int getPort() {
        return port;
    }

    public void setPort(int port) {
        this.port = port;
    }

    public String getUsername() {
        return username;
    }

    public void setUsername(String username) {
        this.username = username;
    }

    public String getPassword() {
        return password;
    }

    public void setPassword(String password) {
        this.password = password;
    }

    public String getDatabase() {
        return database;
    }

    public void setDatabase(String database) {
        this.database = database;
    }

    public int getServerId() {
        return serverId;
    }

    public void setServerId(int serverId) {
        this.serverId = serverId;
    }

    public String getServerName() {
        return serverName;
    }

    public void setServerName(String serverName) {
        this.serverName = serverName;
    }

    public enum DatabaseType {
        @JsonProperty("mysql")
        MYSQL,
        @JsonProperty("postgresql")
        POSTGRESQL
    }
}
