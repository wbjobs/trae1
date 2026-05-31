package com.profile.model;

import java.io.Serializable;
import java.util.HashMap;
import java.util.Map;

public class UserProfileState implements Serializable {

    private static final long serialVersionUID = 1L;

    private String userId;
    private Map<String, Double> tagScores;
    private long lastUpdateTime;
    private long lastDecayTime;
    private int eventCount;

    public UserProfileState() {
        this.tagScores = new HashMap<>();
        this.lastUpdateTime = 0L;
        this.lastDecayTime = 0L;
        this.eventCount = 0;
    }

    public UserProfileState(String userId) {
        this.userId = userId;
        this.tagScores = new HashMap<>();
        this.lastUpdateTime = 0L;
        this.lastDecayTime = 0L;
        this.eventCount = 0;
    }

    public String getUserId() { return userId; }
    public void setUserId(String userId) { this.userId = userId; }

    public Map<String, Double> getTagScores() { return tagScores; }
    public void setTagScores(Map<String, Double> tagScores) { this.tagScores = tagScores; }

    public long getLastUpdateTime() { return lastUpdateTime; }
    public void setLastUpdateTime(long lastUpdateTime) { this.lastUpdateTime = lastUpdateTime; }

    public long getLastDecayTime() { return lastDecayTime; }
    public void setLastDecayTime(long lastDecayTime) { this.lastDecayTime = lastDecayTime; }

    public int getEventCount() { return eventCount; }
    public void setEventCount(int eventCount) { this.eventCount = eventCount; }

    public void incrementEventCount() { this.eventCount++; }

    public void addScore(String tagCode, double score) {
        tagScores.merge(tagCode, score, Double::sum);
    }

    public void applyDecay(String tagCode, double factor) {
        Double current = tagScores.get(tagCode);
        if (current != null) {
            tagScores.put(tagCode, current * factor);
        }
    }
}
