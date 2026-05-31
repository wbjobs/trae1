package com.rsocket.gateway.scripting;

import com.rsocket.gateway.scripting.engine.GroovyScriptEngine;
import com.rsocket.gateway.scripting.model.InterceptContext;
import com.rsocket.gateway.scripting.model.ScriptDefinition;
import com.rsocket.gateway.scripting.model.ScriptExecutionResult;
import jakarta.annotation.PostConstruct;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Component;

import java.time.Instant;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;

@Slf4j
@Component
@RequiredArgsConstructor
public class ScriptManager {

    private final Map<String, ScriptDefinition> scripts = new ConcurrentHashMap<>();
    private final GroovyScriptEngine scriptEngine;
    private final ScriptTemplateLibrary templateLibrary;

    @PostConstruct
    public void init() {
        loadDefaultTemplates();
    }

    private void loadDefaultTemplates() {
        log.info("Loading default script templates...");
        templateLibrary.getTemplates().forEach(template -> {
            try {
                registerScript(template);
                log.info("Loaded template: {}", template.getName());
            } catch (Exception e) {
                log.error("Failed to load template: {}", template.getName(), e);
            }
        });
    }

    public ScriptDefinition registerScript(ScriptDefinition script) {
        if (script.getId() == null) {
            script.setId(UUID.randomUUID().toString());
        }
        if (script.getCreatedAt() == null) {
            script.setCreatedAt(Instant.now());
        }
        script.setUpdatedAt(Instant.now());
        if (script.getVersion() == null) {
            script.setVersion("1.0");
        }
        if (script.getFallbackStrategy() == null) {
            script.setFallbackStrategy(ScriptDefinition.FallbackStrategy.ALLOW);
        }
        if (script.getTimeoutMs() == 0) {
            script.setTimeoutMs(GroovyScriptEngine.DEFAULT_TIMEOUT_MS);
        }
        if (script.getLanguage() == null) {
            script.setLanguage("groovy");
        }

        scripts.put(script.getId(), script);
        log.info("Script registered: {} (id: {})", script.getName(), script.getId());
        return script;
    }

    public ScriptDefinition updateScript(String id, ScriptDefinition updated) {
        ScriptDefinition existing = scripts.get(id);
        if (existing == null) {
            throw new IllegalArgumentException("Script not found: " + id);
        }

        updated.setId(id);
        updated.setCreatedAt(existing.getCreatedAt());
        updated.setUpdatedAt(Instant.now());
        String currentVersion = existing.getVersion();
        String[] parts = currentVersion.split("\\.");
        int major = Integer.parseInt(parts[0]);
        int minor = Integer.parseInt(parts[1]);
        updated.setVersion(major + "." + (minor + 1));

        scripts.put(id, updated);
        scriptEngine.invalidateScriptCache(id);
        log.info("Script updated: {} (version: {})", updated.getName(), updated.getVersion());
        return updated;
    }

    public void deleteScript(String id) {
        ScriptDefinition removed = scripts.remove(id);
        if (removed != null) {
            scriptEngine.invalidateScriptCache(id);
            log.info("Script deleted: {} (id: {})", removed.getName(), id);
        }
    }

    public ScriptDefinition getScript(String id) {
        return scripts.get(id);
    }

    public Collection<ScriptDefinition> getAllScripts() {
        return scripts.values();
    }

    public List<ScriptDefinition> getScriptsForInterceptPoint(ScriptDefinition.InterceptPoint point,
                                                              String route,
                                                              String interactionType) {
        return scripts.values().stream()
                .filter(ScriptDefinition::isEnabled)
                .filter(s -> s.getInterceptPoint() == point)
                .filter(s -> s.getRoutes() == null || s.getRoutes().isEmpty()
                        || s.getRoutes().contains(route)
                        || s.getRoutes().contains("*"))
                .filter(s -> s.getInteractionTypes() == null || s.getInteractionTypes().isEmpty()
                        || s.getInteractionTypes().contains(interactionType)
                        || s.getInteractionTypes().contains("*"))
                .sorted(Comparator.comparingInt(ScriptDefinition::getPriority))
                .collect(Collectors.toList());
    }

    public List<ScriptExecutionResult> executeBeforeInterceptors(InterceptContext context) {
        List<ScriptDefinition> interceptors = getScriptsForInterceptPoint(
                ScriptDefinition.InterceptPoint.BEFORE,
                context.getRoute(),
                context.getInteractionType()
        );

        List<ScriptExecutionResult> results = new ArrayList<>();
        for (ScriptDefinition script : interceptors) {
            ScriptExecutionResult result = executeScript(script, context);
            results.add(result);

            if (context.isRejected()) {
                log.info("Request rejected by script: {}", script.getName());
                break;
            }
        }
        return results;
    }

    public List<ScriptExecutionResult> executeAfterInterceptors(InterceptContext context) {
        List<ScriptDefinition> interceptors = getScriptsForInterceptPoint(
                ScriptDefinition.InterceptPoint.AFTER,
                context.getRoute(),
                context.getInteractionType()
        );

        List<ScriptExecutionResult> results = new ArrayList<>();
        for (ScriptDefinition script : interceptors) {
            ScriptExecutionResult result = executeScript(script, context);
            results.add(result);
        }
        return results;
    }

    public List<ScriptExecutionResult> executeErrorInterceptors(InterceptContext context) {
        List<ScriptDefinition> interceptors = getScriptsForInterceptPoint(
                ScriptDefinition.InterceptPoint.ERROR,
                context.getRoute(),
                context.getInteractionType()
        );

        List<ScriptExecutionResult> results = new ArrayList<>();
        for (ScriptDefinition script : interceptors) {
            ScriptExecutionResult result = executeScript(script, context);
            results.add(result);
        }
        return results;
    }

    private ScriptExecutionResult executeScript(ScriptDefinition script, InterceptContext context) {
        log.debug("Executing script: {} (point: {})", script.getName(), script.getInterceptPoint());
        return scriptEngine.execute(script, context);
    }

    public void reloadScript(String id) {
        ScriptDefinition script = scripts.get(id);
        if (script != null) {
            scriptEngine.invalidateScriptCache(id);
            script.setUpdatedAt(Instant.now());
            log.info("Script reloaded: {} (id: {})", script.getName(), id);
        }
    }

    public void reloadAllScripts() {
        scriptEngine.clearCache();
        scripts.values().forEach(s -> s.setUpdatedAt(Instant.now()));
        log.info("All scripts reloaded");
    }

    public ScriptDefinition createFromTemplate(String templateId, String name,
                                                ScriptDefinition.InterceptPoint interceptPoint) {
        ScriptDefinition template = templateLibrary.getTemplate(templateId);
        if (template == null) {
            throw new IllegalArgumentException("Template not found: " + templateId);
        }

        ScriptDefinition script = ScriptDefinition.builder()
                .name(name)
                .description("Created from template: " + template.getName())
                .script(template.getScript())
                .language(template.getLanguage())
                .interceptPoint(interceptPoint)
                .routes(template.getRoutes())
                .interactionTypes(template.getInteractionTypes())
                .priority(template.getPriority())
                .enabled(true)
                .timeoutMs(template.getTimeoutMs())
                .fallbackStrategy(template.getFallbackStrategy())
                .templateId(templateId)
                .build();

        return registerScript(script);
    }
}
