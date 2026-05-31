package com.quant.gateway.ws;

import io.netty.util.AttributeKey;

final class Attrs {
    static final AttributeKey<String> CLIENT_ID = AttributeKey.valueOf("clientId");

    private Attrs() {}
}
