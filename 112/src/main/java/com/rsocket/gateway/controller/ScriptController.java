package com.rsocket.gateway.controller;

import com.rsocket.gateway.scripting.ScriptManager;
import com.rsocket.gateway.scripting.ScriptTemplateLibrary;
import com.rsocket.gateway.scripting.model.ScriptDefinition;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.RequiredArgsConstructor;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import reactor.core.publisher.Mono;

import java.util.Collection;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/scripts")
@RequiredArgsConstructor
public class ScriptController {

    private final ScriptManager scriptManager;
    private final ScriptTemplateLibrary templateLibrary;

    @GetMapping
    public Mono<Collection<ScriptDefinition>> getAllScripts() {
        return Mono.just(scriptManager.getAllScripts());
    }

    @GetMapping("/{id}")
    public Mono<ResponseEntity<ScriptDefinition>> getScript(@PathVariable String id) {
        ScriptDefinition script = scriptManager.getScript(id);
        if (script == null) {
            return Mono.just(ResponseEntity.notFound().build());
        }
        return Mono.just(ResponseEntity.ok(script));
    }

    @PostMapping
    public Mono<ScriptDefinition> createScript(@RequestBody ScriptDefinition script) {
        return Mono.just(scriptManager.registerScript(script));
    }

    @PutMapping("/{id}")
    public Mono<ResponseEntity<ScriptDefinition>> updateScript(
            @PathVariable String id,
            @RequestBody ScriptDefinition script) {
        try {
            ScriptDefinition updated = scriptManager.updateScript(id, script);
            return Mono.just(ResponseEntity.ok(updated));
        } catch (IllegalArgumentException e) {
            return Mono.just(ResponseEntity.notFound().build());
        }
    }

    @DeleteMapping("/{id}")
    public Mono<ResponseEntity<Void>> deleteScript(@PathVariable String id) {
        scriptManager.deleteScript(id);
        return Mono.just(ResponseEntity.noContent().build());
    }

    @PostMapping("/{id}/reload")
    public Mono<ResponseEntity<Void>> reloadScript(@PathVariable String id) {
        scriptManager.reloadScript(id);
        return Mono.just(ResponseEntity.ok().build());
    }

    @PostMapping("/reload-all")
    public Mono<ResponseEntity<Void>> reloadAllScripts() {
        scriptManager.reloadAllScripts();
        return Mono.just(ResponseEntity.ok().build());
    }

    @GetMapping("/templates")
    public Mono<Collection<ScriptDefinition>> getTemplates() {
        return Mono.just(templateLibrary.getTemplates());
    }

    @GetMapping("/templates/{templateId}")
    public Mono<ResponseEntity<ScriptDefinition>> getTemplate(@PathVariable String templateId) {
        ScriptDefinition template = templateLibrary.getTemplate(templateId);
        if (template == null) {
            return Mono.just(ResponseEntity.notFound().build());
        }
        return Mono.just(ResponseEntity.ok(template));
    }

    @PostMapping("/templates/{templateId}/create")
    public Mono<ResponseEntity<ScriptDefinition>> createFromTemplate(
            @PathVariable String templateId,
            @RequestBody CreateFromTemplateRequest request) {
        try {
            ScriptDefinition script = scriptManager.createFromTemplate(
                    templateId,
                    request.getName(),
                    request.getInterceptPoint()
            );
            return Mono.just(ResponseEntity.ok(script));
        } catch (IllegalArgumentException e) {
            return Mono.just(ResponseEntity.notFound().build());
        }
    }

    @PostMapping("/{id}/toggle")
    public Mono<ResponseEntity<ScriptDefinition>> toggleScript(@PathVariable String id) {
        ScriptDefinition script = scriptManager.getScript(id);
        if (script == null) {
            return Mono.just(ResponseEntity.notFound().build());
        }
        script.setEnabled(!script.isEnabled());
        ScriptDefinition updated = scriptManager.updateScript(id, script);
        return Mono.just(ResponseEntity.ok(updated));
    }

    @GetMapping("/by-intercept-point/{point}")
    public Mono<List<ScriptDefinition>> getScriptsByInterceptPoint(
            @PathVariable ScriptDefinition.InterceptPoint point,
            @RequestParam(required = false) String route,
            @RequestParam(required = false) String interactionType) {
        return Mono.just(scriptManager.getScriptsForInterceptPoint(
                point,
                route != null ? route : "*",
                interactionType != null ? interactionType : "*"
        ));
    }

    @PostMapping("/execute")
    public Mono<Map<String, Object>> executeScript(
            @RequestBody ScriptDefinition script,
            @RequestParam(required = false) String testRoute,
            @RequestParam(required = false) String testInteractionType) {
        return Mono.fromCallable(() -> {
            var context = com.rsocket.gateway.scripting.model.InterceptContext.builder()
                    .interactionType(testInteractionType != null ? testInteractionType : "request-response")
                    .route(testRoute != null ? testRoute : "test.route")
                    .payload(Map.of("test", "data"))
                    .metadata(new java.util.HashMap<>())
                    .clientId("test-client")
                    .connectionId("test-conn-" + System.currentTimeMillis())
                    .timestamp(java.time.Instant.now())
                    .build();

            com.rsocket.gateway.scripting.model.ScriptExecutionResult result =
                    new com.rsocket.gateway.scripting.engine.GroovyScriptEngine().execute(script, context);

            return Map.of(
                    "success", result.isSuccess(),
                    "timedOut", result.isTimedOut(),
                    "executionTimeMs", result.getExecutionTime().toMillis(),
                    "errorMessage", result.getErrorMessage() != null ? result.getErrorMessage() : "",
                    "rejected", context.isRejected(),
                    "rejectionReason", context.getRejectionReason() != null ? context.getRejectionReason() : "",
                    "modified", context.isModified(),
                    "result", result.getResult() != null ? result.getResult() : ""
            );
        });
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class CreateFromTemplateRequest {
        private String name;
        private ScriptDefinition.InterceptPoint interceptPoint;
    }
}
