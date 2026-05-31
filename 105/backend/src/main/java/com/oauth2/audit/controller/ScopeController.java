package com.oauth2.audit.controller;

import com.oauth2.audit.config.ScopeConfig;
import lombok.RequiredArgsConstructor;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

@RestController
@RequestMapping("/api/scopes")
@RequiredArgsConstructor
@CrossOrigin(origins = "*")
public class ScopeController {
    private final ScopeConfig scopeConfig;

    @GetMapping
    public ResponseEntity<Map<String, Object>> getAllScopes() {
        Map<String, ScopeConfig.ScopeInfo> scopeInfoMap = scopeConfig.getScopeInfoMap();

        Map<String, List<ScopeConfig.ScopeInfo>> grouped = scopeInfoMap.values().stream()
            .collect(Collectors.groupingBy(
                info -> info.getGroup().getCode(),
                Collectors.mapping(info -> info, Collectors.toList())
            ));

        return ResponseEntity.ok(Map.of(
            "scopes", scopeInfoMap,
            "grouped", grouped
        ));
    }

    @GetMapping("/grouped")
    public ResponseEntity<Map<String, List<Map<String, Object>>>> getGroupedScopes(@RequestParam List<String> requestedScopes) {
        Map<ScopeConfig.ScopeGroup, List<ScopeConfig.ScopeInfo>> grouped = scopeConfig.groupScopes(requestedScopes);

        Map<String, List<Map<String, Object>>> result = grouped.entrySet().stream()
            .collect(Collectors.toMap(
                entry -> entry.getKey().getCode(),
                entry -> entry.getValue().stream()
                    .map(info -> Map.<String, Object>of(
                        "scope", info.getScope(),
                        "displayName", info.getDisplayName(),
                        "description", info.getDescription(),
                        "highRisk", info.isHighRisk(),
                        "group", info.getGroup().getDisplayName()
                    ))
                    .collect(Collectors.toList())
            ));

        return ResponseEntity.ok(result);
    }

    @GetMapping("/high-risk")
    public ResponseEntity<Boolean> hasHighRiskScopes(@RequestParam List<String> scopes) {
        return ResponseEntity.ok(scopeConfig.hasHighRiskScope(scopes));
    }
}
