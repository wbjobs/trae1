package com.quant.gateway.config;

public final class GatewayConfig {

    public static final String WS_HOST = "0.0.0.0";
    public static final int    WS_PORT = 8080;
    public static final String WS_PATH = "/ws";

    public static final int    REST_PORT = 8081;

    public static final String UDP_GROUP   = "239.255.0.1";
    public static final int    UDP_PORT    = 9000;
    public static final String UDP_BIND_IF = "0.0.0.0";

    public static final int CACHE_TICKS_PER_SYMBOL  = 1000;
    public static final int REPLAY_TICKS_MAX        = 500;
    public static final int CLIENT_SESSION_TTL_SEC  = 600;
    public static final int MAX_CLIENTS             = 1000;

    public static final int HEARTBEAT_INTERVAL_SEC  = 15;
    public static final int FEED_TIMEOUT_SEC        = 5;

    private GatewayConfig() {}
}
