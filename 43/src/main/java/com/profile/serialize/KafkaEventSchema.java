package com.profile.serialize;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.profile.model.UserBehaviorEvent;
import org.apache.flink.api.common.serialization.DeserializationSchema;
import org.apache.flink.api.common.typeinfo.TypeInformation;

import java.io.IOException;

public class KafkaEventSchema implements DeserializationSchema<UserBehaviorEvent> {

    private static final long serialVersionUID = 1L;

    private static final ObjectMapper MAPPER = new ObjectMapper();

    @Override
    public UserBehaviorEvent deserialize(byte[] message) throws IOException {
        if (message == null || message.length == 0) {
            return null;
        }
        try {
            return MAPPER.readValue(message, UserBehaviorEvent.class);
        } catch (Exception e) {
            return null;
        }
    }

    @Override
    public boolean isEndOfStream(UserBehaviorEvent nextElement) {
        return false;
    }

    @Override
    public TypeInformation<UserBehaviorEvent> getProducedType() {
        return TypeInformation.of(UserBehaviorEvent.class);
    }
}
