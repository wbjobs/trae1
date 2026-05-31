package com.configcenter.server.service;

import com.configcenter.server.model.ConfigHistoryEntity;
import com.configcenter.server.model.ConfigKeyEntity;
import com.configcenter.server.store.ConfigRepository;
import com.configcenter.server.store.ConfigRepository.CurrentRecord;
import com.configcenter.server.store.ConfigRepository.StoreException;
import com.configcenter.server.watch.WatchManager;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;
import org.yaml.snakeyaml.Yaml;

import java.util.List;
import java.util.Set;

@Slf4j
@Service
@RequiredArgsConstructor
public class ConfigService {

    private static final Set<String> VALID_PROFILES = Set.of("dev", "staging", "prod");

    private final ConfigRepository repository;
    private final WatchManager watchManager;

    public CurrentRecord getCurrent(ConfigKeyEntity key) {
        validate(key);
        return repository.getCurrent(key);
    }

    public CurrentRecord publish(ConfigKeyEntity key, String content, String operator) {
        validate(key);
        validateYaml(content);
        CurrentRecord previous = repository.getCurrent(key);
        String changeType = previous == null
                ? ConfigHistoryEntity.typeCreate()
                : ConfigHistoryEntity.typeUpdate();
        CurrentRecord next = repository.write(key, content, operator, changeType, previous);
        watchManager.broadcast(key, next, "MODIFIED");
        return next;
    }

    public CurrentRecord rollback(ConfigKeyEntity key, long targetVersion, String operator) {
        validate(key);
        ConfigHistoryEntity target = repository.getHistoryByVersion(key, targetVersion);
        if (target == null) {
            throw new StoreException("version not found: " + targetVersion);
        }
        CurrentRecord previous = repository.getCurrent(key);
        CurrentRecord next = repository.write(key, target.getContent(), operator,
                ConfigHistoryEntity.typeRollback(), previous);
        watchManager.broadcast(key, next, "MODIFIED");
        return next;
    }

    public void delete(ConfigKeyEntity key, String operator) {
        validate(key);
        CurrentRecord previous = repository.getCurrent(key);
        if (previous == null) return;
        repository.delete(key, previous, operator);
        watchManager.broadcast(key, previous, "DELETED");
    }

    public List<ConfigHistoryEntity> getHistory(ConfigKeyEntity key, int limit) {
        validate(key);
        return repository.getHistory(key, limit);
    }

    public ConfigHistoryEntity getHistoryVersion(ConfigKeyEntity key, long version) {
        validate(key);
        return repository.getHistoryByVersion(key, version);
    }

    public List<String> listApplications() {
        return repository.listApplications();
    }

    public List<String> listProfiles(String application) {
        if (application == null || application.isBlank()) {
            throw new StoreException("application is required");
        }
        return List.of("dev", "staging", "prod");
    }

    public List<ConfigKeyEntity> listKeys(String application, String profile) {
        validate(application, profile);
        return repository.listKeysUnderProfile(application, profile);
    }

    private void validate(ConfigKeyEntity key) {
        validate(key.getApplication(), key.getProfile());
    }

    private void validate(String application, String profile) {
        if (application == null || application.isBlank()) {
            throw new StoreException("application is required");
        }
        if (profile == null || profile.isBlank()) {
            throw new StoreException("profile is required");
        }
        if (!VALID_PROFILES.contains(profile)) {
            throw new StoreException("invalid profile: " + profile + " (expected dev/staging/prod)");
        }
    }

    private void validateYaml(String content) {
        if (content == null || content.isBlank()) return;
        try {
            new Yaml().load(content);
        } catch (Exception e) {
            throw new StoreException("invalid YAML: " + e.getMessage());
        }
    }
}
