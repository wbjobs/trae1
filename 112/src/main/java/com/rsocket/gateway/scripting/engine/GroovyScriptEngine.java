package com.rsocket.gateway.scripting.engine;

import com.github.benmanes.caffeine.cache.Cache;
import com.github.benmanes.caffeine.cache.Caffeine;
import com.rsocket.gateway.scripting.model.InterceptContext;
import com.rsocket.gateway.scripting.model.ScriptDefinition;
import com.rsocket.gateway.scripting.model.ScriptExecutionResult;
import groovy.lang.Binding;
import groovy.lang.GroovyShell;
import lombok.extern.slf4j.Slf4j;
import org.codehaus.groovy.control.CompilerConfiguration;
import org.springframework.stereotype.Component;

import java.time.Duration;
import java.time.Instant;
import java.util.concurrent.*;

@Slf4j
@Component
public class GroovyScriptEngine {

    public static final int DEFAULT_TIMEOUT_MS = 50;

    private final ExecutorService executor = Executors.newFixedThreadPool(
            Runtime.getRuntime().availableProcessors(),
            r -> {
                Thread t = new Thread(r, "groovy-script-executor");
                t.setDaemon(true);
                return t;
            }
    );

    private final Cache<String, Class<?>> scriptCache = Caffeine.newBuilder()
            .maximumSize(1000)
            .expireAfterAccess(Duration.ofHours(1))
            .build();

    private final GroovyShell groovyShell;

    public GroovyScriptEngine() {
        CompilerConfiguration config = new CompilerConfiguration();
        config.setSourceEncoding("UTF-8");
        this.groovyShell = new GroovyShell(config);
    }

    public ScriptExecutionResult execute(ScriptDefinition script, InterceptContext context) {
        Instant startTime = Instant.now();
        int timeout = script.getTimeoutMs() > 0 ? script.getTimeoutMs() : DEFAULT_TIMEOUT_MS;

        Future<ScriptExecutionResult> future = executor.submit(() ->
                executeScriptInternal(script, context, startTime));

        try {
            ScriptExecutionResult result = future.get(timeout, TimeUnit.MILLISECONDS);
            return result;
        } catch (TimeoutException e) {
            future.cancel(true);
            log.warn("Script execution timed out after {}ms: {}", timeout, script.getName());
            return buildTimeoutResult(script, context, startTime);
        } catch (InterruptedException | ExecutionException e) {
            log.error("Script execution error: {}", script.getName(), e);
            return buildErrorResult(script, context, startTime, e.getCause() != null ? e.getCause() : e);
        }
    }

    private ScriptExecutionResult executeScriptInternal(ScriptDefinition script,
                                                        InterceptContext context,
                                                        Instant startTime) {
        try {
            Binding binding = new Binding();
            binding.setVariable("context", context);
            binding.setVariable("log", log);
            binding.setVariable("payload", context.getPayload());
            binding.setVariable("metadata", context.getMetadata());
            binding.setVariable("route", context.getRoute());
            binding.setVariable("interactionType", context.getInteractionType());
            binding.setVariable("clientId", context.getClientId());
            binding.setVariable("connectionId", context.getConnectionId());
            binding.setVariable("response", context.getResponse());
            binding.setVariable("error", context.getError());

            Class<?> scriptClass = getCompiledScript(script);
            groovy.lang.Script scriptInstance = (groovy.lang.Script) scriptClass.getDeclaredConstructor().newInstance();
            scriptInstance.setBinding(binding);

            Object result = scriptInstance.run();

            return ScriptExecutionResult.builder()
                    .scriptId(script.getId())
                    .scriptName(script.getName())
                    .success(true)
                    .timedOut(false)
                    .executionTime(Duration.between(startTime, Instant.now()))
                    .result(result)
                    .context(context)
                    .fallbackTriggered(false)
                    .build();

        } catch (Exception e) {
            log.error("Script execution failed: {}", script.getName(), e);
            return buildErrorResult(script, context, startTime, e);
        }
    }

    private Class<?> getCompiledScript(ScriptDefinition script) throws Exception {
        String cacheKey = script.getId() + ":" + script.getVersion();
        Class<?> cached = scriptCache.getIfPresent(cacheKey);
        if (cached != null) {
            return cached;
        }

        synchronized (this) {
            cached = scriptCache.getIfPresent(cacheKey);
            if (cached != null) {
                return cached;
            }

            Class<?> compiled = groovyShell.getClassLoader().parseClass(script.getScript());
            scriptCache.put(cacheKey, compiled);
            return compiled;
        }
    }

    private ScriptExecutionResult buildTimeoutResult(ScriptDefinition script,
                                                     InterceptContext context,
                                                     Instant startTime) {
        ScriptExecutionResult result = ScriptExecutionResult.builder()
                .scriptId(script.getId())
                .scriptName(script.getName())
                .success(false)
                .timedOut(true)
                .executionTime(Duration.between(startTime, Instant.now()))
                .errorMessage("Script execution timed out after " + script.getTimeoutMs() + "ms")
                .context(context)
                .fallbackTriggered(true)
                .fallbackStrategy(script.getFallbackStrategy())
                .build();

        applyFallback(script, context, result);
        return result;
    }

    private ScriptExecutionResult buildErrorResult(ScriptDefinition script,
                                                   InterceptContext context,
                                                   Instant startTime,
                                                   Throwable error) {
        ScriptExecutionResult result = ScriptExecutionResult.builder()
                .scriptId(script.getId())
                .scriptName(script.getName())
                .success(false)
                .timedOut(false)
                .executionTime(Duration.between(startTime, Instant.now()))
                .errorMessage(error.getMessage())
                .context(context)
                .fallbackTriggered(true)
                .fallbackStrategy(script.getFallbackStrategy())
                .build();

        applyFallback(script, context, result);
        return result;
    }

    private void applyFallback(ScriptDefinition script, InterceptContext context,
                               ScriptExecutionResult result) {
        switch (script.getFallbackStrategy()) {
            case ALLOW:
                log.debug("Fallback: ALLOW for script {}", script.getName());
                break;
            case REJECT:
                log.debug("Fallback: REJECT for script {}", script.getName());
                context.reject("Script execution failed: " + result.getErrorMessage());
                break;
            case LOG_ONLY:
                log.warn("Fallback: LOG_ONLY for script {}, error: {}",
                        script.getName(), result.getErrorMessage());
                break;
        }
    }

    public void invalidateScriptCache(String scriptId) {
        scriptCache.asMap().keySet().removeIf(key -> key.startsWith(scriptId + ":"));
        log.info("Invalidated script cache for: {}", scriptId);
    }

    public void clearCache() {
        scriptCache.invalidateAll();
        log.info("Cleared all script cache");
    }
}
