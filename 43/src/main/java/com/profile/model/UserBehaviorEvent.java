package com.profile.model;

import java.io.Serializable;

public class UserBehaviorEvent implements Serializable {

    private static final long serialVersionUID = 1L;

    private String userId;
    private String action;
    private String category;
    private String itemId;
    private long timestamp;
    private double price;

    public UserBehaviorEvent() {
    }

    public UserBehaviorEvent(String userId, String action, String category, String itemId,
                             long timestamp, double price) {
        this.userId = userId;
        this.action = action;
        this.category = category;
        this.itemId = itemId;
        this.timestamp = timestamp;
        this.price = price;
    }

    public String getUserId() { return userId; }
    public void setUserId(String userId) { this.userId = userId; }

    public String getAction() { return action; }
    public void setAction(String action) { this.action = action; }

    public String getCategory() { return category; }
    public void setCategory(String category) { this.category = category; }

    public String getItemId() { return itemId; }
    public void setItemId(String itemId) { this.itemId = itemId; }

    public long getTimestamp() { return timestamp; }
    public void setTimestamp(long timestamp) { this.timestamp = timestamp; }

    public double getPrice() { return price; }
    public void setPrice(double price) { this.price = price; }
}
