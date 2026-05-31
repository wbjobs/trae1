package com.quant.gateway.codec;

public enum MsgType {
    TICK(0),
    ORDER(1),
    SNAPSHOT(2),
    HEARTBEAT(3),
    SUBSCRIBE_REQ(4),
    SUBSCRIBE_RESP(5),
    HELLO(6),
    REPLAY_START(7),
    REPLAY_END(8),
    DEGRADE_NOTICE(9),
    AGGREGATE(10);

    private final int code;

    MsgType(int code) {
        this.code = code;
    }

    public int code() { return code; }

    public static MsgType fromCode(int code) {
        for (MsgType t : values()) {
            if (t.code == code) return t;
        }
        return null;
    }
}
