package com.rsocket.gateway.scripting.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.Duration;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ScriptExecutionResult {

    private String scriptId;
    private String scriptName;
    private boolean success;
    private boolean timedOut;
    private Duration executionTime;
    private String errorMessage;
    private Object result;
    private InterceptContext context;
    private boolean fallbackTriggered;
    private ScriptDefinition.FallbackStrategy fallbackStrategy;
}
