package com.profile.store.dto;

import jakarta.validation.constraints.Min;
import jakarta.validation.constraints.NotBlank;

public class ShardAddRequest {

    @NotBlank(message = "host 不能为空")
    private String host;

    @Min(value = 1, message = "port 必须大于 0")
    private int port = 6379;

    private String password;

    public String getHost() { return host; }
    public void setHost(String host) { this.host = host; }

    public int getPort() { return port; }
    public void setPort(int port) { this.port = port; }

    public String getPassword() { return password; }
    public void setPassword(String password) { this.password = password; }
}
