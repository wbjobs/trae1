package com.clickstream.store;

import com.clickstream.model.BlacklistEntry;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

@Component
public class BlacklistStore {

    private static final Logger logger = LoggerFactory.getLogger(BlacklistStore.class);

    private final Map<String, BlacklistEntry> userBlacklist = new ConcurrentHashMap<>();
    private final Map<String, BlacklistEntry> ipBlacklist = new ConcurrentHashMap<>();
    
    private static final int BLACKLIST_RETENTION_DAYS = 30;

    public void addToBlacklist(BlacklistEntry entry) {
        if (entry.getUserId() != null && !entry.getUserId().equals("multiple-users")) {
            userBlacklist.put(entry.getUserId(), entry);
            logger.info("Added user to blacklist: userId={}, reason={}", entry.getUserId(), entry.getReason());
        }
        
        if (entry.getIpAddress() != null && !entry.getIpAddress().equals("unknown")) {
            ipBlacklist.put(entry.getIpAddress(), entry);
            logger.info("Added IP to blacklist: ip={}, reason={}", entry.getIpAddress(), entry.getReason());
        }
    }

    public boolean isUserBlacklisted(String userId) {
        BlacklistEntry entry = userBlacklist.get(userId);
        return entry != null && entry.getStatus() == BlacklistEntry.BlacklistStatus.ACTIVE;
    }

    public boolean isIpBlacklisted(String ipAddress) {
        BlacklistEntry entry = ipBlacklist.get(ipAddress);
        return entry != null && entry.getStatus() == BlacklistEntry.BlacklistStatus.ACTIVE;
    }

    public BlacklistEntry getUserBlacklistEntry(String userId) {
        return userBlacklist.get(userId);
    }

    public BlacklistEntry getIpBlacklistEntry(String ipAddress) {
        return ipBlacklist.get(ipAddress);
    }

    public Set<String> getAllBlacklistedUsers() {
        return new HashSet<>(userBlacklist.keySet());
    }

    public Set<String> getAllBlacklistedIps() {
        return new HashSet<>(ipBlacklist.keySet());
    }

    public int getBlacklistedUserCount() {
        return userBlacklist.size();
    }

    public int getBlacklistedIpCount() {
        return ipBlacklist.size();
    }

    public List<BlacklistEntry> getRecentBlacklistEntries(int limit) {
        List<BlacklistEntry> allEntries = new ArrayList<>();
        allEntries.addAll(userBlacklist.values());
        allEntries.addAll(ipBlacklist.values());
        
        allEntries.sort((a, b) -> b.getBlacklistedAt().compareTo(a.getBlacklistedAt()));
        
        return allEntries.stream().limit(limit).toList();
    }

    public void removeFromUserBlacklist(String userId) {
        userBlacklist.remove(userId);
        logger.info("Removed user from blacklist: userId={}", userId);
    }

    public void removeFromIpBlacklist(String ipAddress) {
        ipBlacklist.remove(ipAddress);
        logger.info("Removed IP from blacklist: ip={}", ipAddress);
    }

    @Scheduled(fixedRate = 3600000)
    public void cleanExpiredBlacklistEntries() {
        Instant threshold = Instant.now().minus(BLACKLIST_RETENTION_DAYS, ChronoUnit.DAYS);
        
        int removedUsers = 0;
        int removedIps = 0;
        
        Iterator<Map.Entry<String, BlacklistEntry>> userIterator = userBlacklist.entrySet().iterator();
        while (userIterator.hasNext()) {
            Map.Entry<String, BlacklistEntry> entry = userIterator.next();
            if (entry.getValue().getBlacklistedAt().isBefore(threshold)) {
                userIterator.remove();
                removedUsers++;
            }
        }
        
        Iterator<Map.Entry<String, BlacklistEntry>> ipIterator = ipBlacklist.entrySet().iterator();
        while (ipIterator.hasNext()) {
            Map.Entry<String, BlacklistEntry> entry = ipIterator.next();
            if (entry.getValue().getBlacklistedAt().isBefore(threshold)) {
                ipIterator.remove();
                removedIps++;
            }
        }
        
        if (removedUsers > 0 || removedIps > 0) {
            logger.info("Cleaned expired blacklist entries: {} users, {} IPs", removedUsers, removedIps);
        }
    }
}
